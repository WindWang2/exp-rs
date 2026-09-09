/***************************************************************************
 * dataset_experiment_panel.cpp — see dataset_experiment_panel.h
 ***************************************************************************/
#include "dataset_experiment_panel.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include "dataset/sample.h"
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sicnu::app
{
namespace
{

QString statusText( sicnu::dataset::DatasetVersionStatus status )
{
    switch ( status )
    {
        case sicnu::dataset::DatasetVersionStatus::Draft:
            return DatasetExperimentPanel::tr( "草稿" );
        case sicnu::dataset::DatasetVersionStatus::Committed:
            return DatasetExperimentPanel::tr( "已提交" );
        case sicnu::dataset::DatasetVersionStatus::Deprecated:
            return DatasetExperimentPanel::tr( "已弃用" );
    }
    return DatasetExperimentPanel::tr( "未知" );
}

QString qualityText( sicnu::dataset::DatasetQualityLevel level )
{
    switch ( level )
    {
        case sicnu::dataset::DatasetQualityLevel::Unassessed:
            return DatasetExperimentPanel::tr( "未评估" );
        case sicnu::dataset::DatasetQualityLevel::Draft:
            return DatasetExperimentPanel::tr( "草稿级" );
        case sicnu::dataset::DatasetQualityLevel::Valid:
            return DatasetExperimentPanel::tr( "有效" );
        case sicnu::dataset::DatasetQualityLevel::Certified:
            return DatasetExperimentPanel::tr( "已认证" );
    }
    return DatasetExperimentPanel::tr( "未知" );
}

QString runStatusText( sicnu::dataset::RunStatus status )
{
    switch ( status )
    {
        case sicnu::dataset::RunStatus::Created:
            return DatasetExperimentPanel::tr( "已创建" );
        case sicnu::dataset::RunStatus::Running:
            return DatasetExperimentPanel::tr( "运行中" );
        case sicnu::dataset::RunStatus::Interrupted:
            return DatasetExperimentPanel::tr( "已中断" );
        case sicnu::dataset::RunStatus::Cancelling:
            return DatasetExperimentPanel::tr( "取消中" );
        case sicnu::dataset::RunStatus::Cancelled:
            return DatasetExperimentPanel::tr( "已取消" );
        case sicnu::dataset::RunStatus::Failed:
            return DatasetExperimentPanel::tr( "失败" );
        case sicnu::dataset::RunStatus::Completed:
            return DatasetExperimentPanel::tr( "已完成" );
    }
    return DatasetExperimentPanel::tr( "未知" );
}

} // namespace

DatasetExperimentPanel::DatasetExperimentPanel( QWidget *parent )
    : QgsDockWidget( parent )
{
    m_tabs = new QTabWidget( this );

    // ── 数据集 tab ─────────────────────────────────────────────────────
    auto *datasetTab = new QWidget( m_tabs );
    auto *datasetLayout = new QVBoxLayout( datasetTab );

    auto *dbRow = new QHBoxLayout;
    m_openDatasetDb = new QPushButton( tr( "打开数据集库…" ), datasetTab );
    m_openDatasetDb->setObjectName( QStringLiteral( "rsOpenDatasetDb" ) );
    m_datasetDbLabel = new QLabel( tr( "未打开" ), datasetTab );
    dbRow->addWidget( m_openDatasetDb );
    dbRow->addWidget( m_datasetDbLabel, 1 );
    datasetLayout->addLayout( dbRow );

    m_datasetCombo = new QComboBox( datasetTab );
    m_datasetCombo->setObjectName( QStringLiteral( "rsDatasetCombo" ) );
    datasetLayout->addWidget( m_datasetCombo );

    m_versionList = new QListWidget( datasetTab );
    m_versionList->setObjectName( QStringLiteral( "rsDatasetVersionList" ) );
    datasetLayout->addWidget( m_versionList, 1 );

    m_versionDetail = new QLabel( datasetTab );
    m_versionDetail->setWordWrap( true );
    m_versionDetail->setTextFormat( Qt::RichText );
    datasetLayout->addWidget( m_versionDetail );

    auto *samplesLabel = new QLabel( tr( "样本首页预览（最多 %1 条，完整数据以库为准）" )
                                        .arg( sicnu::dataset::DatasetStore::kMaxPageSize ),
                                    datasetTab );
    m_samplePreview = new QPlainTextEdit( datasetTab );
    m_samplePreview->setReadOnly( true );
    m_samplePreview->setMaximumHeight( 140 );
    datasetLayout->addWidget( samplesLabel );
    datasetLayout->addWidget( m_samplePreview );

    m_tabs->addTab( datasetTab, tr( "数据集" ) );

    // ── 实验 tab ───────────────────────────────────────────────────────
    auto *experimentTab = new QWidget( m_tabs );
    auto *experimentLayout = new QVBoxLayout( experimentTab );

    auto *edbRow = new QHBoxLayout;
    m_openExperimentDb = new QPushButton( tr( "打开实验库…" ), experimentTab );
    m_openExperimentDb->setObjectName( QStringLiteral( "rsOpenExperimentDb" ) );
    m_experimentDbLabel = new QLabel( tr( "未打开" ), experimentTab );
    edbRow->addWidget( m_openExperimentDb );
    edbRow->addWidget( m_experimentDbLabel, 1 );
    experimentLayout->addLayout( edbRow );

    m_experimentCombo = new QComboBox( experimentTab );
    m_experimentCombo->setObjectName( QStringLiteral( "rsExperimentCombo" ) );
    experimentLayout->addWidget( m_experimentCombo );

    m_runsTable = new QTableWidget( experimentTab );
    m_runsTable->setObjectName( QStringLiteral( "rsExperimentRunsTable" ) );
    m_runsTable->setColumnCount( 4 );
    m_runsTable->setHorizontalHeaderLabels( { tr( "运行 ID" ), tr( "状态" ),
                                              tr( "算法" ), tr( "数据集版本" ) } );
    m_runsTable->horizontalHeader()->setStretchLastSection( true );
    m_runsTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_runsTable->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_runsTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
    experimentLayout->addWidget( m_runsTable, 1 );

    auto *runActionRow = new QHBoxLayout;
    m_compareBtn = new QPushButton( tr( "对比选中运行" ), experimentTab );
    runActionRow->addWidget( m_compareBtn );
    runActionRow->addStretch( 1 );
    experimentLayout->addLayout( runActionRow );

    m_runDetail = new QPlainTextEdit( experimentTab );
    m_runDetail->setReadOnly( true );
    m_runDetail->setMaximumHeight( 160 );
    experimentLayout->addWidget( m_runDetail );

    m_tabs->addTab( experimentTab, tr( "实验" ) );

    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 4, 4, 4, 4 );
    layout->addWidget( m_tabs );
    setWidget( m_tabs );

    connect( m_openDatasetDb, &QPushButton::clicked, this,
             &DatasetExperimentPanel::openDatasetStore );
    connect( m_openExperimentDb, &QPushButton::clicked, this,
             &DatasetExperimentPanel::openExperimentStore );
    connect( m_datasetCombo, &QComboBox::currentIndexChanged, this,
             &DatasetExperimentPanel::onDatasetSelected );
    connect( m_versionList, &QListWidget::currentRowChanged, this,
             &DatasetExperimentPanel::onVersionSelected );
    connect( m_experimentCombo, &QComboBox::currentIndexChanged, this,
             &DatasetExperimentPanel::onExperimentSelected );
    connect( m_runsTable->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             &DatasetExperimentPanel::onRunsSelectionChanged );
    connect( m_compareBtn, &QPushButton::clicked, this,
             &DatasetExperimentPanel::compareSelectedRuns );

    m_compareBtn->setEnabled( false );
}

void DatasetExperimentPanel::openDatasetStore()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr( "打开数据集库" ), QString(), tr( "SQLite 数据库 (*.db *.sqlite);;所有文件 (*)" ) );
    if ( path.isEmpty() )
        return;
    QString error;
    if ( !m_datasetStore.open( path, &error ) )
    {
        m_datasetDbLabel->setText( tr( "打开失败：%1" ).arg( error ) );
        return;
    }
    m_datasetDbLabel->setText( path );
    rebuildDatasets();
}

void DatasetExperimentPanel::openExperimentStore()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr( "打开实验库" ), QString(), tr( "SQLite 数据库 (*.db *.sqlite);;所有文件 (*)" ) );
    if ( path.isEmpty() )
        return;
    QString error;
    if ( !m_experimentStore.open( path, &error ) )
    {
        m_experimentDbLabel->setText( tr( "打开失败：%1" ).arg( error ) );
        return;
    }
    m_experimentDbLabel->setText( path );
    rebuildExperiments();
}

void DatasetExperimentPanel::rebuildDatasets()
{
    QSignalBlocker blocker( m_datasetCombo );
    m_datasetCombo->clear();
    m_datasets.clear();
    if ( !m_datasetStore.isOpen() )
        return;
    // First page (the store itself is paged; total count is surfaced below).
    const auto page = m_datasetStore.listDatasets( 0, sicnu::dataset::DatasetStore::kMaxPageSize );
    if ( !page.has_value() )
    {
        m_datasetDbLabel->setText(
            tr( "读取失败：%1" )
                .arg( page.diagnostics().isEmpty() ? tr( "未知错误" )
                                                   : page.diagnostics().first().message ) );
        return;
    }
    m_datasets = page->second;
    for ( const QVariantMap &row : m_datasets )
        m_datasetCombo->addItem(
            QStringLiteral( "%1 (%2)" ).arg( row.value( "name" ).toString(),
                                             row.value( "id" ).toString() ),
            row.value( "id" ).toString() );
    m_datasetDbLabel->setText( tr( "%1 — 共 %2 个数据集" )
                                   .arg( m_datasetStore.storePath() )
                                   .arg( page->first ) );
    blocker.unblock();
    if ( !m_datasets.isEmpty() )
        onDatasetSelected( 0 );
}

void DatasetExperimentPanel::rebuildVersions()
{
    m_versionList->clear();
    m_versions.clear();
    const QString datasetId = m_datasetCombo->currentData().toString();
    if ( datasetId.isEmpty() )
        return;
    const auto id = sicnu::dataset::DatasetId::fromString( datasetId );
    if ( !id )
        return;
    m_versions = m_datasetStore.versionsOfDataset( *id );
    for ( const sicnu::dataset::DatasetVersionRecord &version : m_versions )
        m_versionList->addItem(
            QStringLiteral( "%1 — %2" ).arg( version.versionId(), statusText( version.status() ) ) );
    if ( m_versions.isEmpty() )
        m_versionDetail->setText( tr( "该数据集暂无版本。" ) );
}

void DatasetExperimentPanel::onDatasetSelected( int index )
{
    Q_UNUSED( index );
    rebuildVersions();
}

void DatasetExperimentPanel::onVersionSelected( int row )
{
    if ( row < 0 || row >= m_versions.size() )
        return;
    const sicnu::dataset::DatasetVersionRecord &version = m_versions[row];
    const auto versionId = sicnu::dataset::DatasetVersionId::fromString( version.versionId() );
    if ( !versionId )
        return;

    // Bounded, truthful facts only: counts + manifest refs, no derived stats.
    const qint64 sampleTotal = m_datasetStore.sampleCount( *versionId );
    QStringList lines;
    lines << tr( "版本 %1（%2，质量：%3）" )
                 .arg( version.versionId(), statusText( version.status() ),
                       qualityText( version.qualityLevel() ) );
    if ( !version.parentVersionId().isEmpty() )
        lines << tr( "父版本：%1" ).arg( version.parentVersionId() );
    if ( version.committedAtUtc().isValid() )
        lines << tr( "提交时间：%1" ).arg( version.committedAtUtc().toString( Qt::ISODate ) );
    lines << tr( "指纹：%1" )
                 .arg( version.fingerprint().isEmpty() ? tr( "未计算" ) : version.fingerprint() );
    if ( !version.note().isEmpty() )
        lines << tr( "说明：%1" ).arg( version.note() );
    lines << tr( "样本数：%1" ).arg( sampleTotal );

    // Label schema + splits live in the manifest document — project the
    // reference lists without re-deriving anything.
    const auto schemaVersions =
        m_datasetStore.labelSchemaVersions( version.datasetId() );
    lines << tr( "标注方案版本：%1 条" ).arg( schemaVersions.size() );

    m_versionDetail->setText( lines.join( QStringLiteral( "<br/>" ) ) );

    // First page of samples (bounded preview; the store paginates for us).
    m_samplePreview->clear();
    const auto samples = m_datasetStore.samplesPage( *versionId, 0, 20 );
    if ( samples.has_value() )
    {
        QStringList preview;
        for ( const sicnu::dataset::SampleRecord &sample : samples->second )
            preview << sample.sampleId();
        m_samplePreview->setPlainText(
            preview.isEmpty() ? tr( "（无样本）" ) : preview.join( QStringLiteral( "\n" ) ) );
    }
    else
    {
        m_samplePreview->setPlainText( tr( "样本读取失败（库为只读或版本不存在）。" ) );
    }
}

void DatasetExperimentPanel::rebuildExperiments()
{
    QSignalBlocker blocker( m_experimentCombo );
    m_experimentCombo->clear();
    m_experiments.clear();
    if ( !m_experimentStore.isOpen() )
        return;
    const auto page = m_experimentStore.listExperiments( 0, sicnu::experiment::ExperimentStore::kMaxPageSize );
    if ( !page.has_value() )
    {
        m_experimentDbLabel->setText(
            tr( "读取失败：%1" )
                .arg( page.diagnostics().isEmpty() ? tr( "未知错误" )
                                                   : page.diagnostics().first().message ) );
        return;
    }
    m_experiments = page->second;
    for ( const sicnu::experiment::Experiment &experiment : m_experiments )
        m_experimentCombo->addItem(
            QStringLiteral( "%1 (%2)" ).arg( experiment.name(), experiment.experimentId() ),
            experiment.experimentId() );
    m_experimentDbLabel->setText( m_experimentDbLabel->text() +
                                  QStringLiteral( " — " ) +
                                  tr( "共 %1 个实验" ).arg( page->first ) );
    blocker.unblock();
    if ( !m_experiments.isEmpty() )
        onExperimentSelected( 0 );
}

void DatasetExperimentPanel::rebuildRuns()
{
    m_runsTable->setRowCount( 0 );
    m_runs.clear();
    const QString experimentId = m_experimentCombo->currentData().toString();
    if ( experimentId.isEmpty() )
        return;
    const auto page = m_experimentStore.listRuns( experimentId, QString(), QString(), 0,
                                                  sicnu::experiment::ExperimentStore::kMaxPageSize );
    if ( !page.has_value() )
        return;
    m_runs = page->second;
    m_runsTable->setRowCount( m_runs.size() );
    for ( int i = 0; i < m_runs.size(); ++i )
    {
        const sicnu::experiment::ExperimentRun &run = m_runs[i];
        m_runsTable->setItem( i, 0, new QTableWidgetItem( run.runId() ) );
        m_runsTable->setItem( i, 1, new QTableWidgetItem( runStatusText( run.status() ) ) );
        m_runsTable->setItem( i, 2, new QTableWidgetItem( run.algorithmId() ) );
        m_runsTable->setItem( i, 3, new QTableWidgetItem( run.datasetVersionId() ) );
    }
}

void DatasetExperimentPanel::onExperimentSelected( int index )
{
    Q_UNUSED( index );
    rebuildRuns();
}

void DatasetExperimentPanel::onRunsSelectionChanged()
{
    const QModelIndexList rows =
        m_runsTable->selectionModel() ? m_runsTable->selectionModel()->selectedRows()
                                      : QModelIndexList();
    m_compareBtn->setEnabled( rows.size() == 2 );
    if ( rows.size() == 1 )
        showMetricJson( m_runsTable->item( rows.first().row(), 0 )->text() );
}

void DatasetExperimentPanel::showMetricJson( const QString &runId )
{
    const auto record = m_experimentStore.metricRecordForRun( runId );
    if ( !record.has_value() )
    {
        m_runDetail->setPlainText( tr( "该运行没有指标记录。" ) );
        return;
    }
    m_runDetail->setPlainText(
        QString::fromUtf8( QJsonDocument( record->metrics ).toJson( QJsonDocument::Indented ) ) );
}

void DatasetExperimentPanel::compareSelectedRuns()
{
    const QModelIndexList rows =
        m_runsTable->selectionModel() ? m_runsTable->selectionModel()->selectedRows()
                                      : QModelIndexList();
    if ( rows.size() != 2 )
        return;
    const QString runA = m_runsTable->item( rows[0].row(), 0 )->text();
    const QString runB = m_runsTable->item( rows[1].row(), 0 )->text();
    const auto metricA = m_experimentStore.metricRecordForRun( runA );
    const auto metricB = m_experimentStore.metricRecordForRun( runB );
    if ( !metricA.has_value() || !metricB.has_value() )
    {
        m_runDetail->setPlainText( tr( "两个运行都有指标记录才能对比（缺一侧则显示单侧指标）。" ) );
        return;
    }

    // Metric-level diff over top-level keys: truthfully lists both sides and
    // flags identity pins that differ (dataset/fingerprint), which the
    // experiment layer treats as comparability gates.
    QJsonObject report;
    report.insert( QStringLiteral( "run_a" ), runA );
    report.insert( QStringLiteral( "run_b" ), runB );
    const QJsonObject metricsA = metricA->metrics;
    const QJsonObject metricsB = metricB->metrics;
    QStringList onlyA;
    QStringList onlyB;
    QJsonObject diff;
    for ( auto it = metricsA.begin(); it != metricsA.end(); ++it )
    {
        if ( !metricsB.contains( it.key() ) )
        {
            onlyA << it.key();
            continue;
        }
        if ( metricsB.value( it.key() ) != it.value() )
            diff.insert( it.key(),
                         QJsonObject{ { "a", it.value() }, { "b", metricsB.value( it.key() ) } } );
    }
    for ( auto it = metricsB.begin(); it != metricsB.end(); ++it )
        if ( !metricsA.contains( it.key() ) )
            onlyB << it.key();
    report.insert( QStringLiteral( "only_in_a" ), QJsonArray::fromStringList( onlyA ) );
    report.insert( QStringLiteral( "only_in_b" ), QJsonArray::fromStringList( onlyB ) );
    report.insert( QStringLiteral( "differing" ), diff );

    m_runDetail->setPlainText(
        QString::fromUtf8( QJsonDocument( report ).toJson( QJsonDocument::Indented ) ) );
}

} // namespace sicnu::app
