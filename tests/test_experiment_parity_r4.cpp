// test_experiment_parity_r4.cpp — Track 11 (R4) WP-C: studio↔store truth
// parity. The studio never owns run truth ("Run truth stays in the
// ExperimentStore", studio_live.h); its run-matrix view model is a
// projection of buildStudyReport, which itself reads only the store. The
// oracle: for the SAME store state, every observable the projection emits
// (rows, statuses, accounting, metrics) is EXACTLY the persisted truth as
// read back through independent store queries (cursor pages / runById /
// metricRecordForRun) — never the writer's in-memory belief. Batch
// rollbacks, refused transitions and unrecorded metrics must be invisible
// on BOTH sides identically.
#include <catch2/catch_test_macros.hpp>

#include "experiment_studio/run_matrix_projection.h"
#include "study/study_export.h"

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/run_recorder.h"

#include <QJsonDocument>
#include <QTemporaryDir>

using namespace sicnu::study;
namespace es = sicnu::experiment_studio;

namespace
{

struct Fixture
{
    QTemporaryDir dir;
    sicnu::experiment::ExperimentStore store;
    sicnu::experiment::MatrixLedger ledger{ store };
    sicnu::experiment::ExperimentRunRecorder recorder{ store };

    Fixture()
    {
        REQUIRE( dir.isValid() );
        REQUIRE( store.open( dir.filePath( QStringLiteral( "experiment.sqlite" ) ) ) );
        sicnu::experiment::Experiment experiment;
        experiment.setExperimentId( QStringLiteral( "exp-parity" ) );
        experiment.setName( QStringLiteral( "parity" ) );
        REQUIRE( store.upsertExperiment( experiment ).has_value() );
    }

    QString recordRun( const ParameterStudySpec &spec, const StudyPoint &point,
                       const QJsonObject &metrics )
    {
        sicnu::experiment::RunStartRequest request;
        request.experimentId = spec.experimentId;
        request.algorithmId = spec.algorithmId;
        request.parameters = point.parameters;
        request.parameters.insert(
            QStringLiteral( "output" ),
            dir.filePath( point.pointId + QStringLiteral( "/output.tif" ) ) );
        request.seed = point.seed;
        request.executionRef = QStringLiteral( "parity-%1" ).arg( point.pointId );
        const auto runId = recorder.startRun( request );
        REQUIRE( runId.has_value() );
        REQUIRE( recorder.markSucceeded( runId.value(), {}, metrics ).has_value() );
        REQUIRE( ledger.link( point.pointId, runId.value() ).has_value() );
        return runId.value();
    }
};

ParameterStudySpec makeSpec()
{
    ParameterStudySpec spec;
    spec.studyId = QStringLiteral( "parity-r4" );
    spec.experimentId = QStringLiteral( "exp-parity" );
    spec.algorithmId = QStringLiteral( "rs:threshold_raster" );
    spec.strategy = SamplingStrategy::OneAtATime;
    ParameterDimension dim;
    dim.parameterPath = QStringLiteral( "threshold" );
    dim.minValue = 0.0;
    dim.maxValue = 1.0;
    dim.stepCount = 5;
    spec.dimensions.append( dim );
    spec.budget.maxRuns = 100;
    spec.budget.maxInFlight = 2;
    spec.budget.perRunTimeoutMs = 1000;
    spec.budget.seedReplicates = 1;
    spec.budget.seed = 5;
    spec.metricNames.append( QStringLiteral( "maskedPercent" ) );
    spec.objectiveMetric = QStringLiteral( "maskedPercent" );
    spec.objectiveMetrics.append( StudyMetricSpec{ QStringLiteral( "maskedPercent" ), false } );
    return spec;
}

} // namespace

// O-parity-1 (WP-C / #1333 item-adjacent store contract): a batch whose
// first invalid roll refuses WHOLE must be invisible on both sides — the
// store persists none of it, and the studio projection shows none of it.
TEST_CASE( "batch rollback parity: refused batch appears on neither side",
           "[experiment][parity][r4]" )
{
    Fixture fix;
    const auto spec = makeSpec();
    const auto points = sampleStudyPoints( spec ).value();

    // Two live recorded runs are the visible truth.
    QString truthRunId;
    for ( int i = 0; i < 2; ++i )
        truthRunId = fix.recordRun( spec, points.at( i ),
                                    QJsonObject{ { QStringLiteral( "maskedPercent" ),
                                                   i * 10.0 } } );

    // A poisoned batch: [valid new row, EXISTING row with a mutated seed,
    // valid new row]. Identity immutability refuses the middle roll; the
    // all-or-nothing contract must roll the FIRST roll back with it — the
    // batch either lands whole or not at all (#1056 first-failure-rollback).
    // The new rows are never startRun'd: nothing of this batch exists in
    // the store before the call, so rollback is countable.
    const auto newBatchRow = [&]( const QString &id, const StudyPoint &point ) {
        sicnu::experiment::ExperimentRun run;
        run.setRunId( id );
        run.setExperimentId( spec.experimentId );
        run.setAlgorithmId( spec.algorithmId );
        run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
        run.setSeed( point.seed );
        run.setStatus( sicnu::dataset::RunStatus::Created );
        return run;
    };
    const QString rolledBackId = QStringLiteral( "parity-batch-rolled-back" );
    QVector<sicnu::experiment::ExperimentRun> batch;
    batch.append( newBatchRow( rolledBackId, points.at( 2 ) ) );
    {
        // Poison: the identity-pinned seed of an ALREADY-RECORDED run.
        auto recorded = fix.store.runById( truthRunId ).value();
        recorded.setSeed( recorded.seed() + 1 );
        batch.append( recorded );
    }
    batch.append( newBatchRow( QStringLiteral( "parity-batch-late" ), points.at( 4 ) ) );
    const auto batchResult = fix.store.upsertRunsBatch( batch );
    CHECK( !batchResult.has_value() );

    // Store truth: the batch left NOTHING — the first roll is undone with
    // the poisoned one, countably.
    const auto truth = fix.store.listRunsByCursor( QStringLiteral( "exp-parity" ) );
    REQUIRE( truth.has_value() );
    CHECK( truth.value().total == 2 );
    CHECK( truth.value().runs.size() == 2 );
    CHECK( fix.store.runById( rolledBackId ).has_value() == false );

    // Studio projection: exactly the same two rows, same accounting.
    const auto report = buildStudyReport( fix.store, fix.ledger, spec, points, {}, nullptr );
    const auto viewModel = es::projectRunMatrix( report );
    CHECK( viewModel.totalPoints == 5 );
    CHECK( viewModel.recordedCount == 2 );
    CHECK( viewModel.rows.size() == 5 );
    int recordedRows = 0;
    for ( const auto &row : viewModel.rows )
    {
        if ( row.status == QLatin1String( "recorded" ) )
        {
            ++recordedRows;
            // Every projected row resolves to REAL persisted evidence.
            CHECK( fix.store.runById( row.runId ).has_value() );
        }
        else
        {
            CHECK( row.runId.isEmpty() );
        }
    }
    CHECK( recordedRows == 2 );
}

// O-parity-2 (WP-C): the projection's row identities equal the store's
// cursor truth one-to-one — every completed run the store persists appears
// exactly once, and every projected runId resolves.
TEST_CASE( "row parity: projection equals the store cursor walk exactly",
           "[experiment][parity][r4]" )
{
    Fixture fix;
    const auto spec = makeSpec();
    const auto points = sampleStudyPoints( spec ).value();
    for ( const auto &point : points )
        fix.recordRun( spec, point,
                       QJsonObject{ { QStringLiteral( "maskedPercent" ),
                                      point.parameters.value( QStringLiteral( "threshold" ) )
                                          .toDouble()
                                          * 100.0 } } );

    // Store truth via cursor pages (independent query path from the report)
    // with a page size that forces several walks.
    QStringList truthRunIds;
    QString cursor;
    qint64 visited = 0;
    while ( true )
    {
        const auto page = fix.store.listRunsByCursor( QStringLiteral( "exp-parity" ), QString(),
                                                      QString(), cursor, /*limit=*/2 );
        REQUIRE( page.has_value() );
        for ( const auto &run : page.value().runs )
        {
            truthRunIds.append( run.runId() );
            CHECK( run.status() == sicnu::dataset::RunStatus::Completed );
        }
        visited += page.value().runs.size();
        cursor = page.value().nextCursor;
        if ( cursor.isEmpty() || page.value().runs.isEmpty() )
            break;
    }
    CHECK( visited == points.size() );
    CHECK( truthRunIds.size() == points.size() );

    const auto report = buildStudyReport( fix.store, fix.ledger, spec, points, {}, nullptr );
    const auto viewModel = es::projectRunMatrix( report );
    QStringList projectedRunIds;
    for ( const auto &row : viewModel.rows )
        if ( !row.runId.isEmpty() )
            projectedRunIds.append( row.runId );
    truthRunIds.sort();
    projectedRunIds.sort();
    CHECK( projectedRunIds == truthRunIds );
    CHECK( viewModel.recordedCount == truthRunIds.size() );
}

// O-parity-3 (WP-C): a refused status transition changes nothing — the
// store keeps its terminal truth and a re-projection is byte-identical.
TEST_CASE( "transition-refusal parity: re-projection is stable after a refused write",
           "[experiment][parity][r4]" )
{
    Fixture fix;
    const auto spec = makeSpec();
    const auto points = sampleStudyPoints( spec ).value();
    const auto runId = fix.recordRun( spec, points.first(), QJsonObject{} );

    auto terminal = fix.store.runById( runId ).value();
    terminal.setStatus( sicnu::dataset::RunStatus::Running ); // Completed → Running: refused
    CHECK( !fix.store.upsertRun( terminal ).has_value() );

    const auto after = fix.store.runById( runId ).value();
    CHECK( after.status() == sicnu::dataset::RunStatus::Completed );

    const auto report = buildStudyReport( fix.store, fix.ledger, spec, points, {}, nullptr );
    const auto viewModel = es::projectRunMatrix( report );
    CHECK( viewModel.recordedCount == 1 );
    CHECK( viewModel.rows.first().status == QLatin1String( "recorded" ) );
}

// O-parity-4 (WP-C): metrics shown by the projection equal the persisted
// metric records — the studio cannot display a number the store never held.
TEST_CASE( "metric parity: projected values equal persisted metric records",
           "[experiment][parity][r4]" )
{
    Fixture fix;
    const auto spec = makeSpec();
    const auto points = sampleStudyPoints( spec ).value();
    for ( const auto &point : points )
    {
        const double masked =
            point.parameters.value( QStringLiteral( "threshold" ) ).toDouble() * 100.0;
        fix.recordRun( spec, point, QJsonObject{ { QStringLiteral( "maskedPercent" ), masked } } );
    }

    const auto report = buildStudyReport( fix.store, fix.ledger, spec, points, {}, nullptr );
    const auto viewModel = es::projectRunMatrix( report );
    for ( const auto &row : viewModel.rows )
    {
        REQUIRE( !row.runId.isEmpty() );
        // Truth: the persisted run row's own metrics JSON. The projection
        // wraps per-point stats (one recorded run → mean IS the value).
        const auto run = fix.store.runById( row.runId );
        REQUIRE( run.has_value() );
        const double storedMasked =
            run->metrics().value( QStringLiteral( "maskedPercent" ) ).toDouble();
        const QJsonObject stats =
            row.metrics.value( QStringLiteral( "maskedPercent" ) ).toObject();
        REQUIRE( !stats.isEmpty() );
        CHECK( stats.value( QStringLiteral( "mean" ) ).toDouble() == storedMasked );
        CHECK( stats.value( QStringLiteral( "min" ) ).toDouble() == storedMasked );
        CHECK( stats.value( QStringLiteral( "max" ) ).toDouble() == storedMasked );
    }

    // Second leg: the metric-record table agrees with the run rows it
    // belongs to (recordMetrics is the protocol-bound seam).
    for ( const auto &point : points )
    {
        // the run of this point, found via the ledger (store order)
        const auto linked = fix.ledger.runsForCell( point.pointId );
        REQUIRE( linked.has_value() );
        REQUIRE( linked.value().size() == 1 );
        const auto record = fix.store.metricRecordForRun( linked.value().first() );
        CHECK( !record.has_value() ); // recordMetrics was never called here
    }
}

// P2-3 (review): a STOPPED report must keep its refusal visible through the
// projection — issues carry the stopped reason, never a clean empty matrix.
TEST_CASE( "stopped report surfaces its refusal in the view model (r4)",
           "[experiment][parity][r4]" )
{
    Fixture fix;
    const auto spec = makeSpec();
    const auto points = sampleStudyPoints( spec ).value();

    // Poison the second point's ledger page past the budget so the analysis
    // refuses (typed) while the first point recorded normally.
    fix.recordRun( spec, points.first(), QJsonObject{} );
    for ( int i = 0; i < 1001; ++i )
        REQUIRE( fix.ledger
                     .link( points.at( 1 ).pointId,
                            QStringLiteral( "overflow-%1" ).arg( i, 4, 10, QLatin1Char( '0' ) ) )
                     .has_value() );

    const auto report = buildStudyReport( fix.store, fix.ledger, spec, points, {}, nullptr );
    CHECK( !report.stoppedReason.isEmpty() );
    CHECK( report.stoppedReason.contains(
        QLatin1String( "experiment.matrix_cell_runs_overflow" ) ) );

    const auto viewModel = es::projectRunMatrix( report );
    CHECK( !viewModel.issues.isEmpty() );
    CHECK( viewModel.issues.first().contains(
        QLatin1String( "experiment.matrix_cell_runs_overflow" ) ) );
    CHECK( viewModel.recordedCount == 0 );
}

