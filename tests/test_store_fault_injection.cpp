// test_store_fault_injection.cpp — 12.0 data-foundation fault Oracle (O3):
// deterministic transaction fault injection across the three science stores.
// Every fault routes through the REAL failure branch (a failing statement
// step or the fault-point commit branch) and must leave zero partial rows,
// zero phantom references and a usable store.
//
// Determinism note: SQLite's max_page_count is a per-connection limit, so a
// separate raw connection cannot impose disk-full on the store's own
// connection. The planted-trigger technique (RAISE(ABORT) with a count
// predicate, same helper family as test_governance_store) fails a statement
// MID-TRANSACTION through exactly the code path a real write failure takes
// (step() != SQLITE_DONE → typed failure → ROLLBACK), deterministically and
// portably.
#include <catch2/catch_test_macros.hpp>

#include "dataset/dataset_store.h"
#include "dataset/dataset_manifest.h"
#include "dataset/sample.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "runtime/observability/fault_registry.h"

#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include <sqlite3.h>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

void plantTrigger( const QString &dbPath, const QString &sql )
{
    sqlite3 *raw = nullptr;
    REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw, SQLITE_OPEN_READWRITE,
                              nullptr ) == SQLITE_OK );
    char *err = nullptr;
    const int rc = sqlite3_exec( raw, sql.toUtf8().constData(), nullptr, nullptr, &err );
    if ( rc != SQLITE_OK )
        INFO( QString::fromUtf8( err ).toStdString() );
    sqlite3_free( err );
    sqlite3_close( raw );
    REQUIRE( rc == SQLITE_OK );
}

DatasetManifest makeManifest( const QString &datasetId, const QString &versionId )
{
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId );
    manifest.setVersionId( versionId );
    manifest.setName( QStringLiteral( "fault fixture" ) );
    manifest.setCreatedAtUtc( QDateTime::fromString(
        QStringLiteral( "2026-09-20T00:00:00.000Z" ), Qt::ISODateWithMs ) );
    return manifest;
}

ExperimentRun makeRun( const QString &runId, const QString &experimentId )
{
    ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( experimentId );
    run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
    run.setStatus( RunStatus::Created );
    return run;
}

} // namespace

TEST_CASE( "mid-batch statement failure leaves zero partial run rows and the "
           "store usable",
           "[fault][experiment][oracle]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "exp.db" ) );
    ExperimentStore store;
    REQUIRE( store.open( dbPath ) );
    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "fault" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    QVector<ExperimentRun> batch;
    for ( int i = 0; i < 5; ++i )
        batch.append( makeRun( QStringLiteral( "f-%1" ).arg( i ),
                               experiment.experimentId() ) );

    // Fail the THIRD insert of the batch (two rows already written in the
    // open transaction): the real step-failure branch must roll the whole
    // batch back.
    plantTrigger( dbPath, QStringLiteral(
        "CREATE TRIGGER fail_third_run BEFORE INSERT ON experiment_runs"
        " WHEN (SELECT COUNT(*) FROM experiment_runs"
        "       WHERE experiment_id='%1') >= 2"
        " BEGIN SELECT RAISE(ABORT,'injected mid-batch failure'); END" )
                      .arg( experiment.experimentId() ) );
    const auto rejected = store.upsertRunsBatch( batch );
    CHECK_FALSE( rejected.has_value() );
    CHECK( rejected.diagnostics().first().code == QStringLiteral( "experiment.store_write_failed" ) );
    CHECK( store.runCount() == 0 );
    for ( const ExperimentRun &run : batch )
        CHECK_FALSE( store.runById( run.runId() ).has_value() );

    // The connection took a failure but is not wedged: drop the trigger and
    // the same batch lands whole.
    plantTrigger( dbPath, QStringLiteral( "DROP TRIGGER fail_third_run" ) );
    REQUIRE( store.upsertRunsBatch( batch ).has_value() );
    CHECK( store.runCount() == 5 );
}

TEST_CASE( "batch commit fault point rolls the whole batch back",
           "[fault][experiment][oracle]" )
{
    using namespace sicnu::runtime::observability::fault;
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "fault-commit" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    QVector<ExperimentRun> batch;
    for ( int i = 0; i < 3; ++i )
        batch.append( makeRun( QStringLiteral( "c-%1" ).arg( i ),
                               experiment.experimentId() ) );

    armFault( FaultAction{ "experiment_store.batch_commit", Mode::NextN, 1, "" } );
    const auto rejected = store.upsertRunsBatch( batch );
    CHECK_FALSE( rejected.has_value() );
    CHECK( store.runCount() == 0 );
    CHECK( store.runById( QStringLiteral( "c-0" ) ) == std::nullopt );

    // Disarmed (consumed once): the retry succeeds — no poisoned state.
    REQUIRE( store.upsertRunsBatch( batch ).has_value() );
    CHECK( store.runCount() == 3 );
}

TEST_CASE( "mid-batch sample failure rolls back the whole version batch",
           "[fault][dataset][oracle]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "ds.db" ) );
    DatasetStore store;
    REQUIRE( store.open( dbPath ) );
    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( store.createDataset( datasetId, QStringLiteral( "fault" ) ).has_value() );
    const DatasetVersionId versionId = DatasetVersionId::generate();
    REQUIRE( store.createDraftVersion( makeManifest( datasetId.toString(),
                                                     versionId.toString() ) )
                 .has_value() );

    QVector<SampleRecord> batch;
    for ( int i = 0; i < 4; ++i )
    {
        SampleRecord sample;
        sample.setSampleId( SampleId::generate().toString() );
        sample.setDatasetVersionId( versionId.toString() );
        sample.setKind( SampleKind::Point );
        PointSample point;
        point.x = i;
        point.y = 0;
        sample.payload() = point;
        batch.append( sample );
    }

    plantTrigger( dbPath, QStringLiteral(
        "CREATE TRIGGER fail_third_sample BEFORE INSERT ON samples"
        " WHEN (SELECT COUNT(*) FROM samples"
        "       WHERE dataset_version_id='%1') >= 2"
        " BEGIN SELECT RAISE(ABORT,'injected mid-batch failure'); END" )
                      .arg( versionId.toString() ) );
    const auto rejected = store.addSamples( batch );
    CHECK_FALSE( rejected.has_value() );
    CHECK( store.sampleCount( versionId ) == 0 );
    plantTrigger( dbPath, QStringLiteral( "DROP TRIGGER fail_third_sample" ) );

    REQUIRE( store.addSamples( batch ).has_value() );
    CHECK( store.sampleCount( versionId ) == 4 );
}

TEST_CASE( "corrupt rows fail paged reads closed on cursor and offset paths "
           "without poisoning the store",
           "[fault][corruption][oracle]" )
{
    // Experiment store: one tampered run row fails listRuns AND the cursor
    // walk typed; well-formed rows stay readable; the row stays for
    // inspection.
    QTemporaryDir dir;
    const QString expPath = dir.filePath( QStringLiteral( "exp.db" ) );
    ExperimentStore store;
    REQUIRE( store.open( expPath ) );
    Experiment experiment;
    experiment.setExperimentId( ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "corrupt" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
    QVector<ExperimentRun> batch;
    for ( int i = 0; i < 3; ++i )
        batch.append( makeRun( QStringLiteral( "r-%1" ).arg( i ),
                               experiment.experimentId() ) );
    REQUIRE( store.upsertRunsBatch( batch ).has_value() );
    store.close();

    plantTrigger( expPath, QStringLiteral( "CREATE TABLE IF NOT EXISTS noop(x)" ) ); // db open sanity
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( expPath.toUtf8().constData(), &raw,
                                  SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
        sqlite3_exec( raw, "UPDATE experiment_runs SET json='{broken' WHERE run_id='r-1'",
                      nullptr, nullptr, nullptr );
        sqlite3_close( raw );
    }

    ExperimentStore reopened;
    REQUIRE( reopened.open( expPath ) );
    const auto offsetList = reopened.listRuns( experiment.experimentId() );
    CHECK_FALSE( offsetList.has_value() );
    CHECK( offsetList.diagnostics().first().code == QStringLiteral( "experiment.corrupt_record" ) );
    const auto cursorList = reopened.listRunsByCursor( experiment.experimentId(), QString(),
                                                       QString(), QString(), 2 );
    CHECK_FALSE( cursorList.has_value() );
    CHECK( cursorList.diagnostics().first().code == QStringLiteral( "experiment.corrupt_record" ) );
    // The corrupt row cannot be silently overwritten (#1056 contract).
    ExperimentRun sameId = makeRun( QStringLiteral( "r-1" ), experiment.experimentId() );
    sameId.setStatus( RunStatus::Created );
    const auto overwrite = reopened.upsertRun( sameId );
    CHECK_FALSE( overwrite.has_value() );
    CHECK( overwrite.diagnostics().first().code == QStringLiteral( "experiment.corrupt_record" ) );
    // Healthy rows remain readable through direct lookups.
    CHECK( reopened.runById( QStringLiteral( "r-0" ) ).has_value() );
    // And a batch that excludes corrupt rows still lands (store not wedged).
    REQUIRE( reopened.upsertRunsBatch( { makeRun( QStringLiteral( "r-9" ),
                                                  experiment.experimentId() ) } )
                 .has_value() );
    CHECK( reopened.runById( QStringLiteral( "r-9" ) ).has_value() );

    // Dataset store: a tampered sample row fails the cursor page typed.
    QTemporaryDir dsDir;
    const QString dsPath = dsDir.filePath( QStringLiteral( "ds.db" ) );
    DatasetStore ds;
    REQUIRE( ds.open( dsPath ) );
    const DatasetId datasetId = DatasetId::generate();
    REQUIRE( ds.createDataset( datasetId, QStringLiteral( "corrupt" ) ).has_value() );
    const DatasetVersionId versionId = DatasetVersionId::generate();
    REQUIRE( ds.createDraftVersion( makeManifest( datasetId.toString(),
                                                  versionId.toString() ) )
                 .has_value() );
    QVector<SampleRecord> samples;
    for ( int i = 0; i < 2; ++i )
    {
        SampleRecord sample;
        sample.setSampleId( SampleId::generate().toString() );
        sample.setDatasetVersionId( versionId.toString() );
        sample.setKind( SampleKind::Point );
        PointSample point;
        point.x = i;
        point.y = 0;
        sample.payload() = point;
        samples.append( sample );
    }
    REQUIRE( ds.addSamples( samples ).has_value() );
    ds.close();
    {
        sqlite3 *raw = nullptr;
        REQUIRE( sqlite3_open_v2( dsPath.toUtf8().constData(), &raw,
                                  SQLITE_OPEN_READWRITE, nullptr ) == SQLITE_OK );
        sqlite3_exec( raw, "UPDATE samples SET json='{broken'", nullptr, nullptr, nullptr );
        sqlite3_close( raw );
    }
    DatasetStore dsReopened;
    REQUIRE( dsReopened.open( dsPath ) );
    const auto page = dsReopened.samplesPageCursor( versionId, QString(), 500 );
    CHECK_FALSE( page.has_value() );
    CHECK( page.diagnostics().first().code == QStringLiteral( "dataset.corrupt_sample" ) );
}
