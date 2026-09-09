/***************************************************************************
 * model_workbench_panel.cpp — see model_workbench_panel.h
 ***************************************************************************/
#include "model_workbench_panel.h"

#include "operators/framework/model_catalog.h"
#include "operators/runtime/model_runtime.h"

#include "processing/framework/task_center.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace sicnu::app
{
namespace
{

QString readinessText( sicnu::operators::ModelReadiness readiness )
{
    switch ( readiness )
    {
        case sicnu::operators::ModelReadiness::Ready:
            return ModelWorkbenchPanel::tr( "就绪" );
        case sicnu::operators::ModelReadiness::MissingArtifact:
            return ModelWorkbenchPanel::tr( "权重缺失" );
        case sicnu::operators::ModelReadiness::InvalidManifest:
            return ModelWorkbenchPanel::tr( "清单无效" );
        case sicnu::operators::ModelReadiness::ChecksumMismatch:
            return ModelWorkbenchPanel::tr( "校验和不匹配" );
        case sicnu::operators::ModelReadiness::UnsupportedRuntime:
            return ModelWorkbenchPanel::tr( "运行时不支持" );
        case sicnu::operators::ModelReadiness::IncompatibleHardware:
            return ModelWorkbenchPanel::tr( "硬件不兼容" );
    }
    return ModelWorkbenchPanel::tr( "未知" );
}

QString weightSummary( const sicnu::operators::ModelInfo &model )
{
    if ( model.path.empty() )
        return ModelWorkbenchPanel::tr( "无权重文件" );
    QFileInfo info( QString::fromStdString( model.path ) );
    if ( !info.exists() )
        return ModelWorkbenchPanel::tr( "文件不存在" );
    const double mib = info.size() / ( 1024.0 * 1024.0 );
    return ModelWorkbenchPanel::tr( "%1 MiB" ).arg( mib, 0, 'f', 1 );
}

} // namespace

ModelWorkbenchPanel::ModelWorkbenchPanel( QWidget *parent )
    : QgsDockWidget( parent )
{
    auto *central = new QWidget( this );
    auto *layout = new QVBoxLayout( central );

    auto *toolRow = new QHBoxLayout;
    m_search = new QLineEdit( central );
    m_search->setPlaceholderText( tr( "按名称 / 任务 / 标签过滤" ) );
    m_search->setClearButtonEnabled( true );
    m_reloadBtn = new QPushButton( tr( "重新扫描目录" ), central );
    m_reloadBtn->setObjectName( QStringLiteral( "rsModelReload" ) );
    m_backendCombo = new QComboBox( central );
    m_backendCombo->addItem( tr( "CPU" ), QStringLiteral( "cpu" ) );
    m_backendCombo->addItem( tr( "CUDA" ), QStringLiteral( "cuda" ) );
    toolRow->addWidget( m_search, 1 );
    toolRow->addWidget( new QLabel( tr( "设备" ), central ) );
    toolRow->addWidget( m_backendCombo );
    toolRow->addWidget( m_reloadBtn );
    layout->addLayout( toolRow );

    m_modelTable = new QTableWidget( central );
    m_modelTable->setObjectName( QStringLiteral( "rsModelTable" ) );
    m_modelTable->setColumnCount( 6 );
    m_modelTable->setHorizontalHeaderLabels(
        { tr( "模型" ), tr( "任务" ), tr( "框架" ), tr( "设备" ), tr( "权重" ), tr( "状态" ) } );
    m_modelTable->horizontalHeader()->setStretchLastSection( true );
    m_modelTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_modelTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
    m_modelTable->verticalHeader()->setVisible( false );
    layout->addWidget( m_modelTable, 2 );

    auto *actionRow = new QHBoxLayout;
    m_testInferenceBtn = new QPushButton( tr( "测试推理…" ), central );
    m_testInferenceBtn->setObjectName( QStringLiteral( "rsModelTestInference" ) );
    actionRow->addWidget( m_testInferenceBtn );
    actionRow->addStretch( 1 );
    layout->addLayout( actionRow );

    m_manifestView = new QPlainTextEdit( central );
    m_manifestView->setReadOnly( true );
    m_manifestView->setMaximumHeight( 180 );
    m_manifestView->setPlaceholderText( tr( "清单（manifest）细节" ) );
    layout->addWidget( m_manifestView );

    m_issuesView = new QPlainTextEdit( central );
    m_issuesView->setReadOnly( true );
    m_issuesView->setMaximumHeight( 90 );
    m_issuesView->setPlaceholderText( tr( "目录装载诊断（无问题时为空）" ) );
    layout->addWidget( m_issuesView );

    m_statusLabel = new QLabel( central );
    m_statusLabel->setWordWrap( true );
    layout->addWidget( m_statusLabel );

    setWidget( central );

    connect( m_reloadBtn, &QPushButton::clicked, this, &ModelWorkbenchPanel::reloadCatalog );
    connect( m_search, &QLineEdit::textChanged, this, &ModelWorkbenchPanel::rebuildModelTable );
    connect( m_backendCombo, &QComboBox::currentIndexChanged, this,
             &ModelWorkbenchPanel::rebuildModelTable );
    connect( m_modelTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             [this]( const QItemSelection &, const QItemSelection & ) { showModelDetail(); } );
    connect( m_testInferenceBtn, &QPushButton::clicked, this,
             &ModelWorkbenchPanel::runTestInference );

    refreshCatalog();
}

void ModelWorkbenchPanel::refreshCatalog()
{
    sicnu::operators::ModelCatalog::instance().reload();
    rebuildModelTable();

    const auto issues = sicnu::operators::ModelCatalog::instance().issues();
    if ( issues.empty() )
    {
        m_issuesView->clear();
    }
    else
    {
        QStringList lines;
        for ( const auto &issue : issues )
            lines << QStringLiteral( "%1: %2" )
                         .arg( QString::fromStdString( issue.manifestPath ),
                               QString::fromStdString( issue.message ) );
        m_issuesView->setPlainText( lines.join( QLatin1Char( '\n' ) ) );
    }
}

void ModelWorkbenchPanel::reloadCatalog()
{
    refreshCatalog();
    m_statusLabel->setText( tr( "模型目录已重新扫描（%1 个模型）。" )
                                .arg( m_modelTable->rowCount() ) );
}

void ModelWorkbenchPanel::rebuildModelTable()
{
    const auto models = sicnu::operators::ModelCatalog::instance().models();
    const QString needle = m_search->text().trimmed();
    const bool wantCuda = m_backendCombo->currentData().toString() == QLatin1String( "cuda" );

    m_modelNames.clear();
    for ( const auto &model : models )
    {
        if ( !needle.isEmpty() )
        {
            const QString haystack = QStringLiteral( "%1 %2 %3 %4" )
                                         .arg( QString::fromStdString( model.name ),
                                               QString::fromStdString( model.task ),
                                               QString::fromStdString( model.framework ) )
                                         .arg( [&model] {
                                             QStringList tags;
                                             for ( const auto &tag : model.tags )
                                                 tags << QString::fromStdString( tag );
                                             return tags.join( QLatin1Char( ' ' ) );
                                         }() );
            if ( !haystack.contains( needle, Qt::CaseInsensitive ) )
                continue;
        }
        if ( wantCuda && !model.gpu )
            continue; // device filter is an honest intersection, not a promise
        m_modelNames.append( model.name );
    }

    m_modelTable->setRowCount( static_cast<int>( m_modelNames.size() ) );
    const auto hw = sicnu::operators::runtime::ModelHardwareCapabilities::detect();
    for ( int row = 0; row < static_cast<int>( m_modelNames.size() ); ++row )
    {
        const auto model = sicnu::operators::ModelCatalog::instance().find( m_modelNames[row] );
        if ( !model )
            continue;
        // Readiness is evaluated by the runtime layer — the panel never guesses.
        std::string reason;
        const auto readiness = sicnu::operators::runtime::evaluateRuntimeReadiness( *model, hw, &reason );
        QTableWidgetItem *statusItem =
            new QTableWidgetItem( readinessText( readiness ) +
                                  ( reason.empty() ? QString()
                                                   : QStringLiteral( " — %1" )
                                                         .arg( QString::fromStdString( reason ) ) ) );
        m_modelTable->setItem( row, 0,
                               new QTableWidgetItem( QString::fromStdString( model->name ) ) );
        m_modelTable->setItem( row, 1, new QTableWidgetItem( QString::fromStdString( model->task ) ) );
        m_modelTable->setItem( row, 2,
                               new QTableWidgetItem( QString::fromStdString( model->framework ) ) );
        m_modelTable->setItem( row, 3,
                               new QTableWidgetItem( model->gpu ? tr( "GPU/CPU" ) : tr( "CPU" ) ) );
        m_modelTable->setItem( row, 4, new QTableWidgetItem( weightSummary( *model ) ) );
        m_modelTable->setItem( row, 5, statusItem );
    }
}

void ModelWorkbenchPanel::showModelDetail()
{
    const QModelIndex current = m_modelTable->currentIndex();
    m_testInferenceBtn->setEnabled( current.isValid() );
    if ( !current.isValid() )
    {
        m_manifestView->clear();
        return;
    }
    const QString name = m_modelTable->item( current.row(), 0 )->text();
    // inspect() returns the full registry record (manifest + health).
    const Json::Value record =
        sicnu::operators::ModelCatalog::instance().inspect( name.toStdString() );
    if ( record.isNull() )
    {
        m_manifestView->setPlainText( tr( "（该模型无注册记录）" ) );
        return;
    }
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    m_manifestView->setPlainText(
        QString::fromStdString( Json::writeString( builder, record ) ) );
}

void ModelWorkbenchPanel::runTestInference()
{
    const QModelIndex current = m_modelTable->currentIndex();
    if ( !current.isValid() )
        return;
    const QString modelName = m_modelTable->item( current.row(), 0 )->text();

    // Readiness gate BEFORE submission: never enqueue work the runtime layer
    // already knows will fail.
    const auto model = sicnu::operators::ModelCatalog::instance().find( modelName.toStdString() );
    if ( !model )
        return;
    const auto hw = sicnu::operators::runtime::ModelHardwareCapabilities::detect();
    std::string reason;
    const auto readiness = sicnu::operators::runtime::evaluateRuntimeReadiness( *model, hw, &reason );
    if ( readiness != sicnu::operators::ModelReadiness::Ready )
    {
        m_statusLabel->setText(
            tr( "模型未就绪（%1），已取消提交：%2" )
                .arg( readinessText( readiness ),
                      QString::fromStdString( reason.empty() ? "unknown" : reason ) ) );
        return;
    }

    const QString inputPath = QFileDialog::getOpenFileName(
        this, tr( "选择测试输入栅格（%1）" ).arg( modelName ), QString(),
        tr( "栅格文件 (*.tif *.tiff *.img *.dat);;所有文件 (*)" ) );
    if ( inputPath.isEmpty() )
        return;

    QVariantMap params;
    params.insert( QStringLiteral( "model" ), modelName );
    params.insert( QStringLiteral( "input" ), inputPath );
    // rs:infer requires an explicit output destination (never writes in place).
    QFileInfo inputInfo( inputPath );
    const QString outputPath = QDir( inputInfo.absolutePath() ).filePath(
        inputInfo.completeBaseName() + QStringLiteral( "_%1_infer.tif" ).arg( modelName ) );
    params.insert( QStringLiteral( "output" ), outputPath );
    const long taskId = sicnu::TaskCenter::instance().enqueueTask(
        QStringLiteral( "rs:infer" ), params, /*autoLoad=*/false,
        sicnu::TaskPriority::Normal, QList<long>(), /*autoDispatch=*/true, 0,
        QStringLiteral( "gui" ) );
    m_statusLabel->setText(
        taskId > 0
            ? tr( "测试推理已提交（任务 %1），进度与失败原因见处理历史。" ).arg( taskId )
            : tr( "提交被任务中心拒绝（资源或参数问题）——详见处理历史/日志。" ) );
    if ( taskId > 0 )
        emit inferenceSubmitted( taskId );
}

} // namespace sicnu::app
