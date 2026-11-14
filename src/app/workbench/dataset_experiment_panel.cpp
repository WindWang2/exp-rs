/***************************************************************************
 * dataset_experiment_panel.cpp — see dataset_experiment_panel.h
 ***************************************************************************/
#include "dataset_experiment_panel.h"

#include "dataset/dataset_manifest.h"
#include "dataset/label_schema.h"
#include "experiment/comparison_ext.h"
#include "experiment/experiment_types.h"

#include <QComboBox>
#include <QJsonValue>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QLabel>
#include "dataset/sample.h"
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
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

    // R30-T07 WP06: explicit, stable comparison pins that survive paging,
    // searching and rebuilds. A pin is a RUN ID, never a row index.
    auto *pinRow = new QHBoxLayout;
    m_pinABtn = new QPushButton( tr( "Pin A" ), experimentTab );
    m_pinABtn->setObjectName( QStringLiteral( "rsExperimentPinA" ) );
    m_pinBBtn = new QPushButton( tr( "Pin B" ), experimentTab );
    m_pinBBtn->setObjectName( QStringLiteral( "rsExperimentPinB" ) );
    m_clearPinsBtn = new QPushButton( tr( "Clear Pins" ), experimentTab );
    m_clearPinsBtn->setObjectName( QStringLiteral( "rsExperimentClearPins" ) );
    m_pinLabel = new QLabel( tr( "pins: (none)" ), experimentTab );
    m_pinLabel->setObjectName( QStringLiteral( "rsExperimentPinLabel" ) );
    pinRow->addWidget( m_pinABtn );
    pinRow->addWidget( m_pinBBtn );
    pinRow->addWidget( m_clearPinsBtn );
    pinRow->addWidget( m_pinLabel, 1 );
    experimentLayout->addLayout( pinRow );

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
    connect( m_pinABtn, &QPushButton::clicked, this, &DatasetExperimentPanel::pinRunA );
    connect( m_pinBBtn, &QPushButton::clicked, this, &DatasetExperimentPanel::pinRunB );
    connect( m_clearPinsBtn, &QPushButton::clicked, this,
             &DatasetExperimentPanel::clearComparisonPins );
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
    // Identity BEFORE overwriting the path. Re-opening the SAME store is a
    // refresh/pager rebuild: R29's run-id selection memory must survive it.
    // Only a genuinely different store invalidates pins and cross-page ids.
    const auto canonical = []( const QString &p ) {
        return p.isEmpty() ? QString() : QFileInfo( p ).canonicalFilePath();
    };
    const bool sameStore = !m_experimentStorePath.isEmpty()
                           && canonical( m_experimentStorePath ) == canonical( path );
    m_experimentStorePath = path;
    m_experimentDbLabel->setText( path );
    m_experimentOffset = 0;
    m_runOffset = 0;
    if ( !sameStore )
    {
        // R30-T07 WP06: a different store must not inherit the previous
        // store's pins — run ids may repeat with different evidence.
        m_pinnedRunA.clear();
        m_pinnedRunB.clear();
        m_selectedRunIds.clear();
        m_lastEmittedRunSelection.clear();
    }
    refreshPinLabel();
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
    m_pinnedRunA.clear();
    m_pinnedRunB.clear();
    refreshPinLabel();
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
    // D3 (R29-T04): the WHOLE rebuild (clear + repopulate + restore below)
    // runs with selection signals blocked — the transient empty selection
    // while rows are gone must NOT overwrite the id memory the restore
    // reads, and the shell hears ONE consolidated signal with the resolved
    // set after the table settles.
    QItemSelectionModel *const blockedSelectionModel = m_runsTable->selectionModel();
    QSignalBlocker blockedSelection(
      blockedSelectionModel ? static_cast<QObject *>( blockedSelectionModel )
                            : static_cast<QObject *>( m_runsTable ) );
    m_runsTable->setRowCount( 0 );
    m_runs.clear();
    const QString experimentId = m_experimentCombo->currentData().toString();
    if ( experimentId.isEmpty() )
    {
        m_runPageLabel->clear();
        m_runPrevPage->setEnabled( false );
        m_runNextPage->setEnabled( false );
        settleRunsSelection();
        return;
    }
    const auto page = m_experimentStore.listRuns( experimentId, QString(), QString(), m_runOffset,
                                                  sicnu::experiment::ExperimentStore::kMaxPageSize );
    if ( !page.has_value() )
    {
        // Read failure: the rows are already cleared — the selection must
        // resolve (and report) empty, not keep stale enablement.
        settleRunsSelection();
        return;
    }
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
    // D3 (R29-T04): the table rebuild dropped the widget selection;
    // re-select whichever remembered run ids are still on this page. Ids
    // that vanished (deleted record, different experiment) resolve to
    // nothing — the memory is dropped honestly, never re-aimed at a row.
    if ( !m_selectedRunIds.isEmpty() && m_runsTable->selectionModel() )
    {
        QItemSelectionModel *selection = m_runsTable->selectionModel();
        for ( int i = 0; i < m_runs.size(); ++i )
        {
            if ( !m_selectedRunIds.contains( m_runs[i].runId() ) )
                continue;
            selection->select( m_runsTable->model()->index( i, 0 ),
                               QItemSelectionModel::Select | QItemSelectionModel::Rows );
        }
    }
    settleRunsSelection();
}

void DatasetExperimentPanel::settleRunsSelection()
{
    // The resolvable selection (memory ∩ visible rows) drives the compare
    // affordance and ONE shell signal; the MEMORY itself is not trimmed —
    // an off-page selected run re-selects when its page returns, and an id
    // whose record vanished simply resolves to nothing.
    QStringList resolved;
    if ( m_runsTable->selectionModel() )
    {
        const QModelIndexList rows = m_runsTable->selectionModel()->selectedRows();
        for ( const QModelIndex &row : rows )
            if ( const QTableWidgetItem *idItem = m_runsTable->item( row.row(), 0 ) )
                resolved.append( idItem->text() );
    }
    // Explicit pins also arm compare, even when both pinned runs are on
    // other pages (resolve by run id, never by row number).
    invalidateMissingPins();
    const bool pinsComplete = !m_pinnedRunA.isEmpty() && !m_pinnedRunB.isEmpty();
    m_compareBtn->setEnabled( resolved.size() == 2 || pinsComplete );
    if ( m_pinLabel )
        refreshPinLabel();
    if ( resolved != m_lastEmittedRunSelection )
    {
        m_lastEmittedRunSelection = resolved;
        emit experimentRunSelectionChanged( resolved );
    }
}

void DatasetExperimentPanel::onExperimentSelected( int index )
{
    Q_UNUSED( index );
    m_runOffset = 0;
    // Switching experiments changes the input set; pins from the previous
    // experiment must not silently compare unrelated runs.
    m_pinnedRunA.clear();
    m_pinnedRunB.clear();
    refreshPinLabel();
    rebuildRuns();
}

void DatasetExperimentPanel::onRunsSelectionChanged()
{
    const QModelIndexList rows =
        m_runsTable->selectionModel() ? m_runsTable->selectionModel()->selectedRows()
                                      : QModelIndexList();
    invalidateMissingPins();
    const bool pinsComplete = !m_pinnedRunA.isEmpty() && !m_pinnedRunB.isEmpty();
    m_compareBtn->setEnabled( rows.size() == 2 || pinsComplete );
    QStringList runIds;
    runIds.reserve( rows.size() );
    for ( const QModelIndex &row : rows )
    {
        if ( const QTableWidgetItem *idItem = m_runsTable->item( row.row(), 0 ) )
            runIds.append( idItem->text() );
    }
    // The stable selection memory follows every REAL user selection (rebuild
    // restores never reach this handler — they settle separately), and the
    // emitted baseline follows with it so the next settle compares against
    // what the shell actually heard.
    m_selectedRunIds = runIds;
    m_lastEmittedRunSelection = runIds;
    emit experimentRunSelectionChanged( runIds );
    if ( rows.size() == 1 )
    {
        if ( const QTableWidgetItem *idItem = m_runsTable->item( rows.first().row(), 0 ) )
            showMetricJson( idItem->text() );
    }
}

void DatasetExperimentPanel::refreshPinLabel()
{
    if ( !m_pinLabel )
        return;
    const auto text = []( const QString &id ) {
        return id.isEmpty() ? QStringLiteral( "(none)" ) : id;
    };
    m_pinLabel->setText( tr( "pins: A=%1 B=%2" ).arg( text( m_pinnedRunA ),
                                                      text( m_pinnedRunB ) ) );
}

void DatasetExperimentPanel::clearComparisonPins()
{
    m_pinnedRunA.clear();
    m_pinnedRunB.clear();
    refreshPinLabel();
    settleRunsSelection();
}

void DatasetExperimentPanel::invalidateMissingPins()
{
    // A deleted record, an input revision, or a store switch invalidates a
    // pin; silently re-aiming it at another row would compare the wrong run.
    const auto valid = [this]( const QString &id ) {
        return !id.isEmpty() && m_experimentStore.runById( id ).has_value();
    };
    bool changed = false;
    if ( !m_pinnedRunA.isEmpty() && !valid( m_pinnedRunA ) )
    {
        m_pinnedRunA.clear();
        changed = true;
    }
    if ( !m_pinnedRunB.isEmpty() && !valid( m_pinnedRunB ) )
    {
        m_pinnedRunB.clear();
        changed = true;
    }
    if ( changed )
        refreshPinLabel();
}

void DatasetExperimentPanel::pinRunA()
{
    const QModelIndexList rows = m_runsTable->selectionModel()
                                     ? m_runsTable->selectionModel()->selectedRows()
                                     : QModelIndexList();
    if ( rows.size() != 1 )
    {
        m_runDetail->setPlainText( tr( "Select exactly one run to pin as A." ) );
        return;
    }
    const QTableWidgetItem *item = m_runsTable->item( rows.first().row(), 0 );
    if ( !item )
        return;
    m_pinnedRunA = item->text();
    if ( m_pinnedRunB == m_pinnedRunA )
        m_pinnedRunB.clear();
    refreshPinLabel();
    settleRunsSelection();
}

void DatasetExperimentPanel::pinRunB()
{
    const QModelIndexList rows = m_runsTable->selectionModel()
                                     ? m_runsTable->selectionModel()->selectedRows()
                                     : QModelIndexList();
    if ( rows.size() != 1 )
    {
        m_runDetail->setPlainText( tr( "Select exactly one run to pin as B." ) );
        return;
    }
    const QTableWidgetItem *item = m_runsTable->item( rows.first().row(), 0 );
    if ( !item )
        return;
    m_pinnedRunB = item->text();
    if ( m_pinnedRunA == m_pinnedRunB )
        m_pinnedRunA.clear();
    refreshPinLabel();
    settleRunsSelection();
}

QJsonObject DatasetExperimentPanel::labelSchemaEvidenceForRun(
    const QString &runId, std::optional<sicnu::dataset::LabelSchema> *schemaOut ) const
{
    QJsonObject evidence;
    evidence.insert( QStringLiteral( "run_id" ), runId );
    const auto refuse = [&evidence]( const QString &code, const QString &reason ) {
        evidence.insert( QStringLiteral( "checked" ), false );
        evidence.insert( QStringLiteral( "reason_code" ), code );
        evidence.insert( QStringLiteral( "reason" ), reason );
    };
    if ( !m_datasetStore.isOpen() )
    {
        refuse( QStringLiteral( "experiment.dataset_store_not_open" ),
                tr( "no dataset store is open; label schema evidence is unavailable" ) );
        return evidence;
    }
    const auto run = m_experimentStore.runById( runId );
    if ( !run.has_value() )
    {
        refuse( QStringLiteral( "experiment.run_missing" ),
                tr( "the run record is missing, so no dataset version can be resolved" ) );
        return evidence;
    }
    const auto versionId = sicnu::dataset::DatasetVersionId::fromString(
        run->datasetVersionId() );
    if ( !versionId.has_value() || versionId->isNull() )
    {
        refuse( QStringLiteral( "experiment.dataset_version_missing" ),
                tr( "the run has no resolvable dataset version id" ) );
        return evidence;
    }
    const auto version = m_datasetStore.versionById( *versionId );
    if ( !version.has_value() )
    {
        refuse( QStringLiteral( "experiment.dataset_version_missing" ),
                tr( "the referenced dataset version is not in the open dataset store" ) );
        return evidence;
    }
    QJsonParseError parseError;
    const QJsonDocument manifestDoc =
        QJsonDocument::fromJson( version->manifestJson().toUtf8(), &parseError );
    if ( parseError.error != QJsonParseError::NoError || !manifestDoc.isObject() )
    {
        refuse( QStringLiteral( "experiment.manifest_unreadable" ),
                tr( "the dataset version manifest is not readable JSON" ) );
        return evidence;
    }
    const auto manifest = sicnu::dataset::DatasetManifest::fromJson( manifestDoc.object() );
    if ( !manifest )
    {
        refuse( QStringLiteral( "experiment.manifest_unreadable" ),
                tr( "the dataset version manifest failed its typed reader" ) );
        return evidence;
    }
    const sicnu::dataset::LabelSchemaRef ref = manifest.value().labelSchema();
    if ( ref.isNull() )
    {
        refuse( QStringLiteral( "experiment.label_schema_not_declared" ),
                tr( "this dataset version declares no label schema reference" ) );
        return evidence;
    }
    const auto schema = m_datasetStore.labelSchema( ref.schemaId, ref.version );
    if ( !schema.has_value() )
    {
        refuse( QStringLiteral( "experiment.label_schema_missing" ),
                tr( "the declared label schema/version is not in the open dataset store" ) );
        return evidence;
    }
    QStringList codes;
    for ( const auto &klass : schema->classes() )
        codes.append( klass.code() );
    evidence.insert( QStringLiteral( "checked" ), true );
    evidence.insert( QStringLiteral( "schema_id" ), schema->schemaId() );
    evidence.insert( QStringLiteral( "version" ), static_cast<double>( schema->version() ) );
    evidence.insert( QStringLiteral( "class_codes" ), QJsonArray::fromStringList( codes ) );
    evidence.insert( QStringLiteral( "dataset_version_id" ), run->datasetVersionId() );
    evidence.insert( QStringLiteral( "dataset_fingerprint" ), run->datasetFingerprint() );
    if ( schemaOut )
        *schemaOut = *schema;
    return evidence;
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
    // R30-T07 WP06: EXPLICIT pins take precedence and resolve by RUN ID, so
    // a pair selected on two different pages can be compared. Without pins
    // the two currently visible rows remain the input (R29 behavior).
    invalidateMissingPins();
    QString runA = m_pinnedRunA;
    QString runB = m_pinnedRunB;
    if ( runA.isEmpty() || runB.isEmpty() )
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
        runA = itemA->text();
        runB = itemB->text();
    }
    if ( runA == runB )
    {
        m_runDetail->setPlainText( tr( "A run cannot be compared with itself." ) );
        return;
    }
    if ( !m_experimentStore.runById( runA ).has_value()
         || !m_experimentStore.runById( runB ).has_value() )
    {
        m_runDetail->setPlainText(
          tr( "A pinned run no longer resolves by id in this store; re-pin it." ) );
        return;
    }
    const auto metricA = m_experimentStore.metricRecordForRun( runA );
    const auto metricB = m_experimentStore.metricRecordForRun( runB );
    if ( !metricA.has_value() || !metricB.has_value() )
    {
        m_runDetail->setPlainText( tr( "Both runs need recorded metrics to compare (metrics from one side only are shown otherwise)." ) );
        return;
    }

    // D3 (R29-T04): comparability is decided by the experiment layer's
    // authority (comparison_ext: protocol + schema + paired deltas with
    // support), not by a UI-side reimplementation. The report keeps the
    // run-record identity pins (dataset version/fingerprint) as context;
    // the VERDICT comes from compareProtocols/pairedRunComparison. Missing
    // support stays "insufficient_support" — the UI never turns a missing
    // metric into a zero or invents a winner.
    const auto runRecordA = m_experimentStore.runById( runA );
    const auto runRecordB = m_experimentStore.runById( runB );
    QJsonObject report;
    report.insert( QStringLiteral( "run_a" ), runA );
    report.insert( QStringLiteral( "run_b" ), runB );
    if ( runRecordA.has_value() && runRecordB.has_value() )
    {
        report.insert( QStringLiteral( "dataset_version_a" ), runRecordA->datasetVersionId() );
        report.insert( QStringLiteral( "dataset_version_b" ), runRecordB->datasetVersionId() );
        report.insert( QStringLiteral( "dataset_fingerprint_a" ), runRecordA->datasetFingerprint() );
        report.insert( QStringLiteral( "dataset_fingerprint_b" ), runRecordB->datasetFingerprint() );
    }
    // R30-T07 WP06: resolve the REAL label schemas from the recorded run →
    // dataset version → canonical manifest → label_schema ref, then feed
    // compareLabelSchemas into pairedRunComparison. Passing nullptr used to
    // make the backend DEFAULT schemas_compatible=true with no evidence.
    std::optional<sicnu::dataset::LabelSchema> schemaA;
    std::optional<sicnu::dataset::LabelSchema> schemaB;
    const QJsonObject evidenceA = labelSchemaEvidenceForRun( runA, &schemaA );
    const QJsonObject evidenceB = labelSchemaEvidenceForRun( runB, &schemaB );
    const bool schemaChecked = evidenceA.value( QStringLiteral( "checked" ) ).toBool()
                                && evidenceB.value( QStringLiteral( "checked" ) ).toBool()
                                && schemaA.has_value() && schemaB.has_value();
    std::optional<sicnu::experiment::SchemaCompatibility> schemaCompat;
    QJsonObject schemaEvidence;
    schemaEvidence.insert( QStringLiteral( "checked" ), schemaChecked );
    schemaEvidence.insert( QStringLiteral( "run_a" ), evidenceA );
    schemaEvidence.insert( QStringLiteral( "run_b" ), evidenceB );
    if ( schemaChecked )
    {
        schemaCompat = sicnu::experiment::compareLabelSchemas( *schemaA, *schemaB );
        schemaEvidence.insert( QStringLiteral( "verdict" ),
                               schemaCompat->toJson().value( QStringLiteral( "verdict" ) ) );
        schemaEvidence.insert( QStringLiteral( "compatibility" ), schemaCompat->toJson() );
    }
    else
    {
        schemaEvidence.insert( QStringLiteral( "verdict" ),
                               QStringLiteral( "not_checked_missing_evidence" ) );
        QStringList reasons;
        for ( const auto &e : { evidenceA, evidenceB } )
        {
            const QString code = e.value( QStringLiteral( "reason_code" ) ).toString();
            if ( !code.isEmpty() )
                reasons.append( code );
        }
        reasons.removeDuplicates();
        schemaEvidence.insert( QStringLiteral( "reasons" ),
                               QJsonArray::fromStringList( reasons ) );
        schemaEvidence.insert( QStringLiteral( "unverified_note" ),
                               tr( "Label schema evidence is missing: schema compatibility "
                                   "was NOT verified. Do not cite this pair as comparable." ) );
    }
    report.insert( QStringLiteral( "schema_evidence" ), schemaEvidence );

    const sicnu::experiment::ProtocolCompatibility protocolCompat =
        sicnu::experiment::compareProtocols( metricA->protocol, metricB->protocol );
    const sicnu::experiment::PairedRunSummary paired =
        sicnu::experiment::pairedRunComparison( metricA.value(), metricB.value(),
                                                schemaChecked ? &*schemaCompat : nullptr );
    report.insert( QStringLiteral( "protocol_compatibility" ), protocolCompat.toJson() );
    QJsonObject pairedJson = paired.toJson();
    if ( !schemaChecked )
    {
        // Legacy `comparable` field is FROZEN for existing consumers, but
        // the unverified default must not be presented as "already checked".
        pairedJson.insert( QStringLiteral( "schemas_compatible" ), QJsonValue::Null );
        pairedJson.insert( QStringLiteral( "schemas_compatible_unverified" ), true );
    }
    report.insert( QStringLiteral( "paired_summary" ), pairedJson );
    report.insert( QStringLiteral( "comparable" ),
                   protocolCompat.compatible && paired.schemasCompatible );
    // R30-T07 R1 (P2-3): `comparable` is a frozen legacy field and its
    // pairedRunComparison default can still be true with no schema evidence.
    // Name that state explicitly so a report reader never mistakes it for a
    // verified comparison. The authoritative gate is the next field.
    report.insert( QStringLiteral( "comparable_unverified" ),
                   !schemaChecked && protocolCompat.compatible
                       && paired.schemasCompatible );
    // New authoritative gate: protocol equality AND a real, comparable label
    // schema verdict. Nothing can read "comparable": true as schema-verified.
    report.insert( QStringLiteral( "comparable_with_schema_evidence" ),
                   protocolCompat.compatible && schemaChecked
                       && schemaCompat->verdict
                              != sicnu::experiment::SchemaCompatibility::Verdict::NotComparable
                       && paired.schemasCompatible );
    if ( !schemaChecked )
    {
        report.insert( QStringLiteral( "comparability_note" ),
                       tr( "Label schemas were NOT checked (missing evidence). Protocol-only "
                           "comparability is provisional, not verified evidence." ) );
    }
    if ( !protocolCompat.compatible )
        report.insert( QStringLiteral( "comparability_note" ),
                       tr( "Protocols differ (split/threshold/aggregation/label support): "
                           "deltas are shown as data, not as comparable evidence." ) );

    // Metric presence asymmetry stays visible: a metric recorded on one
    // side only is listed, never differenced.
    const QJsonObject metricsA = metricA->metrics;
    const QJsonObject metricsB = metricB->metrics;
    QStringList onlyA;
    QStringList onlyB;
    for ( auto it = metricsA.begin(); it != metricsA.end(); ++it )
        if ( !metricsB.contains( it.key() ) )
            onlyA << it.key();
    for ( auto it = metricsB.begin(); it != metricsB.end(); ++it )
        if ( !metricsA.contains( it.key() ) )
            onlyB << it.key();
    report.insert( QStringLiteral( "only_in_a" ), QJsonArray::fromStringList( onlyA ) );
    report.insert( QStringLiteral( "only_in_b" ), QJsonArray::fromStringList( onlyB ) );

    m_runDetail->setPlainText(
        QString::fromUtf8( QJsonDocument( report ).toJson( QJsonDocument::Indented ) ) );
}

} // namespace sicnu::app
