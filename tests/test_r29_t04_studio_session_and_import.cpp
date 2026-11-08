// test_r29_t04_studio_session_and_import.cpp — D4 (R29-T04).
//
// Two persistence loops of the Experiment Studio:
//   1. Session save/restore: a validated design + its recorded report and
//      tab survive close/reopen through StudioSessionState; a damaged or
//      foreign-version document is refused with an explicit note, never
//      partially adopted.
//   2. Offline bundle import through the dock: dry-run precheck reports
//      what the ENGINE reports (conflicts/warnings, would-be run id),
//      writes nothing; the real import lands the run as Created evidence
//      and a re-import is idempotent.
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTabWidget>

#include "app/experiment_studio/experiment_studio_dock.h"
#include "dataset/dataset_ids.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "experiment/experiment_store.h"
#include "experiment/reproduction_bundle.h"
#include "experiment/reproduction_bundle_import.h"

#include <gdal.h>
#include <filesystem>

using sicnu::app::ExperimentStudioDock;
using namespace sicnu::experiment;

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char appName[] = "test_r29_t04_studio_session_import";
    static char *argv[] = { appName, nullptr };
    QStandardPaths::setTestModeEnabled( true );
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
}

/// Fresh AppData session dir per case (the studio session file lives under
/// AppDataLocation, which setTestModeEnabled redirects into the test dir).
void resetSessionDir( const char *tag )
{
    QCoreApplication::setApplicationName( QString::fromUtf8( tag ) );
    const QString root = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    std::filesystem::remove_all( root.toStdString() );
}

QString writeRealInputRaster( const QTemporaryDir &dir )
{
    const QString path = dir.filePath( "input.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return QString();
    GDALDatasetH ds =
      GDALCreate( driver, path.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    if ( !ds )
        return QString();
    double geoTransform[6] = { 500000.0, 1.0, 0.0, 4000000.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, geoTransform );
    GDALSetProjection( ds, "EPSG:32648" );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    const float row[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    for ( int y = 0; y < 8; ++y )
        GDALRasterIO( band, GF_Write, 0, y, 8, 1, const_cast<float *>( row ), 8, 1,
                      GDT_Float32, 0, 0 );
    GDALClose( ds );
    return QFile::exists( path ) ? path : QString();
}

QLabel *designerSummaryOf( ExperimentStudioDock *dock )
{
    return dock->findChild<QLabel *>( QStringLiteral( "rsStudioDesignerSummary" ) );
}

QLineEdit *inputRasterEditOf( ExperimentStudioDock *dock )
{
    for ( QLineEdit *edit : dock->findChildren<QLineEdit *>() )
        if ( edit->placeholderText().contains( QStringLiteral( "Input raster" ) ) )
            return edit;
    return nullptr;
}

QPushButton *validateButtonOf( ExperimentStudioDock *dock )
{
    for ( QPushButton *button : dock->findChildren<QPushButton *>() )
        if ( button->text() == QLatin1String( "Validate Study Design" ) )
            return button;
    return nullptr;
}

/// A committed dataset version + a Completed run, exported as a real
/// reproduction bundle. Returns (bundleDir, runId, experimentId).
struct BundleFixture
{
    QTemporaryDir dir;
    sicnu::dataset::DatasetStore datasets;
    ExperimentStore experiments;
    QString bundleDir;
    QString sourceExperimentId = QStringLiteral( "exp-src" );

    BundleFixture()
    {
        using namespace sicnu::dataset;
        REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
        REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

        const DatasetId datasetId = DatasetId::generate();
        REQUIRE( datasets.createDataset( datasetId, QStringLiteral( "lc" ) ).has_value() );
        DatasetManifest manifest;
        manifest.setDatasetId( datasetId.toString() );
        manifest.setVersionId( DatasetVersionId::generate().toString() );
        const auto draft = datasets.createDraftVersion( manifest );
        REQUIRE( draft.has_value() );
        const DatasetVersionId versionId =
          DatasetVersionId::fromString( draft->versionId() ).value_or( DatasetVersionId{} );
        REQUIRE( datasets.stageVersion( versionId ).has_value() );
        const auto committed = datasets.commitVersion( versionId );
        REQUIRE( committed.has_value() );

        Experiment experiment;
        experiment.setExperimentId( sourceExperimentId );
        experiment.setName( QStringLiteral( "source" ) );
        REQUIRE( experiments.upsertExperiment( experiment ) );

        ExperimentRun run;
        run.setRunId( QStringLiteral( "run-src-d4" ) );
        run.setExperimentId( sourceExperimentId );
        run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
        run.setDatasetVersionId( committed->versionId() );
        run.setDatasetFingerprint( committed->fingerprint() );
        run.setSeed( 42 );
        run.setStatus( RunStatus::Completed );
        run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
        REQUIRE( experiments.upsertRun( run ) );

        ReproductionBundleExporter exporter( experiments, datasets );
        ReproductionBundleOptions options;
        options.outputDir = dir.filePath( QStringLiteral( "bundle" ) );
        options.currentSoftwareRevision = QStringLiteral( "r29-t04" );
        const auto report = exporter.exportRun( QStringLiteral( "run-src-d4" ), options );
        REQUIRE( report.ok );
        bundleDir = options.outputDir;
    }
};

} // namespace

TEST_CASE( "D4: studio session save/restore round-trips the validated design",
           "[r29_t04][studio][session]" )
{
    ensureQgisApplication();
    resetSessionDir( "studio_session_restore" );
    QTemporaryDir dir;
    const QString raster = writeRealInputRaster( dir );
    REQUIRE( !raster.isEmpty() );

    QString sessionPath;
    {
        ExperimentStudioDock dock;
        sessionPath = dock.studioSessionPath();
        inputRasterEditOf( &dock )->setText( raster );
        validateButtonOf( &dock )->click();
        auto *summary = designerSummaryOf( &dock );
        INFO( summary->text().toStdString() );
        REQUIRE( summary->text().contains( QStringLiteral( "Valid design" ) ) );
    } // destructor — the session is persisted on validation

    REQUIRE( QFile::exists( sessionPath ) );

    // Close/reopen: the new dock restores the draft (algorithm + input).
    ExperimentStudioDock reopened;
    const auto session = reopened.sessionState();
    CHECK( session.ok );
    CHECK( session.designerDraft.value( QStringLiteral( "draft_spec" ) )
               .toObject()
               .value( QStringLiteral( "algorithm_id" ) )
               .toString()
           == QStringLiteral( "rs:threshold_raster" ) );
    CHECK( inputRasterEditOf( &reopened )->text() == raster );
    auto *summary2 = designerSummaryOf( &reopened );
    CHECK( summary2->text().contains( QStringLiteral( "restored" ) ) );
}

TEST_CASE( "D4: a damaged studio session is refused explicitly, not partially adopted",
           "[r29_t04][studio][session][damaged]" )
{
    ensureQgisApplication();
    resetSessionDir( "studio_session_damaged" );

    {
        ExperimentStudioDock dock;
        const QString path = dock.studioSessionPath();
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( "{ not json" );
    }

    ExperimentStudioDock reopened;
    CHECK( designerSummaryOf( &reopened )->text().contains( QStringLiteral( "damaged" ) ) );
    // Nothing was adopted: the draft is empty and the default algorithm
    // field stays the constructor default, not a partially-parsed ghost.
    CHECK( reopened.sessionState().designerDraft.isEmpty() );
}

TEST_CASE( "D4: dock import runs the engine's dry-run first and writes nothing",
           "[r29_t04][studio][import][dryrun]" )
{
    ensureQgisApplication();
    resetSessionDir( "studio_import_dry" );
    BundleFixture fx;

    const QString targetStore = fx.dir.filePath( QStringLiteral( "target.db" ) );
    {
        ExperimentStore store;
        REQUIRE( store.open( targetStore ) );
        Experiment experiment;
        experiment.setExperimentId( QStringLiteral( "exp-dst" ) );
        experiment.setName( QStringLiteral( "destination" ) );
        REQUIRE( store.upsertExperiment( experiment ) );
    }

    ExperimentStudioDock dock;
    REQUIRE( dock.openLiveStoreAtPath( targetStore ) );

    const auto precheck = dock.importReproductionBundleAtPath(
      fx.bundleDir, QStringLiteral( "exp-dst" ), false, true );
    INFO( QJsonDocument( precheck.toJson() ).toJson( QJsonDocument::Compact ).toStdString() );
    CHECK( precheck.dryRun );
    CHECK( precheck.ok );
    CHECK_FALSE( precheck.runId.isEmpty() );

    // Dry-run wrote nothing: no runs in the target store yet.
    {
        ExperimentStore check;
        REQUIRE( check.open( targetStore ) );
        const auto page = check.listRuns( QString(), QString(), QString(), 0, 100 );
        if ( page )
            CHECK( page.value().second.isEmpty() );
    }
}

TEST_CASE( "D4: dock import lands Created evidence and re-imports idempotently",
           "[r29_t04][studio][import]" )
{
    ensureQgisApplication();
    resetSessionDir( "studio_import_real" );
    BundleFixture fx;

    const QString targetStore = fx.dir.filePath( QStringLiteral( "target.db" ) );
    {
        ExperimentStore store;
        REQUIRE( store.open( targetStore ) );
        Experiment experiment;
        experiment.setExperimentId( QStringLiteral( "exp-dst" ) );
        experiment.setName( QStringLiteral( "destination" ) );
        REQUIRE( store.upsertExperiment( experiment ) );
    }

    ExperimentStudioDock dock;
    REQUIRE( dock.openLiveStoreAtPath( targetStore ) );

    const auto imported = dock.importReproductionBundleAtPath( fx.bundleDir,
                                                               QStringLiteral( "exp-dst" ),
                                                               false, false );
    INFO( QJsonDocument( imported.toJson() ).toJson( QJsonDocument::Compact ).toStdString() );
    REQUIRE( imported.ok );
    // Default id policy: a fresh legal id, original preserved in the report.
    CHECK( imported.originalRunId == QStringLiteral( "run-src-d4" ) );
    CHECK( imported.runId != QStringLiteral( "run-src-d4" ) );

    {
        ExperimentStore check;
        REQUIRE( check.open( targetStore ) );
        const auto run = check.runById( imported.runId );
        REQUIRE( run.has_value() );
        // Evidence, not execution: status stays Created.
        CHECK( run->status() == RunStatus::Created );
    }

    // Same bundle again (same execution fingerprint): idempotent, no
    // duplicate rows.
    const auto again =
      dock.importReproductionBundleAtPath( fx.bundleDir, QStringLiteral( "exp-dst" ), false, false );
    CHECK( again.ok );
    CHECK( again.alreadyPresent );
    {
        ExperimentStore check;
        REQUIRE( check.open( targetStore ) );
        const auto page = check.listRuns( QString(), QString(), QString(), 0, 100 );
        if ( page )
            CHECK( page.value().second.size() == 1 );
    }

    // keepOriginalRunId=true with the SAME execution fingerprint: the
    // duplicate-ingest guard recognizes the already-imported evidence —
    // idempotent again, echoing the EXISTING installed run (never a second
    // copy under the original id).
    const auto keepOriginal = dock.importReproductionBundleAtPath(
      fx.bundleDir, QStringLiteral( "exp-dst" ), true, false );
    CHECK( keepOriginal.ok );
    CHECK( keepOriginal.alreadyPresent );
    CHECK( keepOriginal.runId == imported.runId );
    {
        ExperimentStore check;
        REQUIRE( check.open( targetStore ) );
        const auto page = check.listRuns( QString(), QString(), QString(), 0, 100 );
        if ( page )
            CHECK( page.value().second.size() == 1 ); // still exactly one run
    }
}

TEST_CASE( "D4: dock import refuses typed when no store is open",
           "[r29_t04][studio][import][refuse]" )
{
    ensureQgisApplication();
    resetSessionDir( "studio_import_refuse" );
    ExperimentStudioDock dock; // no store
    const auto report = dock.importReproductionBundleAtPath(
      QStringLiteral( "/nonexistent" ), QStringLiteral( "exp" ), false, true );
    CHECK_FALSE( report.ok );
    CHECK_FALSE( report.warnings.isEmpty() );
}
