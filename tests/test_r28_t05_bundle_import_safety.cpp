// tests/test_r28_t05_bundle_import_safety.cpp — R28-T05 regressions for
// #1804/WE-29 + WE-30 + the WP07 dry-run mode on the existing import
// interface:
//   * relocation budgets member reads (byte cap, streamed copy);
//   * relocation never silently overwrites — conflicts are reported and
//     skipped unless explicitly opted in;
//   * dry-run runs every gate, reports the would-be run id and the full
//     conflict summary, and writes nothing (no store rows, no files).
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_ids.h"
#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/reproduction_bundle.h"
#include "experiment/reproduction_bundle_import.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <QDateTime>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

ExperimentRun makeRun( const QString &runId, const QString &experimentId )
{
    ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( experimentId );
    run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
    run.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    run.setDatasetFingerprint( QStringLiteral( "df1" ) );
    run.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    run.setSplitFingerprint( QStringLiteral( "sf1" ) );
    run.setSeed( 42 );
    return run;
}

struct Fixture
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    QString committedVersionId;
    QString committedFingerprint;

    Fixture()
    {
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
        committedVersionId = committed->versionId();
        committedFingerprint = committed->fingerprint();

        Experiment experiment;
        experiment.setExperimentId( QStringLiteral( "exp-src" ) );
        experiment.setName( QStringLiteral( "source" ) );
        REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

        ExperimentRun run = makeRun( QStringLiteral( "run-src" ), QStringLiteral( "exp-src" ) );
        run.setDatasetVersionId( committedVersionId );
        run.setDatasetFingerprint( committedFingerprint );
        run.setStatus( RunStatus::Completed );
        run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
        REQUIRE( experiments.upsertRun( run ).has_value() );
    }

    ReproductionBundleReport exportBundle( const QString &name )
    {
        ReproductionBundleExporter exporter( experiments, datasets );
        ReproductionBundleOptions options;
        options.outputDir = dir.filePath( name );
        options.currentSoftwareRevision = QStringLiteral( "r28-t05" );
        return exporter.exportRun( QStringLiteral( "run-src" ), options );
    }
};

/// Appends a verified data/ member to the bundle (governance-exporter
/// portable shape): writes the payload and extends checksums.txt.
void addDataMember( const QString &bundlePath, const QString &fileName,
                    const QByteArray &payload )
{
    QDir( bundlePath ).mkpath( QStringLiteral( "data" ) );
    const QString memberPath =
        QDir( bundlePath ).filePath( QStringLiteral( "data/" ) + fileName );
    {
        QFile out( memberPath );
        REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        REQUIRE( out.write( payload ) == payload.size() );
    }
    const QString checksumsPath = QDir( bundlePath ).filePath( QStringLiteral( "checksums.txt" ) );
    QFile checksumsFile( checksumsPath );
    REQUIRE( checksumsFile.open( QIODevice::ReadOnly | QIODevice::Append ) );
    const QByteArray existing = checksumsFile.readAll();
    checksumsFile.close();
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash( payload, QCryptographicHash::Sha256 ).toHex() );
    QFile append( checksumsPath );
    REQUIRE( append.open( QIODevice::WriteOnly | QIODevice::Append ) );
    append.write( existing );
    append.write(
        ( digest + QStringLiteral( "  data/" ) + fileName + QStringLiteral( "\n" ) ).toUtf8() );
}

bool destinationHasContent( const QString &targetDir, const QString &fileName,
                            const QByteArray &expected )
{
    QFile file( QDir( targetDir ).filePath( fileName ) );
    if ( !file.open( QIODevice::ReadOnly ) )
        return false;
    return file.readAll() == expected;
}

} // namespace

TEST_CASE( "relocation reports conflicts instead of overwriting (WE-30)",
           "[r28_t05][experiment][repro]" )
{
    Fixture fx;
    const auto exportReport = fx.exportBundle( QStringLiteral( "bundle-a" ) );
    REQUIRE( exportReport.ok );
    const QByteArray payload = QByteArray( "student-ndvi-output-v1" );
    addDataMember( exportReport.bundlePath, QStringLiteral( "ndvi.tif" ), payload );

    ExperimentStore target;
    REQUIRE( target.open( fx.dir.filePath( QStringLiteral( "target-a.db" ) ) ) );
    Experiment home;
    home.setExperimentId( QStringLiteral( "exp-dst" ) );
    home.setName( QStringLiteral( "destination" ) );
    REQUIRE( target.upsertExperiment( home ).has_value() );

    const QString relocateDir = fx.dir.filePath( QStringLiteral( "relocate-a" ) );
    ReproductionBundleImporter importer( target );

    // First import relocates cleanly.
    {
        ReproductionBundleImportOptions options;
        options.bundleDir = exportReport.bundlePath;
        options.targetExperimentId = QStringLiteral( "exp-dst" );
        options.relocateDataDir = relocateDir;
        const auto report = importer.importRun( options );
        REQUIRE( report.ok );
        CHECK( report.relocatedFiles == 1 );
        CHECK( report.conflicts.isEmpty() );
        CHECK( destinationHasContent( relocateDir, QStringLiteral( "ndvi.tif" ), payload ) );
    }

    // A second import of a DIFFERENT payload under the same name must not
    // destroy the relocated file: the conflict is reported and the member
    // skipped (the run install itself remains idempotent through the
    // duplicate-ingest guard).
    const QByteArray otherPayload = QByteArray( "other-run-output" );
    addDataMember( exportReport.bundlePath, QStringLiteral( "ndvi2.tif" ), otherPayload );
    // Rewrite ndvi.tif's content + checksum so the bundle is self-consistent
    // again but carries different bytes for the existing member name.
    {
        QDir dataDir( QDir( exportReport.bundlePath ).filePath( QStringLiteral( "data" ) ) );
        QFile out( dataDir.filePath( QStringLiteral( "ndvi.tif" ) ) );
        REQUIRE( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        out.write( otherPayload );
    }
    // Re-sign the whole checksums table for the changed member.
    {
        const QString checksumsPath =
            QDir( exportReport.bundlePath ).filePath( QStringLiteral( "checksums.txt" ) );
        QFile checksumsFile( checksumsPath );
        REQUIRE( checksumsFile.open( QIODevice::ReadOnly ) );
        QStringList lines = QString::fromUtf8( checksumsFile.readAll() )
                                .split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
        checksumsFile.close();
        const QString newDigest = QString::fromLatin1(
            QCryptographicHash::hash( otherPayload, QCryptographicHash::Sha256 ).toHex() );
        for ( QString &line : lines )
            if ( line.endsWith( QStringLiteral( "  data/ndvi.tif" ) ) )
                line = newDigest + QStringLiteral( "  data/ndvi.tif" );
        QFile rewritten( checksumsPath );
        REQUIRE( rewritten.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
        rewritten.write( lines.join( QLatin1Char( '\n' ) ).toUtf8() + "\n" );
    }

    ExperimentStore target2;
    REQUIRE( target2.open( fx.dir.filePath( QStringLiteral( "target-a2.db" ) ) ) );
    REQUIRE( target2.upsertExperiment( home ).has_value() );
    ReproductionBundleImporter importer2( target2 );
    ReproductionBundleImportOptions options;
    options.bundleDir = exportReport.bundlePath;
    options.targetExperimentId = QStringLiteral( "exp-dst" );
    options.relocateDataDir = relocateDir;
    const auto report = importer2.importRun( options );
    REQUIRE( report.ok );
    CHECK( report.relocatedFiles == 1 ); // only ndvi2.tif landed
    REQUIRE( report.conflicts.size() == 1 );
    CHECK( report.conflicts.first().contains( QStringLiteral( "ndvi.tif" ) ) );
    CHECK( report.conflicts.first().contains( QStringLiteral( "not overwritten" ) ) );
    // The original relocated bytes survived.
    CHECK( destinationHasContent( relocateDir, QStringLiteral( "ndvi.tif" ), payload ) );

    // Explicit opt-in replaces the destination.
    ReproductionBundleImportOptions overwrite = options;
    overwrite.overwriteExistingData = true;
    const auto overwriteReport = importer2.importRun( overwrite );
    REQUIRE( overwriteReport.ok );
    CHECK( destinationHasContent( relocateDir, QStringLiteral( "ndvi.tif" ), otherPayload ) );
}

TEST_CASE( "relocation refuses members above the declared byte budget (WE-29)",
           "[r28_t05][experiment][repro]" )
{
    Fixture fx;
    const auto exportReport = fx.exportBundle( QStringLiteral( "bundle-b" ) );
    REQUIRE( exportReport.ok );
    const QByteArray payload( 64, 'x' );
    addDataMember( exportReport.bundlePath, QStringLiteral( "big.tif" ), payload );

    ExperimentStore target;
    REQUIRE( target.open( fx.dir.filePath( QStringLiteral( "target-b.db" ) ) ) );
    Experiment home;
    home.setExperimentId( QStringLiteral( "exp-dst" ) );
    home.setName( QStringLiteral( "destination" ) );
    REQUIRE( target.upsertExperiment( home ).has_value() );

    ReproductionBundleImporter importer( target );
    ReproductionBundleImportOptions options;
    options.bundleDir = exportReport.bundlePath;
    options.targetExperimentId = QStringLiteral( "exp-dst" );
    options.relocateDataDir = fx.dir.filePath( QStringLiteral( "relocate-b" ) );
    options.maxRelocateMemberBytes = 16; // far below the 64-byte member
    const auto report = importer.importRun( options );
    REQUIRE( report.ok );
    CHECK( report.relocatedFiles == 0 );
    bool refused = false;
    for ( const QString &warning : report.warnings )
        if ( warning.contains( QStringLiteral( "big.tif" ) )
             && warning.contains( QStringLiteral( "budget" ) ) )
            refused = true;
    CHECK( refused );
    CHECK_FALSE( QFileInfo::exists(
        QDir( options.relocateDataDir ).filePath( QStringLiteral( "big.tif" ) ) ) );
    CHECK_FALSE( QFileInfo::exists( QDir( options.relocateDataDir ).filePath(
        QStringLiteral( "big.tif.importing" ) ) ) );
}

TEST_CASE( "dry-run imports check everything and write nothing (WP07)",
           "[r28_t05][experiment][repro]" )
{
    Fixture fx;
    const auto exportReport = fx.exportBundle( QStringLiteral( "bundle-c" ) );
    REQUIRE( exportReport.ok );
    const QByteArray payload = QByteArray( "payload" );
    addDataMember( exportReport.bundlePath, QStringLiteral( "out.tif" ), payload );

    ExperimentStore target;
    REQUIRE( target.open( fx.dir.filePath( QStringLiteral( "target-c.db" ) ) ) );
    Experiment home;
    home.setExperimentId( QStringLiteral( "exp-dst" ) );
    home.setName( QStringLiteral( "destination" ) );
    REQUIRE( target.upsertExperiment( home ).has_value() );

    const QString relocateDir = fx.dir.filePath( QStringLiteral( "relocate-c" ) );
    ReproductionBundleImporter importer( target );
    ReproductionBundleImportOptions options;
    options.bundleDir = exportReport.bundlePath;
    options.targetExperimentId = QStringLiteral( "exp-dst" );
    options.relocateDataDir = relocateDir;
    options.dryRun = true;

    const auto report = importer.importRun( options );
    REQUIRE( report.ok );
    CHECK( report.dryRun );
    CHECK_FALSE( report.runId.isEmpty() );
    // Nothing landed: no store row, no relocated file.
    CHECK_FALSE( target.runById( report.runId ).has_value() );
    CHECK_FALSE( QFileInfo::exists( QDir( relocateDir ).filePath(
        QStringLiteral( "out.tif" ) ) ) );

    // A second dry-run against an existing destination surfaces the would-be
    // overwrite as a conflict without touching the file.
    {
        QDir( relocateDir ).mkpath( QStringLiteral( "." ) );
        QFile existing( QDir( relocateDir ).filePath( QStringLiteral( "out.tif" ) ) );
        REQUIRE( existing.open( QIODevice::WriteOnly ) );
        existing.write( "previous" );
    }
    const auto conflictReport = importer.importRun( options );
    REQUIRE( conflictReport.ok );
    CHECK( conflictReport.dryRun );
    REQUIRE( conflictReport.conflicts.size() == 1 );
    CHECK( conflictReport.conflicts.first().contains( QStringLiteral( "WOULD be overwritten" ) ) );
    CHECK( destinationHasContent( relocateDir, QStringLiteral( "out.tif" ),
                                 QByteArray( "previous" ) ) );

    // The real (non-dry) import afterwards works on the same store.
    ReproductionBundleImportOptions real = options;
    real.dryRun = false;
    real.overwriteExistingData = true;
    const auto realReport = importer.importRun( real );
    REQUIRE( realReport.ok );
    CHECK_FALSE( realReport.dryRun );
    CHECK( target.runById( realReport.runId ).has_value() );
    CHECK( destinationHasContent( relocateDir, QStringLiteral( "out.tif" ), payload ) );
}
