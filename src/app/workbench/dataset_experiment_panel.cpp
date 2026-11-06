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

#include <algorithm>

namespace sicnu::app
{
namespace
{

/// Sample-preview page size — the stores page catalogs by kMaxPageSize; the
/// sample table uses its own smaller window.
constexpr qint64 kSamplePageSize = 20;

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

    auto *datasetPageRow = new QHBoxLayout;
    m_datasetPrevPage = new QPushButton( tr( "Previous Page" ), datasetTab );
    m_datasetPrevPage->setObjectName( QStringLiteral( "rsDatasetPrevPage" ) );
    m_datasetPageLabel = new QLabel( datasetTab );
    m_datasetPageLabel->setObjectName( QStringLiteral( "rsDatasetPageLabel" ) );
    m_datasetNextPage = new QPushButton( tr( "Next Page" ), datasetTab );
    m_datasetNextPage->setObjectName( QStringLiteral( "rsDatasetNextPage" ) );
    datasetPageRow->addWidget( m_datasetPrevPage );
    datasetPageRow->addWidget( m_datasetPageLabel, 1 );
    datasetPageRow->addWidget( m_datasetNextPage );
    datasetLayout->addLayout( datasetPageRow );

    m_versionList = new QListWidget( datasetTab );
    m_versionList->setObjectName( QStringLiteral( "rsDatasetVersionList" ) );
    datasetLayout->addWidget( m_versionList, 1 );

    m_versionDetail = new QLabel( datasetTab );
    m_versionDetail->setWordWrap( true );
    m_versionDetail->setTextFormat( Qt::RichText );
    datasetLayout->addWidget( m_versionDetail );

    auto *samplesLabel = new QLabel( tr( "Sample preview (%1 rows per page; the library holds the complete data)" )
                                         .arg( kSamplePageSize ),
                                    datasetTab );
    auto *samplePageRow = new QHBoxLayout;
    m_samplePrevPage = new QPushButton( tr( "Previous Page" ), datasetTab );
    m_samplePrevPage->setObjectName( QStringLiteral( "rsSamplePrevPage" ) );
    m_samplePageLabel = new QLabel( datasetTab );
    m_samplePageLabel->setObjectName( QStringLiteral( "rsSamplePageLabel" ) );
    m_sampleNextPage = new QPushButton( tr( "Next Page" ), datasetTab );
    m_sampleNextPage->setObjectName( QStringLiteral( "rsSampleNextPage" ) );
    samplePageRow->addWidget( m_samplePrevPage );
    samplePageRow->addWidget( m_samplePageLabel, 1 );
    samplePageRow->addWidget( m_sampleNextPage );
    m_samplePreview = new QPlainTextEdit( datasetTab );
    m_samplePreview->setReadOnly( true );
    m_samplePreview->setMaximumHeight( 140 );
    datasetLayout->addWidget( samplesLabel );
    datasetLayout->addLayout( samplePageRow );
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

    auto *experimentPageRow = new QHBoxLayout;
    m_experimentPrevPage = new QPushButton( tr( "Previous Page" ), experimentTab );
    m_experimentPrevPage->setObjectName( QStringLiteral( "rsExperimentPrevPage" ) );
    m_experimentPageLabel = new QLabel( experimentTab );
    m_experimentPageLabel->setObjectName( QStringLiteral( "rsExperimentPageLabel" ) );
    m_experimentNextPage = new QPushButton( tr( "Next Page" ), experimentTab );
    m_experimentNextPage->setObjectName( QStringLiteral( "rsExperimentNextPage" ) );
    experimentPageRow->addWidget( m_experimentPrevPage );
    experimentPageRow->addWidget( m_experimentPageLabel, 1 );
    experimentPageRow->addWidget( m_experimentNextPage );
    experimentLayout->addLayout( experimentPageRow );

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
    m_runPrevPage = new QPushButton( tr( "Previous Page" ), experimentTab );
    m_runPrevPage->setObjectName( QStringLiteral( "rsRunPrevPage" ) );
    m_runPageLabel = new QLabel( experimentTab );
    m_runPageLabel->setObjectName( QStringLiteral( "rsRunPageLabel" ) );
    m_runNextPage = new QPushButton( tr( "Next Page" ), experimentTab );
    m_runNextPage->setObjectName( QStringLiteral( "rsRunNextPage" ) );
    m_compareBtn = new QPushButton( tr( "Compare Selected Runs" ), experimentTab );
    runActionRow->addWidget( m_runPrevPage );
    runActionRow->addWidget( m_runPageLabel, 1 );
    runActionRow->addWidget( m_runNextPage );
    runActionRow->addWidget( m_compareBtn );
    experimentLayout->addLayout( runActionRow );

    m_runDetail = new QPlainTextEdit( experimentTab );
    m_runDetail->setReadOnly( true );
    m_runDetail->setMaximumHeight( 160 );
    experimentLayout->addWidget( m_runDetail );

    m_tabs->addTab( experimentTab, tr( "Experiment" ) );

    // QgsDockWidget owns an internal layout; a second QVBoxLayout(this) only
    // produced "already has a layout" warnings and fought setWidget() over
    // m_tabs' parent (#1821). setWidget() is the single ownership seam.
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
    connect( m_datasetPrevPage, &QPushButton::clicked, this, [this] {
        m_datasetOffset = std::max<qint64>( 0, m_datasetOffset - sicnu::dataset::DatasetStore::kMaxPageSize );
        rebuildDatasets();
    } );
    connect( m_datasetNextPage, &QPushButton::clicked, this, [this] {
        m_datasetOffset += sicnu::dataset::DatasetStore::kMaxPageSize;
        rebuildDatasets();
    } );
    connect( m_samplePrevPage, &QPushButton::clicked, this, [this] {
        m_sampleOffset = std::max<qint64>( 0, m_sampleOffset - kSamplePageSize );
        onVersionSelected( m_versionList->currentRow() );
    } );
    connect( m_sampleNextPage, &QPushButton::clicked, this, [this] {
        m_sampleOffset += kSamplePageSize;
        onVersionSelected( m_versionList->currentRow() );
    } );
    connect( m_experimentPrevPage, &QPushButton::clicked, this, [this] {
        m_experimentOffset = std::max<qint64>( 0, m_experimentOffset - sicnu::experiment::ExperimentStore::kMaxPageSize );
        rebuildExperiments();
    } );
    connect( m_experimentNextPage, &QPushButton::clicked, this, [this] {
        m_experimentOffset += sicnu::experiment::ExperimentStore::kMaxPageSize;
        rebuildExperiments();
    } );
    connect( m_runPrevPage, &QPushButton::clicked, this, [this] {
        m_runOffset = std::max<qint64>( 0, m_runOffset - sicnu::experiment::ExperimentStore::kMaxPageSize );
        rebuildRuns();
    } );
    connect( m_runNextPage, &QPushButton::clicked, this, [this] {
        m_runOffset += sicnu::experiment::ExperimentStore::kMaxPageSize;
        rebuildRuns();
    } );

    m_compareBtn->setEnabled( false );
    for ( QPushButton *btn : { m_datasetPrevPage, m_datasetNextPage, m_samplePrevPage,
                               m_sampleNextPage, m_experimentPrevPage, m_experimentNextPage,
                               m_runPrevPage, m_runNextPage } )
        btn->setEnabled( false );
}

void DatasetExperimentPanel::openDatasetStore()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr( "Open Dataset Library" ), QString(), tr( "SQLite Databases (*.db *.sqlite);;All Files (*)" ) );
    if ( !path.isEmpty() )
        openDatasetStoreAtPath( path );
}

bool DatasetExperimentPanel::openDatasetStoreAtPath( const QString &path )
{
    if ( !QFileInfo::exists( path ) )
    {
        m_datasetDbLabel->setText( tr( "File not found: %1" ).arg( path ) );
        resetDatasetViews();
        return false;
    }
    QString error;
    if ( !m_datasetStore.open( path, &error ) )
    {
        m_datasetDbLabel->setText( tr( "Open failed: %1" ).arg( error ) );
        resetDatasetViews();
        return false;
    }
    m_datasetDbLabel->setText( path );
    m_datasetOffset = 0;
    m_sampleOffset = 0;
    rebuildDatasets();
    return true;
}

void DatasetExperimentPanel::openExperimentStore()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr( "Open Experiment Library" ), QString(), tr( "SQLite Databases (*.db *.sqlite);;All Files (*)" ) );
    if ( !path.isEmpty() )
        openExperimentStoreAtPath( path );
}

bool DatasetExperimentPanel::openExperimentStoreAtPath( const QString &path )
{
    if ( !QFileInfo::exists( path ) )
    {
        m_experimentDbLabel->setText( tr( "File not found: %1" ).arg( path ) );
        resetExperimentViews();
        return false;
    }
    QString error;
    if ( !m_experimentStore.open( path, &error ) )
    {
        m_experimentDbLabel->setText( tr( "Open failed: %1" ).arg( error ) );
        resetExperimentViews();
        return false;
    }
    m_experimentStorePath = path;
    m_experimentDbLabel->setText( path );
    m_experimentOffset = 0;
    m_runOffset = 0;
    rebuildExperiments();
    return true;
}

void DatasetExperimentPanel::resetDatasetViews()
{
    // A failed (re)open must not leave the previous store's rows, pager
    // buttons and page labels on screen under an error message.
    m_datasetOffset = 0;
    m_sampleOffset = 0;
    m_datasetTotal = 0;
    m_sampleTotal = 0;
    m_datasets.clear();
    m_datasetCombo->clear();
    m_datasetPageLabel->clear();
    m_datasetPrevPage->setEnabled( false );
    m_datasetNextPage->setEnabled( false );
    rebuildVersions();
}

void DatasetExperimentPanel::resetExperimentViews()
{
    m_experimentOffset = 0;
    m_runOffset = 0;
    m_experimentTotal = 0;
    m_runTotal = 0;
    m_experiments.clear();
    m_experimentCombo->clear();
    m_experimentPageLabel->clear();
    m_experimentPrevPage->setEnabled( false );
    m_experimentNextPage->setEnabled( false );
    m_runs.clear();
    m_runsTable->setRowCount( 0 );
    m_runDetail->clear();
    m_runPageLabel->clear();
    m_runPrevPage->setEnabled( false );
    m_runNextPage->setEnabled( false );
}

void DatasetExperimentPanel::rebuildDatasets()
{
    QSignalBlocker blocker( m_datasetCombo );
    m_datasetCombo->clear();
    m_datasets.clear();
    if ( !m_datasetStore.isOpen() )
        return;
    const auto page = m_datasetStore.listDatasets( m_datasetOffset, sicnu::dataset::DatasetStore::kMaxPageSize );
    if ( !page.has_value() )
    {
        m_datasetDbLabel->setText(
            tr( "Read failed: %1" )
                .arg( page.diagnostics().isEmpty() ? tr( "Unknown error" )
                                                   : page.diagnostics().first().message ) );
        return;
    }
    if ( page->second.isEmpty() && m_datasetOffset > 0 )
    {
        m_datasetOffset = std::max<qint64>( 0, m_datasetOffset - sicnu::dataset::DatasetStore::kMaxPageSize );
        rebuildDatasets();
        return;
    }
    m_datasetTotal = page->first;
    m_datasets = page->second;
    for ( const QVariantMap &row : m_datasets )
        m_datasetCombo->addItem(
            tr( "%1 (%2)" ).arg( row.value( "name" ).toString(),
                                      row.value( "id" ).toString() ),
            row.value( "id" ).toString() );
    const qint64 pageCount = std::max<qint64>( 1, ( m_datasetTotal + sicnu::dataset::DatasetStore::kMaxPageSize - 1 )
                                                   / sicnu::dataset::DatasetStore::kMaxPageSize );
    const qint64 currentPage = m_datasetTotal == 0 ? 1 : m_datasetOffset / sicnu::dataset::DatasetStore::kMaxPageSize + 1;
    m_datasetPageLabel->setText( tr( "Page %1 / %2" ).arg( currentPage ).arg( pageCount ) );
    m_datasetPrevPage->setEnabled( m_datasetOffset > 0 );
    m_datasetNextPage->setEnabled( m_datasetOffset + m_datasets.size() < m_datasetTotal );
    m_datasetDbLabel->setText( tr( "%1 — %2 datasets in total" )
                                   .arg( m_datasetStore.storePath() )
                                   .arg( m_datasetTotal ) );
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
    m_sampleOffset = 0;
    m_sampleVersionId.clear();
    m_samplePreview->clear();
    m_samplePageLabel->clear();
    m_samplePrevPage->setEnabled( false );
    m_sampleNextPage->setEnabled( false );
    m_versionDetail->clear();
    const QString datasetId = m_datasetCombo->currentData().toString();
    if ( datasetId.isEmpty() )
        return;
    const auto id = sicnu::dataset::DatasetId::fromString( datasetId );
    if ( !id )
        return;
    m_versions = m_datasetStore.versionsOfDataset( *id );
    for ( const sicnu::dataset::DatasetVersionRecord &version : m_versions )
        m_versionList->addItem(
            tr( "%1 — %2" ).arg( version.versionId(), statusText( version.status() ) ) );
    if ( m_versions.isEmpty() )
        m_versionDetail->setText( tr( "This dataset has no versions yet." ) );
}

void DatasetExperimentPanel::onDatasetSelected( int index )
{
    Q_UNUSED( index );
    rebuildVersions();
    emit datasetSelectionChanged( m_datasetCombo->currentData().toString() );
}

void DatasetExperimentPanel::onVersionSelected( int row )
{
    if ( row < 0 || row >= m_versions.size() )
        return;
    const sicnu::dataset::DatasetVersionRecord &version = m_versions[row];
    const auto versionId = sicnu::dataset::DatasetVersionId::fromString( version.versionId() );
    if ( !versionId )
        return;

    // Selecting a different version restarts its sample pager — the previous
    // version's offset can point past this version's sample count.
    if ( version.versionId() != m_sampleVersionId )
    {
        m_sampleVersionId = version.versionId();
        m_sampleOffset = 0;
    }

    // Bounded, truthful facts only: counts + manifest refs, no derived stats.
    m_sampleTotal = m_datasetStore.sampleCount( *versionId );
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
    lines << tr( "Samples: %1" ).arg( m_sampleTotal );

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

    m_samplePreview->clear();
    const auto samples = m_datasetStore.samplesPage( *versionId, m_sampleOffset, kSamplePageSize );
    const qint64 pageCount = std::max<qint64>( 1, ( m_sampleTotal + kSamplePageSize - 1 ) / kSamplePageSize );
    m_samplePageLabel->setText( tr( "Page %1 / %2" )
                                    .arg( m_sampleTotal == 0 ? 1 : m_sampleOffset / kSamplePageSize + 1 )
                                    .arg( pageCount ) );
    m_samplePrevPage->setEnabled( m_sampleOffset > 0 );
    m_sampleNextPage->setEnabled( samples.has_value()
                                  && m_sampleOffset + samples->second.size() < m_sampleTotal );
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
    const auto page = m_experimentStore.listExperiments( m_experimentOffset,
                                                         sicnu::experiment::ExperimentStore::kMaxPageSize );
    if ( !page.has_value() )
    {
        m_experimentDbLabel->setText(
            tr( "Read failed: %1" )
                .arg( page.diagnostics().isEmpty() ? tr( "Unknown error" )
                                                   : page.diagnostics().first().message ) );
        return;
    }
    if ( page->second.isEmpty() && m_experimentOffset > 0 )
    {
        m_experimentOffset = std::max<qint64>( 0, m_experimentOffset - sicnu::experiment::ExperimentStore::kMaxPageSize );
        rebuildExperiments();
        return;
    }
    m_experimentTotal = page->first;
    m_experiments = page->second;
    for ( const sicnu::experiment::Experiment &experiment : m_experiments )
        m_experimentCombo->addItem(
            tr( "%1 (%2)" ).arg( experiment.name(), experiment.experimentId() ),
            experiment.experimentId() );
    const qint64 pageCount = std::max<qint64>( 1, ( m_experimentTotal + sicnu::experiment::ExperimentStore::kMaxPageSize - 1 )
                                                   / sicnu::experiment::ExperimentStore::kMaxPageSize );
    m_experimentPageLabel->setText( tr( "Page %1 / %2" )
                                        .arg( m_experimentTotal == 0 ? 1 : m_experimentOffset / sicnu::experiment::ExperimentStore::kMaxPageSize + 1 )
                                        .arg( pageCount ) );
    m_experimentPrevPage->setEnabled( m_experimentOffset > 0 );
    m_experimentNextPage->setEnabled( m_experimentOffset + m_experiments.size() < m_experimentTotal );
    m_experimentDbLabel->setText( tr( "%1 — %2 experiments in total" )
                                      .arg( m_experimentStorePath )
                                      .arg( m_experimentTotal ) );
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
    {
        m_runPageLabel->clear();
        m_runPrevPage->setEnabled( false );
        m_runNextPage->setEnabled( false );
        return;
    }
    const auto page = m_experimentStore.listRuns( experimentId, QString(), QString(), m_runOffset,
                                                  sicnu::experiment::ExperimentStore::kMaxPageSize );
    if ( !page.has_value() )
        return;
    if ( page->second.isEmpty() && m_runOffset > 0 )
    {
        m_runOffset = std::max<qint64>( 0, m_runOffset - sicnu::experiment::ExperimentStore::kMaxPageSize );
        rebuildRuns();
        return;
    }
    m_runTotal = page->first;
    m_runs = page->second;
    m_runsTable->setRowCount( m_runs.size() );
    const qint64 pageCount = std::max<qint64>( 1, ( m_runTotal + sicnu::experiment::ExperimentStore::kMaxPageSize - 1 )
                                                   / sicnu::experiment::ExperimentStore::kMaxPageSize );
    m_runPageLabel->setText( tr( "Page %1 / %2" )
                                 .arg( m_runTotal == 0 ? 1 : m_runOffset / sicnu::experiment::ExperimentStore::kMaxPageSize + 1 )
                                 .arg( pageCount ) );
    m_runPrevPage->setEnabled( m_runOffset > 0 );
    m_runNextPage->setEnabled( m_runOffset + m_runs.size() < m_runTotal );
    m_runDetail->setPlaceholderText( tr( "Run metrics (%1 runs in total)." ).arg( m_runTotal ) );
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
    m_runOffset = 0;
    rebuildRuns();
}

void DatasetExperimentPanel::onRunsSelectionChanged()
{
    const QModelIndexList rows =
        m_runsTable->selectionModel() ? m_runsTable->selectionModel()->selectedRows()
                                      : QModelIndexList();
    m_compareBtn->setEnabled( rows.size() == 2 );
    QStringList runIds;
    runIds.reserve( rows.size() );
    for ( const QModelIndex &row : rows )
    {
        if ( const QTableWidgetItem *idItem = m_runsTable->item( row.row(), 0 ) )
            runIds.append( idItem->text() );
    }
    emit experimentRunSelectionChanged( runIds );
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
