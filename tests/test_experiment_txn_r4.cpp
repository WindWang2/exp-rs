// test_experiment_txn_r4.cpp — Track 11 (R4) WP-E: transaction boundaries
// of the experiment store, pinned as oracles.
//
// Boundary truth: every store API is its own transaction (upsertRun /
// upsertRunsBatch / saveMetricRecord / saveMetricRecordsBatch /
// addLineageEdge); a mid-write fault leaves the seam's tables EXACTLY as
// before — countable via store queries AND via direct sqlite reads of the
// persisted file — and the store survives (reopen == pre-fault state, the
// WAL never surfaces a half transaction, ADR 0137). Failure injection goes
// through the production seams (armed fault points, planted RAISE
// triggers) — never by hand-editing the database into a state production
// could not reach.
#include <catch2/catch_test_macros.hpp>

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/evaluation.h"
#include "runtime/observability/fault_registry.h"

#include <QJsonObject>
#include <QTemporaryDir>

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
    run.setStatus( RunStatus::Created );
    return run;
}

MetricRecord makeMetric( const QString &runId, double accuracy )
{
    MetricRecord record;
    record.runId = runId;
    record.protocol.setDatasetVersionId(
        QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    record.protocol.setSplitManifestId(
        QStringLiteral( "22222222-2222-4222-8222-222222222222" ) );
    record.metrics = QJsonObject{ { QStringLiteral( "overall_accuracy" ), accuracy } };
    record.metricsSchemaVersion = kMetricsSchemaVersion;
    return record;
}

qint64 directCount( const QString &dbPath, const QString &table )
{
    sqlite3 *raw = nullptr;
    REQUIRE( sqlite3_open_v2( qUtf8Printable( dbPath ), &raw, SQLITE_OPEN_READONLY,
                              nullptr )
             == SQLITE_OK );
    sqlite3_stmt *stmt = nullptr;
    const QString sql = QStringLiteral( "SELECT COUNT(*) FROM %1" ).arg( table );
    REQUIRE( sqlite3_prepare_v2( raw, qUtf8Printable( sql ), -1, &stmt, nullptr )
             == SQLITE_OK );
    REQUIRE( sqlite3_step( stmt ) == SQLITE_ROW );
    const qint64 count = sqlite3_column_int64( stmt, 0 );
    sqlite3_finalize( stmt );
    sqlite3_close( raw );
    return count;
}

} // namespace

// O-txn-1 (WP-E): the metric-record batch is all-or-nothing on the FIRST
// conflicting roll — a batch of three with one conflict lands zero rows,
// and the pre-existing record it conflicts with is byte-identical after.
TEST_CASE( "metric record batch rolls back whole on the first conflict",
           "[experiment][txn][r4]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "exp.db" ) );
    ExperimentStore store;
    REQUIRE( store.open( dbPath ) );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-txn" ) );
    experiment.setName( QStringLiteral( "txn" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
    REQUIRE( store.upsertRun( makeRun( QStringLiteral( "run-a" ),
                                       QStringLiteral( "exp-txn" ) ) )
                 .has_value() );

    MetricRecord original = makeMetric( QStringLiteral( "run-a" ), 0.8 );
    original.metricsSchemaVersion = 1;
    REQUIRE( store.saveMetricRecord( original ).has_value() );

    // [new row, CONFLICT with run-a, new row] — the conflict is roll two.
    MetricRecord conflict = makeMetric( QStringLiteral( "run-a" ), 0.99 );
    conflict.metricsSchemaVersion = 1;
    // The batch's middle roll conflicts with the EXISTING run-a record
    // (same run id, different content — saveMetricRecord's per-run
    // uniqueness is the seam a torn concurrent write would hit).
    const auto rejected = store.saveMetricRecordsBatch(
        { makeMetric( QStringLiteral( "run-b-txn" ), 0.7 ), conflict,
          makeMetric( QStringLiteral( "run-c-txn" ), 0.6 ) } );
    CHECK( !rejected.has_value() );

    // Countable truth, both through the store and through the file:
    // exactly ONE metric record exists and it is the original content.
    CHECK( directCount( dbPath, QStringLiteral( "run_metrics" ) ) == 1 );
    const auto kept = store.metricRecordForRun( QStringLiteral( "run-a" ) );
    REQUIRE( kept.has_value() );
    CHECK( kept->metrics.value( QStringLiteral( "overall_accuracy" ) ).toDouble()
           == 0.8 );
    CHECK( !store.metricRecordForRun( QStringLiteral( "run-b-txn" ) ).has_value() );
    CHECK( !store.metricRecordForRun( QStringLiteral( "run-c-txn" ) ).has_value() );
}

// O-txn-2 (WP-E): the lineage seam fails atomically under its injected
// commit fault — no edge, no partial row — and disarms cleanly so the retry
// lands exactly one edge (idempotent by edge identity).
TEST_CASE( "lineage commit fault leaves no partial edge and the store usable",
           "[experiment][txn][r4]" )
{
    using namespace sicnu::runtime::observability::fault;
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "exp.db" ) );
    ExperimentStore store;
    REQUIRE( store.open( dbPath ) );

    armFault( FaultAction{ "experiment_store.lineage_commit", Mode::NextN, 1, "" } );
    const auto refused = store.addLineageEdge(
        QStringLiteral( "run" ), QStringLiteral( "r1" ), QStringLiteral( "derived_from" ),
        QStringLiteral( "run" ), QStringLiteral( "r0" ) );
    CHECK( !refused.has_value() );
    CHECK( directCount( dbPath, QStringLiteral( "experiment_lineage" ) ) == 0 );
    CHECK( store.outgoingEdges( QStringLiteral( "run" ), QStringLiteral( "r1" ) ).isEmpty() );

    // Fault consumed: the retry succeeds and is idempotent (still one edge
    // after a second identical save).
    REQUIRE( store
                 .addLineageEdge( QStringLiteral( "run" ), QStringLiteral( "r1" ),
                                  QStringLiteral( "derived_from" ), QStringLiteral( "run" ),
                                  QStringLiteral( "r0" ) )
                 .has_value() );
    REQUIRE( store
                 .addLineageEdge( QStringLiteral( "run" ), QStringLiteral( "r1" ),
                                  QStringLiteral( "derived_from" ), QStringLiteral( "run" ),
                                  QStringLiteral( "r0" ) )
                 .has_value() );
    CHECK( directCount( dbPath, QStringLiteral( "experiment_lineage" ) ) == 1 );
}

// O-txn-3 (WP-E): after injected faults the REOPENED store is exactly the
// pre-fault state — the WAL never surfaces a half transaction (ADR 0137)
// and the store is usable for new writes.
TEST_CASE( "reopen after faults shows no half transaction",
           "[experiment][txn][r4]" )
{
    using namespace sicnu::runtime::observability::fault;
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "exp.db" ) );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-reopen" ) );
    experiment.setName( QStringLiteral( "reopen" ) );
    {
        ExperimentStore store;
        REQUIRE( store.open( dbPath ) );
        REQUIRE( store.upsertExperiment( experiment ).has_value() );
        REQUIRE( store.upsertRun( makeRun( QStringLiteral( "run-keep" ),
                                           QStringLiteral( "exp-reopen" ) ) )
                     .has_value() );

        // Runs batch: commit fault, consumed once.
        armFault( FaultAction{ "experiment_store.batch_commit", Mode::NextN, 1, "" } );
        QVector<ExperimentRun> batch;
        batch.append( makeRun( QStringLiteral( "run-batch" ), QStringLiteral( "exp-reopen" ) ) );
        batch.append( makeRun( QStringLiteral( "run-batch-2" ), QStringLiteral( "exp-reopen" ) ) );
        CHECK( !store.upsertRunsBatch( batch ).has_value() );
        CHECK( store.runCount() == 1 );
    }

    // Reopen: exactly the pre-fault truth, through a fresh connection.
    {
        ExperimentStore reopened;
        REQUIRE( reopened.open( dbPath ) );
        CHECK( reopened.runCount() == 1 );
        CHECK( directCount( dbPath, QStringLiteral( "experiment_runs" ) ) == 1 );
        CHECK( reopened.runById( QStringLiteral( "run-keep" ) ).has_value() );
        CHECK( !reopened.runById( QStringLiteral( "run-batch" ) ).has_value() );
        CHECK( !reopened.runById( QStringLiteral( "run-batch-2" ) ).has_value() );
        // The store is usable: a new write lands.
        REQUIRE( reopened.upsertRun( makeRun( QStringLiteral( "run-after" ),
                                              QStringLiteral( "exp-reopen" ) ) )
                     .has_value() );
        CHECK( reopened.runCount() == 2 );
    }
}
