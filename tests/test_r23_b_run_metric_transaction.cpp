// tests/test_r23_b_run_metric_transaction.cpp — R23-B risk A5 closure.
//
// The bundle importer's HEAD sequence was saveMetricRecord() → upsertRun()
// with a compensation delete on run-install failure. That closes the
// FAILURE window but not the CRASH window: a process death between the two
// commits stranded an orphan metric row keyed to a run id that was never
// installed (recorded as the r21-B A5 residual). The fix installs both rows
// through ExperimentStore::upsertRunWithMetric in ONE transaction.
//
// Oracles:
//  1. Crash-window A/B (pre-fix sequence): a forked child performs exactly
//     the importer's write order on its own store connection — metric commit,
//     then immediate _exit(9) where the process could die — and the parent
//     observes the orphan (metric row present, run row absent). This pins
//     the window the two-call sequence exposes.
//  2. Atomicity: with an injected run-write failure (BEFORE INSERT RAISE
//     trigger, same surgery as test_experiment_consistency_r4), the combined
//     call leaves NEITHER row.
//  3. Equivalence: the happy path lands both rows; identical re-save is a
//     no-op success; a content conflict refuses and changes NEITHER row.
//  4. Typed refusal when the metric names a different run than the one being
//     installed (the atomicity promise only binds same-run rows).
#include <catch2/catch_test_macros.hpp>

#include "experiment/evaluation.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include <QDateTime>
#include <QTemporaryDir>

#include <sqlite3.h>

#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

using sicnu::experiment::ExperimentStore;
using sicnu::experiment::Experiment;
using sicnu::experiment::ExperimentRun;
using sicnu::experiment::MetricRecord;
using sicnu::experiment::RunStatus;

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

MetricRecord makeMetric( const QString &runId, double accuracy )
{
    MetricRecord metric;
    metric.runId = runId;
    metric.protocol.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    metric.protocol.setSplitManifestId( QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    metric.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), accuracy } };
    return metric;
}

void seedExperiment( ExperimentStore &store, const QString &experimentId )
{
    Experiment experiment;
    experiment.setExperimentId( experimentId );
    experiment.setName( QStringLiteral( "a5-closure" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
}

// Direct DB surgery (same technique as test_experiment_consistency_r4's A5
// case): block run inserts so the run write aborts inside its transaction.
void setRunInsertTrigger( const QString &dbPath, bool install )
{
    sqlite3 *raw = nullptr;
    REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw, SQLITE_OPEN_READWRITE,
                              nullptr ) == SQLITE_OK );
    char *err = nullptr;
    const char *sql =
      install ? "CREATE TRIGGER block_run_insert BEFORE INSERT ON experiment_runs"
                " BEGIN SELECT RAISE(ABORT, 'run install blocked'); END;"
              : "DROP TRIGGER block_run_insert";
    REQUIRE( sqlite3_exec( raw, sql, nullptr, nullptr, &err ) == SQLITE_OK );
    sqlite3_free( err );
    sqlite3_close( raw );
}

} // namespace

TEST_CASE( "upsertRunWithMetric is atomic under an injected run-write failure",
           "[experiment][r23b]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "experiments.db" ) );
    ExperimentStore store;
    REQUIRE( store.open( dbPath ) );
    seedExperiment( store, QStringLiteral( "exp-a5-atomic" ) );

    setRunInsertTrigger( dbPath, true );
    const auto run = makeRun( QStringLiteral( "run-a5-atomic" ), QStringLiteral( "exp-a5-atomic" ) );
    const auto metric = makeMetric( QStringLiteral( "run-a5-atomic" ), 0.75 );
    const auto installed = store.upsertRunWithMetric( run, metric );
    CHECK( !installed );

    // Neither row: the single transaction rolled the metric insert back
    // with the aborted run write.
    CHECK( store.runById( QStringLiteral( "run-a5-atomic" ) ) == std::nullopt );
    const auto remaining = store.listMetricRecords();
    REQUIRE( remaining.has_value() );
    CHECK( remaining.value().second.isEmpty() );

    setRunInsertTrigger( dbPath, false );
    REQUIRE( store.upsertRunWithMetric( run, metric ).has_value() );
    CHECK( store.runById( QStringLiteral( "run-a5-atomic" ) ).has_value() );
    const auto landed = store.metricRecordForRun( QStringLiteral( "run-a5-atomic" ) );
    REQUIRE( landed.has_value() );
    CHECK( landed->metrics[QStringLiteral( "overall_accuracy" )] == 0.75 );
}

TEST_CASE( "upsertRunWithMetric conflict and mismatch refusals change nothing",
           "[experiment][r23b]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    seedExperiment( store, QStringLiteral( "exp-a5-conflict" ) );

    const auto run = makeRun( QStringLiteral( "run-a5-conflict" ), QStringLiteral( "exp-a5-conflict" ) );
    REQUIRE( store.upsertRunWithMetric( run, makeMetric( QStringLiteral( "run-a5-conflict" ), 0.9 ) )
               .has_value() );

    // A run may advance while its metric stays content-identical (re-save
    // with the same numbers rides along as the historical no-op success).
    auto advanced = run;
    advanced.setStatus( RunStatus::Running );
    REQUIRE( store.upsertRunWithMetric( advanced, makeMetric( QStringLiteral( "run-a5-conflict" ), 0.9 ) )
               .has_value() );
    CHECK( store.runById( QStringLiteral( "run-a5-conflict" ) ).has_value() );

    // Different metric content for an EXISTING run's record is a conflict —
    // and the status advance riding the same call must NOT land either.
    auto conflicting = advanced;
    conflicting.setStatus( RunStatus::Completed );
    const auto refused = store.upsertRunWithMetric(
      conflicting, makeMetric( QStringLiteral( "run-a5-conflict" ), 0.1 ) );
    CHECK( !refused );
    const auto unchanged = store.runById( QStringLiteral( "run-a5-conflict" ) );
    REQUIRE( unchanged.has_value() );
    CHECK( unchanged->status() == RunStatus::Running );

    // A metric naming a DIFFERENT run than the one being installed refuses:
    // such a row would escape this transaction's all-or-nothing guarantee.
    const auto mismatch = store.upsertRunWithMetric(
      makeRun( QStringLiteral( "run-a5-other" ), QStringLiteral( "exp-a5-conflict" ) ),
      makeMetric( QStringLiteral( "run-a5-conflict" ), 0.5 ) );
    CHECK( !mismatch );
    CHECK( store.runById( QStringLiteral( "run-a5-other" ) ) == std::nullopt );
}

#ifdef Q_OS_UNIX

// The pre-fix crash window, pinned: the child performs exactly the HEAD
// importer's write order on its own connection — commit the metric, then
// die where the process could die — and the parent reads the store the next
// process would read. (The importer now calls upsertRunWithMetric, a single
// commit boundary, so this sequence no longer exists in production; the
// case documents why.)
TEST_CASE( "the two-call sequence leaves an orphan metric on process death",
           "[experiment][r23b][crash]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "crash.db" ) );
    {
        ExperimentStore store;
        REQUIRE( store.open( dbPath ) );
        seedExperiment( store, QStringLiteral( "exp-a5-crash" ) );
    }

    const pid_t child = fork();
    REQUIRE( child >= 0 );
    if ( child == 0 )
    {
        // Child: own connection, the HEAD importer's first write, then
        // death before the second. _exit skips atexit/Qt teardown — the
        // closest in-test analogue of a crash between the two commits.
        ExperimentStore store;
        if ( !store.open( dbPath ) )
            _exit( 10 );
        _exit( store.saveMetricRecord( makeMetric( QStringLiteral( "run-a5-crash" ), 0.5 ) )
                 .has_value()
                 ? 0
                 : 11 );
    }
    int status = 0;
    REQUIRE( waitpid( child, &status, 0 ) == child );
    REQUIRE( WIFEXITED( status ) );
    REQUIRE( WEXITSTATUS( status ) == 0 );

    ExperimentStore store;
    REQUIRE( store.open( dbPath ) );
    CHECK( store.metricRecordForRun( QStringLiteral( "run-a5-crash" ) ).has_value() );
    CHECK( store.runById( QStringLiteral( "run-a5-crash" ) ) == std::nullopt );

    // The orphan is exactly what upsertRunWithMetric cannot produce: the
    // same write attempted through the combined API installs BOTH rows.
    const auto run =
      makeRun( QStringLiteral( "run-a5-crash" ), QStringLiteral( "exp-a5-crash" ) );
    REQUIRE( store.deleteMetricRecord( QStringLiteral( "run-a5-crash" ) ).has_value() );
    REQUIRE( store.upsertRunWithMetric( run, makeMetric( QStringLiteral( "run-a5-crash" ), 0.5 ) )
               .has_value() );
    CHECK( store.runById( QStringLiteral( "run-a5-crash" ) ).has_value() );
    CHECK( store.metricRecordForRun( QStringLiteral( "run-a5-crash" ) ).has_value() );
}

#endif // Q_OS_UNIX
