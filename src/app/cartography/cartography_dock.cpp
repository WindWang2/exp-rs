/***************************************************************************
 * cartography_dock.cpp — desktop surface over the cartography operator family
 *
 * Production 11.0: every action runs as a TaskCenter job (the SAME
 * registry dispatch a workflow node takes) — no operator executes on the
 * GUI thread anymore. The dock keeps its no-engine rule: it drafts specs,
 * submits cartography:* operators, and renders their structured reports.
 ***************************************************************************/
#include "cartography_dock.h"

#include "agent/cartography/registry.h"
#include "operators/framework/rs_operator_registry.h"

#include "../shell/rs_job_runner.h"

#include <processing/framework/task_center.h>

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <qgslayoutexporter.h>
#include <qgsprintlayout.h>

#include "../agent/layout_tools/layout_service.h"

namespace sicnu::app
{

namespace
{

using sicnu::agent::layout_tools::LayoutService;

/// Bounded preview budget: the label shows a page-shaped projection, the
/// export stays the evidence path.
constexpr int kMaxPreviewWidthPx = 1100;
constexpr double kPreviewDpi = 72.0;

QString prettyJson( const Json::Value &value )
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return QString::fromStdString( Json::writeString( builder, value ) );
}

} // namespace

CartographyDock::CartographyDock( LayerSourcesProvider layers, WorkDirProvider workDir,
                                  QWidget *parent )
    : QgsDockWidget( parent )
    , m_layers( std::move( layers ) )
    , m_workDir( std::move( workDir ) )
{
    setObjectName( QStringLiteral( "rsCartographyDock" ) );
    setWindowTitle( tr( "制图工作台" ) );
    buildUi();
    reloadTemplates();
}

void CartographyDock::buildUi()
{
    auto *central = new QWidget( this );
    auto *layout = new QVBoxLayout( central );
    layout->setContentsMargins( 8, 8, 8, 8 );

    // ── Draft inputs ────────────────────────────────────────────────────
    auto *templateRow = new QHBoxLayout;
    m_templateCombo = new QComboBox( central );
    m_templateCombo->setObjectName( QStringLiteral( "rsCartographyTemplate" ) );
    m_templateCombo->setAccessibleName( tr( "地图模板" ) );
    m_templateCombo->setToolTip( tr( "模板来自制图组件目录（与 agent 工具同源）。" ) );
    templateRow->addWidget( new QLabel( tr( "模板：" ), central ), 0 );
    templateRow->addWidget( m_templateCombo, 1 );
    layout->addLayout( templateRow );

    m_layoutNameEdit = new QLineEdit( QStringLiteral( "wb-map" ), central );
    m_layoutNameEdit->setObjectName( QStringLiteral( "rsCartographyLayoutName" ) );
    m_layoutNameEdit->setAccessibleName( tr( "布局名称" ) );
    layout->addWidget( m_layoutNameEdit );

    m_titleEdit = new QLineEdit( central );
    m_titleEdit->setObjectName( QStringLiteral( "rsCartographyTitle" ) );
    m_titleEdit->setAccessibleName( tr( "地图标题" ) );
    m_titleEdit->setPlaceholderText( tr( "地图标题" ) );
    layout->addWidget( m_titleEdit );

    m_sourceNoteEdit = new QLineEdit( central );
    m_sourceNoteEdit->setObjectName( QStringLiteral( "rsCartographySourceNote" ) );
    m_sourceNoteEdit->setAccessibleName( tr( "数据来源说明" ) );
    m_sourceNoteEdit->setPlaceholderText( tr( "数据来源说明（可选）" ) );
    layout->addWidget( m_sourceNoteEdit );

    // ── Actions (each one an operator dispatch) ─────────────────────────
    auto *composeRow = new QHBoxLayout;
    m_composeBtn = new QPushButton( tr( "排版生成" ), central );
    m_composeBtn->setObjectName( QStringLiteral( "rsCartographyCompose" ) );
    m_composeBtn->setAccessibleName( tr( "排版生成" ) );
    m_preflightBtn = new QPushButton( tr( "检查" ), central );
    m_preflightBtn->setObjectName( QStringLiteral( "rsCartographyPreflight" ) );
    m_preflightBtn->setAccessibleName( tr( "检查" ) );
    m_repairBtn = new QPushButton( tr( "修复" ), central );
    m_repairBtn->setObjectName( QStringLiteral( "rsCartographyRepair" ) );
    m_repairBtn->setAccessibleName( tr( "修复" ) );
    composeRow->addWidget( m_composeBtn );
    composeRow->addWidget( m_preflightBtn );
    composeRow->addWidget( m_repairBtn );
    composeRow->addStretch( 1 );
    layout->addLayout( composeRow );

    // ── Export row ──────────────────────────────────────────────────────
    auto *exportRow = new QHBoxLayout;
    m_formatCombo = new QComboBox( central );
    m_formatCombo->setObjectName( QStringLiteral( "rsCartographyFormat" ) );
    m_formatCombo->setAccessibleName( tr( "导出格式" ) );
    m_formatCombo->addItems( { tr( "png" ), tr( "pdf" ), tr( "svg" ) } );
    m_dpiSpin = new QSpinBox( central );
    m_dpiSpin->setObjectName( QStringLiteral( "rsCartographyDpi" ) );
    m_dpiSpin->setAccessibleName( tr( "导出 DPI" ) );
    m_dpiSpin->setRange( 72, 1200 );
    m_dpiSpin->setValue( 300 );
    m_directoryEdit = new QLineEdit( central );
    m_directoryEdit->setObjectName( QStringLiteral( "rsCartographyDirectory" ) );
    m_directoryEdit->setAccessibleName( tr( "导出目录" ) );
    m_directoryEdit->setPlaceholderText( tr( "导出目录" ) );
    auto *browseBtn = new QPushButton( tr( "…选择目录" ), central );
    browseBtn->setAccessibleName( tr( "选择导出目录" ) );
    m_exportBtn = new QPushButton( tr( "导出" ), central );
    m_exportBtn->setObjectName( QStringLiteral( "rsCartographyExport" ) );
    m_exportBtn->setAccessibleName( tr( "导出" ) );
    m_produceBtn = new QPushButton( tr( "生产导出" ), central );
    m_produceBtn->setObjectName( QStringLiteral( "rsCartographyProduce" ) );
    m_produceBtn->setAccessibleName( tr( "生产导出（排版→修复→导出→清单）" ) );
    m_produceBtn->setToolTip(
      tr( "一次完成：排版 → 有界修复 → 导出（含图集）→ 清单 sidecar；原子发布。" ) );
    m_stopBtn = new QPushButton( tr( "停止" ), central );
    m_stopBtn->setObjectName( QStringLiteral( "rsCartographyStop" ) );
    m_stopBtn->setAccessibleName( tr( "停止当前制图任务" ) );
    m_stopBtn->setEnabled( false );
    exportRow->addWidget( m_formatCombo );
    exportRow->addWidget( new QLabel( tr( "DPI：" ), central ) );
    exportRow->addWidget( m_dpiSpin );
    exportRow->addWidget( m_directoryEdit, 1 );
    exportRow->addWidget( browseBtn );
    exportRow->addWidget( m_exportBtn );
    exportRow->addWidget( m_produceBtn );
    exportRow->addWidget( m_stopBtn );
    layout->addLayout( exportRow );

    connect( browseBtn, &QPushButton::clicked, this, [this] {
        const QString start = m_directoryEdit->text().isEmpty()
                                  ? ( m_workDir ? m_workDir() : QString() )
                                  : m_directoryEdit->text();
        const QString dir = QFileDialog::getExistingDirectory( this, tr( "导出目录" ), start );
        if ( !dir.isEmpty() )
            m_directoryEdit->setText( dir );
    } );

    // ── Preview + report ────────────────────────────────────────────────
    m_previewLabel = new QLabel( central );
    m_previewLabel->setObjectName( QStringLiteral( "rsCartographyPreview" ) );
    m_previewLabel->setAccessibleName( tr( "排版预览" ) );
    m_previewLabel->setMinimumSize( 320, 220 );
    m_previewLabel->setAlignment( Qt::AlignCenter );
    m_previewLabel->setText( tr( "暂无预览 —— 选择模板并点击「排版生成」。" ) );
    auto *scroll = new QScrollArea( central );
    scroll->setWidgetResizable( true );
    scroll->setWidget( m_previewLabel );
    layout->addWidget( scroll, 2 );

    m_reportView = new QPlainTextEdit( central );
    m_reportView->setObjectName( QStringLiteral( "rsCartographyReport" ) );
    m_reportView->setAccessibleName( tr( "质量报告" ) );
    m_reportView->setReadOnly( true );
    m_reportView->setPlaceholderText( tr( "检查/修复/导出报告显示在这里。" ) );
    layout->addWidget( m_reportView, 1 );

    connect( m_composeBtn, &QPushButton::clicked, this, &CartographyDock::runCompose );
    connect( m_preflightBtn, &QPushButton::clicked, this, &CartographyDock::runPreflight );
    connect( m_repairBtn, &QPushButton::clicked, this, &CartographyDock::runRepair );
    connect( m_exportBtn, &QPushButton::clicked, this, &CartographyDock::runExport );
    connect( m_produceBtn, &QPushButton::clicked, this, &CartographyDock::runProduce );
    connect( m_stopBtn, &QPushButton::clicked, this, &CartographyDock::cancelRunningJob );
    setWidget( central );
}

void CartographyDock::reloadTemplates()
{
    const Json::Value templates = sicnu::agent::cartography::TemplateRegistry::instance().templates();
    m_templateCombo->clear();
    for ( const auto &entry : templates )
    {
        if ( entry.isObject() && entry.isMember( "id" ) )
        {
            const QString id = QString::fromStdString( entry["id"].asString() );
            const QString description =
                entry.isMember( "description" ) && entry["description"].isString()
                    ? QString::fromStdString( entry["description"].asString() )
                    : QString();
            m_templateCombo->addItem( id, id );
            if ( !description.isEmpty() )
                m_templateCombo->setItemData( m_templateCombo->count() - 1, description,
                                              Qt::ToolTipRole );
        }
    }
}

QStringList CartographyDock::templateIds() const
{
    QStringList ids;
    for ( int i = 0; i < m_templateCombo->count(); ++i )
        ids.append( m_templateCombo->itemText( i ) );
    return ids;
}

QString CartographyDock::lastReportText() const
{
    return m_reportView ? m_reportView->toPlainText() : QString();
}

Json::Value CartographyDock::buildDraft( QString *error ) const
{
    const QString templateId = m_templateCombo->currentData().toString();
    if ( templateId.isEmpty() )
    {
        if ( error )
            *error = tr( "没有可用模板：请检查 data/cartography/templates 目录。" );
        return {};
    }
    const QStringList sources = m_layers ? m_layers() : QStringList();
    if ( sources.isEmpty() )
    {
        if ( error )
            *error = tr( "当前没有可用的地图图层：先在画布中加入数据，再生成排版。" );
        return {};
    }
    Json::Value params( Json::objectValue );
    params["layout_name"] = m_layoutNameEdit->text().trimmed().toStdString();
    if ( !m_titleEdit->text().trimmed().isEmpty() )
        params["title"] = m_titleEdit->text().trimmed().toStdString();
    if ( !m_sourceNoteEdit->text().trimmed().isEmpty() )
        params["source_note"] = m_sourceNoteEdit->text().trimmed().toStdString();
    Json::Value layers( Json::arrayValue );
    for ( const QString &source : sources )
        layers.append( source.toStdString() );
    params["layers"] = layers;

    QString instantiateError;
    const Json::Value draft = sicnu::agent::cartography::TemplateRegistry::instance()
                                  .instantiateTemplate( templateId, params, &instantiateError );
    if ( draft.isNull() )
    {
        if ( error )
            *error = tr( "模板实例化失败：%1" ).arg( instantiateError );
        return {};
    }
    return draft;
}

bool CartographyDock::submitOperatorJob(
  const QString &operatorId, const Json::Value &params,
  const std::function<void( const Json::Value &, const QString & )> &onDone )
{
    if ( m_runningTaskId >= 0 )
    {
        emit statusMessage( tr( "已有一个制图任务在执行（可点「停止」取消）。" ) );
        return false;
    }
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( operatorId.toStdString() );
    if ( !op )
    {
        emit statusMessage(
          tr( "算子未注册：%1（应用启动时应完成 cartography 算子族注册）" ).arg( operatorId ) );
        return false;
    }

    // Registry dispatch: the JobEngine resolves algorithmId through
    // RSOperatorRegistry and runs op->run(params, ctx) on a worker with
    // TaskCenter-owned cancel/progress wiring — the identical path a
    // workflow node takes. The dock adds no execution semantics.
    sicnu::jobs::JobRequest request;
    request.algorithmId = operatorId.toStdString();
    request.title = operatorId.toStdString();
    request.source = "ui";
    request.params = params;

    const long taskId = sicnu::TaskCenter::instance().submitJob( request );
    if ( taskId <= 0 )
    {
        emit statusMessage( tr( "%1 提交 TaskCenter 失败。" ).arg( operatorId ) );
        return false;
    }
    m_runningTaskId = taskId;
    setBusy( true );

    auto *lifetime = new QObject( this );
    // Progress reflection: TaskCenter owns the authoritative progress feed
    // (its task panel renders it); the dock mirrors a compact status line.
    // Both connections ride the per-job lifetime object so stale handlers
    // disappear with the job.
    connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated, lifetime,
             [ this ]( const sicnu::AlgorithmTaskInfo &info ) {
                 if ( info.taskId != m_runningTaskId )
                     return;
                 if ( info.status == sicnu::TaskStatus::Running &&
                      info.progressPercentage >= 0.0 )
                     emit statusMessage( tr( "制图任务进行中：%1%" )
                                           .arg( static_cast<int>( info.progressPercentage *
                                                                  100.0 ) ) );
             } );

    connect( &sicnu::TaskCenter::instance(), &sicnu::TaskCenter::taskUpdated, lifetime,
             [ this, lifetime, operatorId, onDone ]( const sicnu::AlgorithmTaskInfo &info ) {
                 if ( info.taskId != m_runningTaskId )
                     return;
                 if ( info.status != sicnu::TaskStatus::Completed &&
                      info.status != sicnu::TaskStatus::Failed &&
                      info.status != sicnu::TaskStatus::Canceled )
                     return;
                 m_runningTaskId = -1;
                 setBusy( false );
                 lifetime->deleteLater();
                 if ( info.status == sicnu::TaskStatus::Completed )
                 {
                     onDone( info.resultPayload, QString() );
                     return;
                 }
                 const QString message = info.errorMessage.isEmpty()
                                           ? tr( "%1 任务异常终止。" ).arg( operatorId )
                                           : info.errorMessage;
                 if ( !info.resultPayload.isNull() && m_reportView )
                     showReport( tr( "%1 结构化错误" ).arg( operatorId ), info.resultPayload );
                 onDone( {}, message );
             } );
    emit statusMessage( tr( "%1 已提交后台任务（#%2）。" ).arg( operatorId ).arg( taskId ) );
    return true;
}

void CartographyDock::setBusy( bool busy )
{
    m_composeBtn->setEnabled( !busy );
    m_preflightBtn->setEnabled( !busy );
    m_repairBtn->setEnabled( !busy );
    m_exportBtn->setEnabled( !busy );
    m_produceBtn->setEnabled( !busy );
    m_stopBtn->setEnabled( busy );
    m_templateCombo->setEnabled( !busy );
}

void CartographyDock::showReport( const QString &heading, const Json::Value &payload )
{
    if ( m_reportView )
        m_reportView->setPlainText( heading + "\n" + prettyJson( payload ) );
}

void CartographyDock::adoptSpec( const Json::Value &spec )
{
    m_currentSpec = spec;
    if ( spec.isObject() && spec.isMember( "layout_name" ) && spec["layout_name"].isString() )
        m_composedLayoutName = QString::fromStdString( spec["layout_name"].asString() );
}

void CartographyDock::runCompose()
{
    QString error;
    const Json::Value draft = buildDraft( &error );
    if ( !error.isEmpty() )
    {
        emit statusMessage( error );
        if ( m_reportView )
            m_reportView->setPlainText( error );
        return;
    }
    Json::Value params( Json::objectValue );
    params["mapspec"] = draft;
    const bool submitted = submitOperatorJob(
      QStringLiteral( "cartography:compose" ), params,
      [ this, draft ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          adoptSpec( result.isMember( "mapspec" ) ? result["mapspec"] : draft );
          const bool compiled = result.isMember( "compiled" ) && result["compiled"].asBool();
          showReport( compiled ? tr( "排版完成（结构化摘要）" ) : tr( "排版未通过" ), result );
          if ( compiled )
          {
              updatePreview( m_composedLayoutName );
              emit statusMessage( tr( "排版完成：布局 %1 已生成。" ).arg( m_composedLayoutName ) );
          }
          else
          {
              emit statusMessage( tr( "排版失败：见质量报告。" ) );
          }
      } );
    if ( submitted )
        m_reportView->setPlainText( tr( "排版任务已提交。" ) );
}

void CartographyDock::runPreflight()
{
    Json::Value spec = m_currentSpec;
    if ( spec.isNull() || spec.empty() )
    {
        QString error;
        spec = buildDraft( &error );
        if ( !error.isEmpty() )
        {
            emit statusMessage( error );
            return;
        }
    }
    Json::Value params( Json::objectValue );
    params["mapspec"] = spec;
    submitOperatorJob(
      QStringLiteral( "cartography:preflight" ), params,
      [ this ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          showReport( tr( "检查报告" ), result );
          emit statusMessage( tr( "检查完成：见质量报告。" ) );
      } );
}

void CartographyDock::runRepair()
{
    Json::Value spec = m_currentSpec;
    if ( spec.isNull() || spec.empty() )
    {
        QString error;
        spec = buildDraft( &error );
        if ( !error.isEmpty() )
        {
            emit statusMessage( error );
            return;
        }
    }
    Json::Value params( Json::objectValue );
    params["mapspec"] = spec;
    submitOperatorJob(
      QStringLiteral( "cartography:repair" ), params,
      [ this, spec ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          adoptSpec( result.isMember( "mapspec" ) ? result["mapspec"] : spec );
          showReport( tr( "修复台账（applied / still_reported）" ), result );
          emit statusMessage( tr( "修复完成：%1 项已应用（%2 轮）。" )
                                  .arg( result.isMember( "repairs_applied" )
                                            ? result["repairs_applied"].asInt()
                                            : 0 )
                                  .arg( result.isMember( "iterations" )
                                            ? result["iterations"].asInt()
                                            : 0 ) );
      } );
}

void CartographyDock::runExport()
{
    if ( m_composedLayoutName.isEmpty() )
    {
        emit statusMessage( tr( "还没有已排版的布局：先执行「排版生成」。" ) );
        return;
    }
    const QString directory =
        m_directoryEdit->text().trimmed().isEmpty() && m_workDir ? m_workDir()
                                                                 : m_directoryEdit->text().trimmed();
    if ( directory.isEmpty() )
    {
        emit statusMessage( tr( "请选择导出目录。" ) );
        return;
    }
    Json::Value params( Json::objectValue );
    params["layout"] = m_composedLayoutName.toStdString();
    params["format"] = m_formatCombo->currentText().toStdString();
    params["directory"] = directory.toStdString();
    params["dpi"] = m_dpiSpin->value();
    submitOperatorJob(
      QStringLiteral( "cartography:export" ), params,
      [ this ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          showReport( tr( "导出证据（原子写入 + sha256）" ), result );
          if ( !result.isObject() || !result.isMember( "path" ) || !result["path"].isString() )
          {
              emit statusMessage( tr( "导出返回缺少路径（见报告）。" ) );
              return;
          }
          emit statusMessage( tr( "导出完成：%1" )
                                  .arg( QString::fromStdString( result["path"].asString() ) ) );
      } );
}

void CartographyDock::runProduce()
{
    Json::Value spec = m_currentSpec;
    if ( spec.isNull() || spec.empty() )
    {
        QString error;
        spec = buildDraft( &error );
        if ( !error.isEmpty() )
        {
            emit statusMessage( error );
            return;
        }
    }
    const QString directory =
        m_directoryEdit->text().trimmed().isEmpty() && m_workDir ? m_workDir()
                                                                 : m_directoryEdit->text().trimmed();
    if ( directory.isEmpty() )
    {
        emit statusMessage( tr( "请选择导出目录。" ) );
        return;
    }
    Json::Value params( Json::objectValue );
    params["mapspec"] = spec;
    params["directory"] = directory.toStdString();
    params["format"] = m_formatCombo->currentText().toStdString();
    params["dpi"] = m_dpiSpin->value();
    params["write_manifest"] = true;
    submitOperatorJob(
      QStringLiteral( "cartography:produce" ), params,
      [ this ]( const Json::Value &result, const QString &jobError ) {
          if ( !jobError.isEmpty() )
          {
              emit statusMessage( jobError );
              return;
          }
          if ( result.isMember( "mapspec" ) )
              adoptSpec( result["mapspec"] );
          showReport( tr( "生产交付（原子发布 + 清单）" ), result );
          const QString delivered =
            result.isMember( "output" ) && result["output"].isString()
              ? QString::fromStdString( result["output"].asString() )
              : QString();
          const int pages = result.isMember( "page_count" ) ? result["page_count"].asInt() : 0;
          emit statusMessage( tr( "生产完成：%1（%2 页）" ).arg( delivered ).arg( pages ) );
      } );
}

void CartographyDock::cancelRunningJob()
{
    if ( m_runningTaskId < 0 )
        return;
    const long taskId = m_runningTaskId;
    if ( sicnu::TaskCenter::instance().cancelTask( taskId ) )
        emit statusMessage( tr( "已请求取消任务 #%1。" ).arg( taskId ) );
    else
        emit statusMessage( tr( "任务 #%1 无法取消（可能已结束）。" ).arg( taskId ) );
}

void CartographyDock::updatePreview( const QString &layoutName )
{
    QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
    if ( !layout )
    {
        m_previewLabel->setText( tr( "布局 %1 不可用（可能已被移除）。" ).arg( layoutName ) );
        return;
    }
    QgsLayoutExporter exporter( layout );
    const QImage image = exporter.renderPageToImage( 0, QSize(), kPreviewDpi );
    if ( image.isNull() )
    {
        m_previewLabel->setText( tr( "预览渲染失败（布局仍可导出）。" ) );
        return;
    }
    QPixmap pixmap = QPixmap::fromImage( image );
    if ( pixmap.width() > kMaxPreviewWidthPx )
        pixmap = pixmap.scaledToWidth( kMaxPreviewWidthPx, Qt::SmoothTransformation );
    m_previewLabel->setPixmap( pixmap );
}

} // namespace sicnu::app
