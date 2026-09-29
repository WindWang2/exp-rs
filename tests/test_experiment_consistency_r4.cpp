// test_experiment_consistency_r4.cpp — Track 11 (R4) consistency oracles and
// regressions for the #1333 known-limitations list. Each TEST_CASE pins one
// cross-module invariant that used to fail silently; the truth source is the
// store's persisted state (direct reads), never the implementation's own
// view of itself.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/comparison_ext.h"
#include "experiment/lineage.h"
#include "experiment/promotion.h"
#include "experiment/reproduction_bundle.h"
#include "experiment/reproduction_bundle_import.h"
#include "dataset/dataset_types.h"
#include "dataset/split.h"
#include "experiment/repeat_execution.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QJsonValue>

#include <sqlite3.h>

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

} // namespace

namespace
{

using sicnu::experiment::ExperimentStore;
using sicnu::experiment::MatrixLedger;

// ⑤ (item 5 of the #1333 known-limitations list): outgoingEdges binds its
// page LIMIT before any edge-kind filtering happens, so non-run edges from
// the same node crowd real recorded runs out of runsForCell — silently, with
// no marker. The invariant: the cell↔run page limit bounds RECORDED runs,
// never raw lineage edges of other kinds.
TEST_CASE( "runsForCell filters edge kind before the page limit",
           "[experiment][matrix][r4]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    MatrixLedger ledger( store );

    // Noise first: three non-recorded edges from the same cell, inserted
    // BEFORE the recorded-run edges so the unfiltered LIMIT page would be
    // exhausted by them under store order.
    for ( int i = 0; i < 3; ++i )
    {
        REQUIRE( store
                     .addLineageEdge( QStringLiteral( "matrix" ), QStringLiteral( "c1" ),
                                      QStringLiteral( "evaluated" ), QStringLiteral( "metric" ),
                                      QStringLiteral( "m%1" ).arg( i ) )
                     .operator bool() );
    }

    const QStringList recorded{
        QStringLiteral( "r1" ), QStringLiteral( "r2" ), QStringLiteral( "r3" )
    };
    for ( const QString &runId : recorded )
        REQUIRE( ledger.link( QStringLiteral( "c1" ), runId ).operator bool() );

    // Truth source: the persisted lineage rows, counted through a raw
    // sqlite connection independent of the code under test.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( qUtf8Printable( dir.filePath( QStringLiteral( "experiments.db" ) ) ),
                                  &raw, SQLITE_OPEN_READONLY, nullptr ) == SQLITE_OK );
        sqlite3_stmt *stmt = nullptr;
        REQUIRE( sqlite3_prepare_v2( raw, "SELECT COUNT(*) FROM experiment_lineage WHERE"
                                          " from_kind='matrix' AND from_id='c1'", -1,
                                     &stmt, nullptr ) == SQLITE_OK );
        REQUIRE( sqlite3_step( stmt ) == SQLITE_ROW );
        CHECK( sqlite3_column_int( stmt, 0 ) == 6 );
        sqlite3_finalize( stmt );
        sqlite3_close( raw );
    }

    const auto runsPage = ledger.runsForCell( QStringLiteral( "c1" ), /*limit=*/3 );
    REQUIRE( runsPage.has_value() );
    CHECK( runsPage.value() == recorded );
}

// ④ (item 4): exceeding the cell run budget is a TYPED refusal, never a
// silent truncation — the same honesty contract as the kMaxMatrixCells cap.
// A refusal must be countable: the diagnostic names the cell, the budget and
// the visible lower bound.
TEST_CASE( "runsForCell refuses a cell whose recorded runs overflow the page",
           "[experiment][matrix][r4]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    MatrixLedger ledger( store );

    const int budget = 5;
    for ( int i = 0; i < budget + 1; ++i )
    {
        REQUIRE( ledger
                     .link( QStringLiteral( "c-full" ),
                            QStringLiteral( "r%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ) )
                     .operator bool() );
    }
    // A second cell stays under the budget: pages are per-cell.
    REQUIRE( ledger.link( QStringLiteral( "c-ok" ), QStringLiteral( "rx" ) ).operator bool() );

    const auto refused = ledger.runsForCell( QStringLiteral( "c-full" ), budget );
    REQUIRE( !refused.has_value() );
    bool overflowTyped = false;
    for ( const auto &diagnostic : refused.diagnostics() )
    {
        CHECK( diagnostic.message.contains( QStringLiteral( "c-full" ) ) );
        CHECK( diagnostic.message.contains( QString::number( budget ) ) );
        if ( diagnostic.code == QLatin1String( "experiment.matrix_cell_runs_overflow" ) )
            overflowTyped = true;
    }
    CHECK( overflowTyped );

    // Truth source: budget+1 recorded edges are really in the store, so the
    // refusal reports a truncation that would really have happened.
    const auto allEdges = store.outgoingEdges( QStringLiteral( "matrix" ),
                                               QStringLiteral( "c-full" ), budget + 2,
                                               QStringLiteral( "recorded" ),
                                               QStringLiteral( "run" ) );
    REQUIRE( allEdges.size() == budget + 1 );

    // Under-budget cell still reads whole.
    const auto ok = ledger.runsForCell( QStringLiteral( "c-ok" ), budget );
    REQUIRE( ok.has_value() );
    CHECK( ok.value() == QStringList{ QStringLiteral( "rx" ) } );
}

// ① (item 1): a logical metric present on BOTH sides but in different shapes
// (scalar leaf vs per-class document) produces no paired delta — it used to
// vanish with no trace in the summary. Array-valued metrics are likewise
// shape-excluded. The invariant: every such path is NAMED in a typed note;
// a summary whose delta table is partial says where.
TEST_CASE( "paired comparison names structural asymmetries instead of dropping them",
           "[experiment][comparison][r4]" )
{
    MetricRecord a;
    MetricRecord b;

    // Same family, different shapes: side A records a per-class document
    // under "scores", side B records scalar leaves under "scores::".
    a.metrics = QJsonObject{ { QStringLiteral( "scores" ),
                               QJsonObject{ { QStringLiteral( "water" ),
                                              QJsonObject{ { QStringLiteral( "iou" ), 0.8 } } } } } };
    b.metrics = QJsonObject{ { QStringLiteral( "scores" ),
                               QJsonObject{ { QStringLiteral( "iou" ), 0.7 } } } };

    const auto mismatchSummary = pairedRunComparison( a, b );
    bool mismatchNamed = false;
    for ( const QString &note : mismatchSummary.notes )
        if ( note.startsWith( QLatin1String( "structure_mismatch:scores" ) ) )
            mismatchNamed = true;
    CHECK( mismatchNamed );

    // Array-valued metrics: recorded on both sides, never paired — the
    // summary must say so instead of dropping them silently.
    a.metrics.insert( QStringLiteral( "cm" ),
                      QJsonArray{ QJsonArray{ 1, 2 }, QJsonArray{ 3, 4 } } );
    b.metrics.insert( QStringLiteral( "cm" ),
                      QJsonArray{ QJsonArray{ 1, 2 }, QJsonArray{ 3, 4 } } );
    const auto arraySummary = pairedRunComparison( a, b );
    bool arrayNamed = false;
    for ( const QString &note : arraySummary.notes )
        if ( note.startsWith( QLatin1String( "array_excluded:cm" ) ) )
            arrayNamed = true;
    CHECK( arrayNamed );

    // Symmetric shapes stay note-free: a clean scalar pair produces no
    // structure notes (no crying wolf).
    MetricRecord cleanA;
    cleanA.metrics = QJsonObject{ { QStringLiteral( "f1" ), 0.9 } };
    MetricRecord cleanB;
    cleanB.metrics = QJsonObject{ { QStringLiteral( "f1" ), 0.8 } };
    const auto cleanSummary = pairedRunComparison( cleanA, cleanB );
    bool cleanStructureNotes = false;
    for ( const QString &note : cleanSummary.notes )
        if ( note.startsWith( QLatin1String( "structure_mismatch:" ) ) ||
             note.startsWith( QLatin1String( "array_excluded:" ) ) )
            cleanStructureNotes = true;
    CHECK( !cleanStructureNotes );
    CHECK( cleanSummary.deltas.size() == 1 );
}

namespace
{

// Re-forges the bundle checksums after editing run_config.json — the same
// wholesale-tamper power a forger has. Truth source: the importer's own
// checksum gate must pass so the seed decision is the ONLY thing on trial.
bool reForgeChecksums( const QString &bundleDir )
{
    const QString runConfigPath = QDir( bundleDir ).filePath( QStringLiteral( "run_config.json" ) );
    QFile runConfigFile( runConfigPath );
    if ( !runConfigFile.open( QIODevice::ReadOnly ) )
        return false;
    const QByteArray content = runConfigFile.readAll();
    runConfigFile.close();
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash( content, QCryptographicHash::Sha256 ).toHex() );
    const QString checksumsPath = QDir( bundleDir ).filePath( QStringLiteral( "checksums.txt" ) );
    QFile checksumsFile( checksumsPath );
    if ( !checksumsFile.open( QIODevice::ReadOnly ) )
        return false;
    QStringList lines = QString::fromUtf8( checksumsFile.readAll() )
                            .split( QLatin1Char( '\n' ), Qt::SkipEmptyParts );
    checksumsFile.close();
    for ( QString &line : lines )
        if ( line.endsWith( QStringLiteral( "  run_config.json" ) ) )
            line = digest + QStringLiteral( "  run_config.json" );
    const QByteArray rewritten = lines.join( QLatin1Char( '\n' ) ).toUtf8() + "\n";
    if ( !QFile::remove( checksumsPath ) )
        return false;
    QFile out( checksumsPath );
    if ( !out.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    return out.write( rewritten ) == rewritten.size();
}

bool rewriteRunConfig( const QString &bundleDir, const QJsonObject &runConfig )
{
    const QString runConfigPath = QDir( bundleDir ).filePath( QStringLiteral( "run_config.json" ) );
    const QByteArray content = QJsonDocument( runConfig ).toJson( QJsonDocument::Indented );
    if ( !QFile::remove( runConfigPath ) )
        return false;
    QFile out( runConfigPath );
    if ( !out.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    return out.write( content ) == content.size();
}

} // namespace

// ⑧ (item 8): legacy decimal seeds (bundles written before the seed_hex pin)
// survive a double round-trip only as integers in [0, 2^53). Pre-fix, the
// importer cast whatever the JSON number parsed to straight into a quint64 —
// huge seeds wrapped, negative JSON numbers became implementation-defined.
// The invariant: the legacy path imports losslessly restorable seeds and
// REFUSES the rest (the bundle proves re-export would restore the truth).
TEST_CASE( "legacy decimal seeds outside the lossless domain are refused",
           "[experiment][bundle][r4]" )
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
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
    experiment.setExperimentId( QStringLiteral( "exp-r4-seed" ) );
    experiment.setName( QStringLiteral( "seed domain" ) );
    REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

    ExperimentRun run = makeRun( QStringLiteral( "run-seed" ), QStringLiteral( "exp-r4-seed" ) );
    run.setDatasetVersionId( committed->versionId() );
    run.setDatasetFingerprint( committed->fingerprint() );
    run.setSeed( 42 );
    run.setStatus( RunStatus::Created );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Running );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experiments.upsertRun( run ).has_value() );

    ReproductionBundleExporter exporter( experiments, datasets );
    ReproductionBundleOptions exportOptions;
    exportOptions.outputDir = dir.filePath( QStringLiteral( "bundle" ) );
    exportOptions.currentSoftwareRevision = QStringLiteral( "test" );
    const auto exportReport = exporter.exportRun( QStringLiteral( "run-seed" ), exportOptions );
    REQUIRE( exportReport.ok );
    QString bundleDir = exportReport.bundlePath;
    const auto exportFreshBundle = [&]( const QString &suffix ) {
        ReproductionBundleOptions options = exportOptions;
        options.outputDir = dir.filePath( QStringLiteral( "bundle-%1" ).arg( suffix ) );
        const auto fresh = exporter.exportRun( QStringLiteral( "run-seed" ), options );
        REQUIRE( fresh.ok );
        bundleDir = fresh.bundlePath;
    };

    // The strip helper: legacy-bundle emulation — drop seed_hex, keep the
    // decimal key carrying whatever the forger wants.
    const auto stripHexPin = [&]( const QJsonValue &seedHolder ) {
        QFile file( QDir( bundleDir ).filePath( QStringLiteral( "run_config.json" ) ) );
        REQUIRE( file.open( QIODevice::ReadOnly ) );
        QJsonObject runConfig = QJsonDocument::fromJson( file.readAll() ).object();
        file.close();
        runConfig.remove( QStringLiteral( "seed_hex" ) );
        runConfig.insert( QStringLiteral( "seed" ), seedHolder );
        REQUIRE( rewriteRunConfig( bundleDir, runConfig ) );
        REQUIRE( reForgeChecksums( bundleDir ) );
    };

    const auto importWith = [&]( ExperimentStore &target ) {
        Experiment home;
        home.setExperimentId( QStringLiteral( "exp-r4-seed-dst" ) );
        home.setName( QStringLiteral( "seed destination" ) );
        REQUIRE( target.upsertExperiment( home ).has_value() );
        ReproductionBundleImporter importer( target );
        ReproductionBundleImportOptions options;
        options.bundleDir = bundleDir;
        options.targetExperimentId = QStringLiteral( "exp-r4-seed-dst" );
        return importer.importRun( options );
    };

    // A huge legacy decimal (>= 2^53): not losslessly restorable → refused.
    {
        ExperimentStore targetA;
        REQUIRE( targetA.open( dir.filePath( QStringLiteral( "target-a.db" ) ) ) );
        exportFreshBundle( QStringLiteral( "huge" ) );
        stripHexPin( QJsonValue( 1e19 ) );
        const auto report = importWith( targetA );
        CHECK( !report.ok );
        bool seedNamed = false;
        for ( const QString &warning : report.warnings )
            if ( warning.contains( QLatin1String( "seed" ) ) )
                seedNamed = true;
        CHECK( seedNamed );
    }

    // A negative legacy decimal: same refusal.
    {
        ExperimentStore targetB;
        REQUIRE( targetB.open( dir.filePath( QStringLiteral( "target-b.db" ) ) ) );
        exportFreshBundle( QStringLiteral( "negative" ) );
        stripHexPin( QJsonValue( -5 ) );
        const auto report = importWith( targetB );
        CHECK( !report.ok );
    }

    // An in-range legacy decimal stays importable and lands exactly.
    {
        ExperimentStore targetC;
        REQUIRE( targetC.open( dir.filePath( QStringLiteral( "target-c.db" ) ) ) );
        exportFreshBundle( QStringLiteral( "legacy42" ) );
        stripHexPin( QJsonValue( 42 ) );
        const auto report = importWith( targetC );
        REQUIRE( report.ok );
        const auto installed = targetC.runById( report.runId );
        REQUIRE( installed.has_value() );
        CHECK( installed->seed() == 42 );
    }
}


// ⑩ (item 10, whitelist-bounded minimal fix): splitManifestsForVersion can
// only return a list, so a row that no longer parses used to shrink the
// version's split evidence silently. The fix stays inside the one file this
// track owns and makes the skip LOUD instead: the returned subset is
// accompanied by a stable, countable warning.
TEST_CASE( "skipped corrupt split manifest rows are named, not silent",
           "[dataset][splits][r4]" )
{
    QTemporaryDir dir;
    DatasetStore datasets;
    const QString dbPath = dir.filePath( QStringLiteral( "datasets.db" ) );
    REQUIRE( datasets.open( dbPath ) );

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

    // One healthy split manifest for the committed version (a valid config
    // plus one assignment — a default manifest would fail fromJson and
    // degrade into exactly the corrupt row this test is about).
    SplitManifest healthy;
    healthy.setManifestId( QStringLiteral( "split-healthy" ) );
    healthy.setDatasetVersionId( committed->versionId() );
    SplitConfig healthyConfig;
    healthyConfig.method = SplitMethod::Random;
    healthyConfig.seed = 7;
    healthy.setConfig( healthyConfig );
    SplitAssignment healthyAssignment;
    healthyAssignment.sampleId = QStringLiteral( "sample-1" );
    healthyAssignment.role = SplitRole::Train;
    healthy.assignments().append( healthyAssignment );
    REQUIRE( datasets.saveSplitManifest( healthy ).has_value() );
    {
        const auto roundTrip = datasets.splitManifestById( QStringLiteral( "split-healthy" ) );
        REQUIRE( roundTrip.has_value() ); // the healthy row really parses
    }

    // A second row that no longer parses — injected the way a torn write or
    // a forger would leave it.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( qUtf8Printable( dbPath ), &raw, SQLITE_OPEN_READWRITE,
                                  nullptr )
                 == SQLITE_OK );
        const QString insert = QStringLiteral(
                                   "INSERT INTO split_manifests(manifest_id,"
                                   " dataset_version_id, fingerprint, json, created_ms)"
                                   " VALUES('split-corrupt', '%1', '', '{not json', 1)" )
                                   .arg( committed->versionId() );
        char *error = nullptr;
        REQUIRE( sqlite3_exec( raw, qUtf8Printable( insert ), nullptr, nullptr, &error )
                 == SQLITE_OK );
        sqlite3_free( error );
        sqlite3_close( raw );
    }

    // Capture the category-stable warning the skip must emit. The handler
    // must be a function pointer, so the capture rides in file-static state.
    static QStringList warnings;
    warnings.clear();
    QtMessageHandler previous = qInstallMessageHandler(
        []( QtMsgType type, const QMessageLogContext &, const QString &message ) {
            if ( type == QtWarningMsg )
                warnings.append( message );
        } );
    const DatasetVersionId committedId =
        DatasetVersionId::fromString( committed->versionId() ).value_or( DatasetVersionId{} );
    const QVector<SplitManifest> manifests = datasets.splitManifestsForVersion( committedId );
    qInstallMessageHandler( previous );

    // The healthy row still reads; the corrupt one did not crash the read.
    REQUIRE( manifests.size() == 1 );
    CHECK( manifests.first().manifestId() == QStringLiteral( "split-healthy" ) );

    // And the skip is on the record, countable, with the stable token.
    bool skipNamed = false;
    for ( const QString &warning : warnings )
        if ( warning.contains( QLatin1String( "dataset.split_manifest_corrupt_skipped" ) ) &&
             warning.contains( QLatin1String( "1" ) ) )
            skipNamed = true;
    CHECK( skipNamed );
}


// ⑪ (item 11): corruption must never read as absence on decision paths. A
// run row that no longer parses is tamper-or-torn-write evidence: the
// promotion gate refuses on it (typed failure), the typed reader
// distinguishes it from not-found, and the plain optional reader keeps its
// documented display-seam contract (corrupt reads as absent).
TEST_CASE( "a corrupt run row is refused by the promotion gate, not read as absent",
           "[experiment][promotion][r4]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-r4-corrupt" ) );
    experiment.setName( QStringLiteral( "corrupt row gate" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    ExperimentRun run = makeRun( QStringLiteral( "run-corrupt" ), QStringLiteral( "exp-r4-corrupt" ) );
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( store.upsertRun( run ).has_value() );

    // Corrupt the stored row the way a torn write would.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( qUtf8Printable( dir.filePath( QStringLiteral( "experiments.db" ) ) ),
                                  &raw, SQLITE_OPEN_READWRITE, nullptr )
                 == SQLITE_OK );
        char *error = nullptr;
        REQUIRE( sqlite3_exec( raw, "UPDATE experiment_runs SET json='{' WHERE run_id='run-corrupt'",
                               nullptr, nullptr, &error )
                 == SQLITE_OK );
        sqlite3_free( error );
        sqlite3_close( raw );
    }

    // Truth source: the row IS there, and the typed reader says corrupt.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( qUtf8Printable( dir.filePath( QStringLiteral( "experiments.db" ) ) ),
                                  &raw, SQLITE_OPEN_READONLY, nullptr ) == SQLITE_OK );
        sqlite3_stmt *stmt = nullptr;
        REQUIRE( sqlite3_prepare_v2( raw, "SELECT COUNT(*) FROM experiment_runs WHERE"
                                          " run_id='run-corrupt'", -1, &stmt, nullptr )
                 == SQLITE_OK );
        REQUIRE( sqlite3_step( stmt ) == SQLITE_ROW );
        CHECK( sqlite3_column_int( stmt, 0 ) == 1 );
        sqlite3_finalize( stmt );
        sqlite3_close( raw );
    }
    const auto record = store.runRecordById( QStringLiteral( "run-corrupt" ) );
    REQUIRE( !record.has_value() );
    bool corruptTyped = false;
    for ( const auto &diagnostic : record.diagnostics() )
        if ( diagnostic.code == QLatin1String( "experiment.run_corrupt" ) )
            corruptTyped = true;
    CHECK( corruptTyped );

    // The promotion gate REFUSES on the corrupt evidence instead of scoring
    // the run as merely "missing" and answering a decision from a gap.
    PromotionRequest request;
    request.runId = QStringLiteral( "run-corrupt" );
    request.modelId = QStringLiteral( "model-a" );
    const auto evaluation = PromotionEvaluator( store ).evaluate( request );
    REQUIRE( !evaluation.has_value() );
    bool gateTypedCorrupt = false;
    for ( const auto &diagnostic : evaluation.diagnostics() )
        if ( diagnostic.code == QLatin1String( "experiment.run_corrupt" ) )
            gateTypedCorrupt = true;
    CHECK( gateTypedCorrupt );

    // Control: a genuinely ABSENT run keeps the historical contract
    // (success with missingEvidence naming "run").
    PromotionRequest absentRequest;
    absentRequest.runId = QStringLiteral( "run-never-recorded" );
    absentRequest.modelId = QStringLiteral( "model-a" );
    const auto absent = PromotionEvaluator( store ).evaluate( absentRequest );
    REQUIRE( absent.has_value() );
    CHECK( absent.value().missingEvidence.contains( QStringLiteral( "run" ) ) );
}

// ⑨ (item 9): the whole-table lineage scan carries its truncation in the
// return value — page API names total/truncated, the graph stamps it onto
// every query answer, and the serialization discloses it.
TEST_CASE( "lineage edge page reports truncation instead of hiding it",
           "[experiment][lineage][r4]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    for ( int i = 0; i < 7; ++i )
        REQUIRE( store
                     .addLineageEdge( QStringLiteral( "run" ), QStringLiteral( "r%1" ).arg( i ),
                                      QStringLiteral( "derived_from" ), QStringLiteral( "run" ),
                                      QStringLiteral( "r%1" ).arg( i + 1 ) )
                     .operator bool() );

    // Truth source: the table really holds 7 edges.
    const auto page = store.lineageEdgePage( /*limit=*/5 );
    CHECK( page.total == 7 );
    REQUIRE( page.edges.size() == 5 );
    CHECK( page.truncated );

    const auto full = store.lineageEdgePage( /*limit=*/16 );
    CHECK( full.total == 7 );
    CHECK( full.edges.size() == 7 );
    CHECK( !full.truncated );

    // Graph assembly over an untruncated feed stamps a clean flag (and the
    // JSON discloses it either way).
    DatasetStore datasets;
    REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    LineageGraph graph( datasets, store );
    CHECK( !graph.experimentSourceTruncated() );
    const auto result =
        graph.ancestors( LineageNodeId{ QStringLiteral( "run" ), QStringLiteral( "r7" ) } );
    CHECK( !result.experimentSourceTruncated );
    CHECK( result.toJson().value( QStringLiteral( "experiment_source_truncated" ) ).toBool()
           == false );
}


// WP-D oracle (bundle determinism): exporting the SAME recorded run twice
// into two different directories must produce byte-identical bundle files —
// path separators, EOL and locale number formatting are normalized at the
// export layer, so the digest set is stable across processes and platforms.
// A consumer re-forging or verifying checksums.txt on another machine sees
// the same bytes.
TEST_CASE( "reproduction bundle double export is byte-stable",
           "[experiment][bundle][r4][determinism]" )
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
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
    experiment.setExperimentId( QStringLiteral( "exp-r4-digest" ) );
    experiment.setName( QStringLiteral( "digest" ) );
    REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

    ExperimentRun run = makeRun( QStringLiteral( "run-digest" ), QStringLiteral( "exp-r4-digest" ) );
    run.setDatasetVersionId( committed->versionId() );
    run.setDatasetFingerprint( committed->fingerprint() );
    run.setStatus( RunStatus::Created );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Running );
    REQUIRE( experiments.upsertRun( run ).has_value() );
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experiments.upsertRun( run ).has_value() );

    const auto exportOnce = [&]( const QString &outDir ) {
        ReproductionBundleExporter exporter( experiments, datasets );
        ReproductionBundleOptions options;
        options.outputDir = outDir;
        options.currentSoftwareRevision = QStringLiteral( "r4-digest-pin" );
        const auto report = exporter.exportRun( QStringLiteral( "run-digest" ), options );
        REQUIRE( report.ok );
        return report.bundlePath;
    };
    const QString bundleA = exportOnce( dir.filePath( QStringLiteral( "bundle-a" ) ) );
    const QString bundleB = exportOnce( dir.filePath( QStringLiteral( "bundle-b" ) ) );
    REQUIRE( bundleA != bundleB );

    const QStringList files{
        QStringLiteral( "manifest.json" ),   QStringLiteral( "dataset_refs.json" ),
        QStringLiteral( "run_config.json" ), QStringLiteral( "environment.json" ),
        QStringLiteral( "software.json" ),   QStringLiteral( "model_refs.json" ),
        QStringLiteral( "metrics.json" ),    QStringLiteral( "provenance.json" ),
        QStringLiteral( "split.json" ),      QStringLiteral( "checksums.txt" )
    };
    for ( const QString &name : files )
    {
        QFile a( QDir( bundleA ).filePath( name ) );
        QFile b( QDir( bundleB ).filePath( name ) );
        if ( !a.exists() && !b.exists() )
            continue; // optional file for a run without that evidence
        REQUIRE( a.open( QIODevice::ReadOnly ) );
        REQUIRE( b.open( QIODevice::ReadOnly ) );
        CHECK( a.readAll() == b.readAll() );
    }
}


// P2-2 (review): the typed promotion reader distinguishes corrupt from
// not-found — both legs pinned so the API cannot silently collapse states.
TEST_CASE( "promotionRecordById separates corrupt from not-found",
           "[experiment][promotion][r4]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    // Not found: typed absence.
    const auto absent = store.promotionRecordById( QStringLiteral( "promo-none" ) );
    REQUIRE( !absent.has_value() );
    CHECK( absent.diagnostics().constFirst().code ==
           QLatin1String( "experiment.promotion_not_found" ) );

    // Corrupt: the row exists but no longer parses.
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( qUtf8Printable( dir.filePath( QStringLiteral( "experiments.db" ) ) ),
                                  &raw, SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
        char *error = nullptr;
        REQUIRE( sqlite3_exec( raw,
                               "INSERT INTO model_promotions(promotion_id, run_id, verdict,"
                               " created_ms, json) VALUES('promo-corrupt', 'run-x', 'pending',"
                               " 1, '{ torn')",
                               nullptr, nullptr, &error ) == SQLITE_OK );
        sqlite3_free( error );
        sqlite3_close( raw );
    }
    const auto corrupt = store.promotionRecordById( QStringLiteral( "promo-corrupt" ) );
    REQUIRE( !corrupt.has_value() );
    CHECK( corrupt.diagnostics().constFirst().code ==
           QLatin1String( "experiment.promotion_corrupt" ) );
}

} // namespace
// ⑥ (item 6): the identity-twin scan is bounded (50 by contract); hitting
// the bound used to be invisible. The invariant: a verdict decided against
// a full page says so — twinScanCapped / "twin_scan_capped" — and a verdict
// decided against a partial page does not.
TEST_CASE( "repeat verdict marks when the twin scan hits its cap",
           "[experiment][repeat][r4]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-r4-twins" ) );
    experiment.setName( QStringLiteral( "twin cap oracle" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    // 50 identity twins: distinct run ids, identical pins (all still in
    // Created — status plays no role in the identity hash).
    for ( int i = 0; i < 50; ++i )
    {
        ExperimentRun run =
            makeRun( QStringLiteral( "twin-%1" ).arg( i ), experiment.experimentId() );
        run.setStatus( RunStatus::Created );
        REQUIRE( store.upsertRun( run ).has_value() );
    }

    RepeatExecutionClassifier classifier( store );
    const ExperimentRun probe =
        makeRun( QStringLiteral( "probe" ), experiment.experimentId() );

    // Full page: the verdict must disclose that the scan hit the cap.
    {
        const auto verdict = classifier.classify( probe.executionIdentity() );
        REQUIRE( verdict.has_value() );
        CHECK( verdict.value().twinScanCapped );
        CHECK( verdict.value().toJson().value( QStringLiteral( "twin_scan_capped" ) ).toBool() );
        CHECK( verdict.value().matchedRunIds.size() == 50 );
    }

    // One twin short of the cap: nothing to disclose.
    {
        ExperimentStore store49;
        REQUIRE( store49.open( dir.filePath( QStringLiteral( "experiments49.db" ) ) ) );
        Experiment experiment49;
        experiment49.setExperimentId( QStringLiteral( "exp-r4-twins49" ) );
        experiment49.setName( QStringLiteral( "twin cap oracle 49" ) );
        REQUIRE( store49.upsertExperiment( experiment49 ).has_value() );
        for ( int i = 0; i < 49; ++i )
        {
            ExperimentRun run = makeRun( QStringLiteral( "twin-%1" ).arg( i ),
                                         experiment49.experimentId() );
            run.setStatus( RunStatus::Created );
            REQUIRE( store49.upsertRun( run ).has_value() );
        }
        RepeatExecutionClassifier classifier49( store49 );
        const ExperimentRun probe49 =
            makeRun( QStringLiteral( "probe" ), experiment49.experimentId() );
        const auto verdict = classifier49.classify( probe49.executionIdentity() );
        REQUIRE( verdict.has_value() );
        CHECK( !verdict.value().twinScanCapped );
    }
}
