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
            return DatasetExperimentPanel::tr( "Draft" );
        case sicnu::dataset::DatasetVersionStatus::Committed:
            return DatasetExperimentPanel::tr( "Submitted" );
        case sicnu::dataset::DatasetVersionStatus::Deprecated:
            return DatasetExperimentPanel::tr( "Deprecated" );
    }
    return DatasetExperimentPanel::tr( "Unknown" );
}

QString qualityText( sicnu::dataset::DatasetQualityLevel level )
{
    switch ( level )
    {
        case sicnu::dataset::DatasetQualityLevel::Unassessed:
            return DatasetExperimentPanel::tr( "Not assessed" );
        case sicnu::dataset::DatasetQualityLevel::Draft:
            return DatasetExperimentPanel::tr( "Draft Grade" );
        case sicnu::dataset::DatasetQualityLevel::Valid:
            return DatasetExperimentPanel::tr( "Valid" );
        case sicnu::dataset::DatasetQualityLevel::Certified:
            return DatasetExperimentPanel::tr( "Authenticated" );
    }
    return DatasetExperimentPanel::tr( "Unknown" );
}

QString runStatusText( sicnu::dataset::RunStatus status )
{
    switch ( status )
    {
        case sicnu::dataset::RunStatus::Created:
            return DatasetExperimentPanel::tr( "Created" );
        case sicnu::dataset::RunStatus::Running:
            return DatasetExperimentPanel::tr( "Running" );
        case sicnu::dataset::RunStatus::Interrupted:
            return DatasetExperimentPanel::tr( "Interrupted" );
        case sicnu::dataset::RunStatus::Cancelling:
            return DatasetExperimentPanel::tr( "Cancelling" );
        case sicnu::dataset::RunStatus::Cancelled:
            return DatasetExperimentPanel::tr( "Cancelled" );
        case sicnu::dataset::RunStatus::Failed:
            return DatasetExperimentPanel::tr( "Failed" );
        case sicnu::dataset::RunStatus::Completed:
            return DatasetExperimentPanel::tr( "Finished" );
    }
    return DatasetExperimentPanel::tr( "Unknown" );
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
    m_openDatasetDb = new QPushButton( tr( "Open Dataset Library..." ), datasetTab );
    m_openDatasetDb->setObjectName( QStringLiteral( "rsOpenDatasetDb" ) );
    m_datasetDbLabel = new QLabel( tr( "Not open" ), datasetTab );
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

    auto *samplesLabel = new QLabel( tr( "First-page sample preview (up to %1 rows; the library holds the complete data)" )
                                        .arg( sicnu::dataset::DatasetStore::kMaxPageSize ),
                                    datasetTab );
    m_samplePreview = new QPlainTextEdit( datasetTab );
    m_samplePreview->setReadOnly( true );
    m_samplePreview->setMaximumHeight( 140 );
    datasetLayout->addWidget( samplesLabel );
    datasetLayout->addWidget( m_samplePreview );

    m_tabs->addTab( datasetTab, tr( "Dataset" ) );

    // ── 实验 tab ───────────────────────────────────────────────────────
    auto *experimentTab = new QWidget( m_tabs );
    auto *experimentLayout = new QVBoxLayout( experimentTab );

    auto *edbRow = new QHBoxLayout;
    m_openExperimentDb = new QPushButton( tr( "Open Experiment Library..." ), experimentTab );
    m_openExperimentDb->setObjectName( QStringLiteral( "rsOpenExperimentDb" ) );
    m_experimentDbLabel = new QLabel( tr( "Not open" ), experimentTab );
    edbRow->addWidget( m_openExperimentDb );
    edbRow->addWidget( m_experimentDbLabel, 1 );
    experimentLayout->addLayout( edbRow );

    m_experimentCombo = new QComboBox( experimentTab );
    m_experimentCombo->setObjectName( QStringLiteral( "rsExperimentCombo" ) );
    experimentLayout->addWidget( m_experimentCombo );

    m_runsTable = new QTableWidget( experimentTab );
    m_runsTable->setObjectName( QStringLiteral( "rsExperimentRunsTable" ) );
    m_runsTable->setColumnCount( 4 );
    m_runsTable->setHorizontalHeaderLabels( { tr( "Run ID" ), tr( "Status" ),
                                              tr( "Algorithm" ), tr( "Dataset Version" ) } );
    m_runsTable->horizontalHeader()->setStretchLastSection( true );
    m_runsTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_runsTable->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_runsTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
    experimentLayout->addWidget( m_runsTable, 1 );

    auto *runActionRow = new QHBoxLayout;
    m_compareBtn = new QPushButton( tr( "Compare Selected Runs" ), experimentTab );
    runActionRow->addWidget( m_compareBtn );
    runActionRow->addStretch( 1 );
    experimentLayout->addLayout( runActionRow );

    m_runDetail = new QPlainTextEdit( experimentTab );
    m_runDetail->setReadOnly( true );
    m_runDetail->setMaximumHeight( 160 );
    experimentLayout->addWidget( m_runDetail );

    m_tabs->addTab( experimentTab, tr( "Experiment" ) );

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
        this, tr( "Open Dataset Library" ), QString(), tr( "SQLite Databases (*.db *.sqlite);;All Files (*)" ) );
    if ( path.isEmpty() )
        return;
    // DatasetStore::open() creates missing files — a typo'd path must not
    // silently become a brand-new authoritative store in the GUI.
    if ( !QFileInfo::exists( path ) )
    {
        m_datasetDbLabel->setText( tr( "File not found: %1" ).arg( path ) );
        return;
    }
    QString error;
    if ( !m_datasetStore.open( path, &error ) )
    {
        m_datasetDbLabel->setText( tr( "Open failed: %1" ).arg( error ) );
        return;
    }
    m_datasetDbLabel->setText( path );
    rebuildDatasets();
}

void DatasetExperimentPanel::openExperimentStore()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr( "Open Experiment Library" ), QString(), tr( "SQLite Databases (*.db *.sqlite);;All Files (*)" ) );
    if ( path.isEmpty() )
        return;
    if ( !QFileInfo::exists( path ) )
    {
        m_experimentDbLabel->setText( tr( "File not found: %1" ).arg( path ) );
        return;
    }
    QString error;
    if ( !m_experimentStore.open( path, &error ) )
    {
        m_experimentDbLabel->setText( tr( "Open failed: %1" ).arg( error ) );
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
            tr( "Read failed: %1" )
                .arg( page.diagnostics().isEmpty() ? tr( "Unknown error" )
                                                   : page.diagnostics().first().message ) );
        return;
    }
    m_datasets = page->second;
    for ( const QVariantMap &row : m_datasets )
        m_datasetCombo->addItem(
            QStringLiteral( "%1 (%2)" ).arg( row.value( "name" ).toString(),
                                             row.value( "id" ).toString() ),
            row.value( "id" ).toString() );
    m_datasetDbLabel->setText( tr( "%1 — %2 datasets in total" )
                                   .arg( m_datasetStore.storePath() )
                                   .arg( page->first ) );
    blocker.unblock();
    // Always rebuild dependents: a reopened (possibly empty) store must never
    // keep showing the previous store's versions/samples.
    rebuildVersions();
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
        m_versionDetail->setText( tr( "This dataset has no versions yet." ) );
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
    lines << tr( "Version %1 (%2, quality: %3)" )
                 .arg( version.versionId(), statusText( version.status() ),
                       qualityText( version.qualityLevel() ) );
    if ( !version.parentVersionId().isEmpty() )
        lines << tr( "Parent version: %1" ).arg( version.parentVersionId() );
    if ( version.committedAtUtc().isValid() )
        lines << tr( "Submitted at: %1" ).arg( version.committedAtUtc().toString( Qt::ISODate ) );
    lines << tr( "Fingerprint: %1" )
                 .arg( version.fingerprint().isEmpty() ? tr( "Not computed" ) : version.fingerprint() );
    if ( !version.note().isEmpty() )
        lines << tr( "Description: %1" ).arg( version.note() );
    lines << tr( "Samples: %1" ).arg( sampleTotal );

    // Label schema + splits live in the manifest document — project the
    // document's OWN references (label_schema.schema_id, split_manifests)
    // without re-deriving anything. Leakage/quality reports and reproduction
    // bundles are separate CLI/agent artifacts; the GUI states that plainly
    // instead of inventing numbers.
    QString schemaId;
    QStringList splitManifests;
    if ( !version.manifestJson().isEmpty() )
    {
        const QJsonDocument doc =
            QJsonDocument::fromJson( version.manifestJson().toUtf8() );
        if ( doc.isObject() )
        {
            const QJsonObject obj = doc.object();
            schemaId = obj.value( QStringLiteral( "label_schema" ) )
                         .toObject()
                         .value( QStringLiteral( "schema_id" ) )
                         .toString();
            const QJsonArray splits =
                obj.value( QStringLiteral( "split_manifests" ) ).toArray();
            for ( const QJsonValue &value : splits )
                splitManifests << value.toString();
        }
    }
    lines << tr( "Labeling scheme: %1" )
                 .arg( schemaId.isEmpty() ? tr( "Not declared in the manifest" ) : schemaId );
    if ( !splitManifests.isEmpty() )
        lines << tr( "Split list: %1" ).arg( splitManifests.join( QStringLiteral( ", " ) ) );
    else
        lines << tr( "Split list: none (not split or not declared)" );
    lines << tr( "Leak audit / quality report / reproduction bundle: generated by CLI / Agent pipelines; this panel does not recompute them." );

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
            preview.isEmpty() ? tr( "(no samples)" ) : preview.join( QStringLiteral( "\n" ) ) );
    }
    else
    {
        m_samplePreview->setPlainText( tr( "Failed to read samples (the library is read-only or the version does not exist)." ) );
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
            tr( "Read failed: %1" )
                .arg( page.diagnostics().isEmpty() ? tr( "Unknown error" )
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
                                  tr( "%1 experiments in total" ).arg( page->first ) );
    blocker.unblock();
    rebuildRuns();
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
    // First-page projection: state the truncation instead of implying "all".
    m_runDetail->setPlaceholderText(
        page->first > static_cast<qint64>( m_runs.size() )
            ? tr( "Run metrics (showing the first %1 of %2 runs)." )
                  .arg( m_runs.size() )
                  .arg( page->first )
            : tr( "Run metrics (%1 runs in total)." ).arg( page->first ) );
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
    {
        if ( const QTableWidgetItem *idItem = m_runsTable->item( rows.first().row(), 0 ) )
            showMetricJson( idItem->text() );
    }
}

void DatasetExperimentPanel::showMetricJson( const QString &runId )
{
    const auto record = m_experimentStore.metricRecordForRun( runId );
    if ( !record.has_value() )
    {
        m_runDetail->setPlainText( tr( "This run has no recorded metrics." ) );
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
    const QTableWidgetItem *itemA = m_runsTable->item( rows[0].row(), 0 );
    const QTableWidgetItem *itemB = m_runsTable->item( rows[1].row(), 0 );
    if ( !itemA || !itemB )
        return;
    const QString runA = itemA->text();
    const QString runB = itemB->text();
    const auto metricA = m_experimentStore.metricRecordForRun( runA );
    const auto metricB = m_experimentStore.metricRecordForRun( runB );
    if ( !metricA.has_value() || !metricB.has_value() )
    {
        m_runDetail->setPlainText( tr( "Both runs need recorded metrics to compare (metrics from one side only are shown otherwise)." ) );
        return;
    }

    // Metric-level diff over top-level keys, PLUS the comparability identity
    // pins the experiment layer gates on: runs on different dataset
    // versions/fingerprints are flagged incomparable rather than silently
    // placed side by side.
    const auto runRecordA = m_experimentStore.runById( runA );
    const auto runRecordB = m_experimentStore.runById( runB );
    QJsonObject report;
    report.insert( QStringLiteral( "run_a" ), runA );
    report.insert( QStringLiteral( "run_b" ), runB );
    if ( runRecordA.has_value() && runRecordB.has_value() )
    {
        const bool sameDataset =
            runRecordA->datasetVersionId() == runRecordB->datasetVersionId() &&
            runRecordA->datasetFingerprint() == runRecordB->datasetFingerprint();
        report.insert( QStringLiteral( "comparable_dataset_identity" ), sameDataset );
        report.insert( QStringLiteral( "dataset_version_a" ), runRecordA->datasetVersionId() );
        report.insert( QStringLiteral( "dataset_version_b" ), runRecordB->datasetVersionId() );
        report.insert( QStringLiteral( "dataset_fingerprint_a" ), runRecordA->datasetFingerprint() );
        report.insert( QStringLiteral( "dataset_fingerprint_b" ), runRecordB->datasetFingerprint() );
    }
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
