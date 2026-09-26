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
#include "experiment/repeat_execution.h"

#include <QTemporaryDir>

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

    // Truth source: the persisted lineage rows themselves.
    const auto allEdges = store.outgoingEdges( QStringLiteral( "matrix" ),
                                               QStringLiteral( "c1" ), /*limit=*/16 );
    REQUIRE( allEdges.size() == 6 );

    const QStringList runs = ledger.runsForCell( QStringLiteral( "c1" ), /*limit=*/3 );
    REQUIRE( runs.size() == 3 );
    CHECK( runs == recorded );
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
