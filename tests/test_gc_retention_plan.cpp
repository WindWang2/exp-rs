// test_gc_retention_plan.cpp — 12.0 GC/retention Oracle (O4):
// dry-run plans and executed deletions agree EXACTLY, and every asset that
// is still reachable from a dataset, a result, a run or downstream lineage
// survives. The graph fixture exercises every relationship family the
// removeAsset guard protects.
#include <catch2/catch_test_macros.hpp>

#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"
#include "data/governance/workspace_lifecycle.h"
#include "data/governance/workspace_service.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>

using namespace sicnu::workspace;
using namespace sicnu::experiment;

namespace
{

GovernedAsset makeAsset( const QString &id, const QString &canonicalSource )
{
    GovernedAsset asset;
    asset.assetId = id;
    asset.canonicalSource = canonicalSource;
    asset.kind = QStringLiteral( "raster" );
    asset.state = QStringLiteral( "Ready" );
    asset.persistence = QStringLiteral( "project" );
    asset.displayName = id;
    asset.revision = 1;
    asset.availability = QStringLiteral( "unverified" );
    return asset;
}

GovernanceStore::LineageEdge makeEdge( const QString &out, const QString &in )
{
    GovernanceStore::LineageEdge edge;
    edge.outputAssetId = out;
    edge.inputAssetId = in;
    edge.operatorId = QStringLiteral( "rs:test" );
    return edge;
}

ExperimentRun makeRun( const QString &runId, const QString &experimentId, quint64 seed )
{
    ExperimentRun run;
    run.setRunId( runId );
    run.setExperimentId( experimentId );
    run.setAlgorithmId( QStringLiteral( "rs:classify" ) );
    run.setAlgorithmVersion( QStringLiteral( "1.0" ) );
    run.setDatasetVersionId( QStringLiteral( "11111111-1111-4111-8111-111111111111" ) );
    run.setSeed( seed );
    run.setStatus( RunStatus::Created );
    return run;
}

} // namespace

TEST_CASE( "governance cleanup: dry-run equals execution and reachability is "
           "preserved (graph fixture)",
           "[gc][governance][oracle]" )
{
    QTemporaryDir dir;
    const QString payloadDir = dir.filePath( QStringLiteral( "payload" ) );
    QDir().mkpath( payloadDir );

    WorkspaceService service;
    REQUIRE( service.openStore( dir.filePath( QStringLiteral( "gov.db" ) ) ) );

    // --- graph fixture -------------------------------------------------------
    // raw(payload) → inter(payload MISSING) → final(payload); final is a
    // dataset member AND a run output; inter is a result input; orphan has
    // no payload and no references.
    const QString rawFile = payloadDir + QStringLiteral( "/raw.tif" );
    const QString finalFile = payloadDir + QStringLiteral( "/final.tif" );
    REQUIRE( QFile( rawFile ).open( QIODevice::WriteOnly ) );
    REQUIRE( QFile( finalFile ).open( QIODevice::WriteOnly ) );

    GovernanceStore &store = service.store();
    REQUIRE( store.upsertAsset( makeAsset( QStringLiteral( "raw" ), rawFile ) ).operator bool() );
    // inter's payload is gone (never created).
    REQUIRE( store.upsertAsset( makeAsset( QStringLiteral( "inter" ),
                                           payloadDir + QStringLiteral( "/inter.tif" ) ) )
                 .operator bool() );
    REQUIRE( store.upsertAsset( makeAsset( QStringLiteral( "final" ), finalFile ) ).operator bool() );
    REQUIRE( store
                 .upsertAsset( makeAsset( QStringLiteral( "orphan" ),
                                          payloadDir + QStringLiteral( "/orphan.tif" ) ) )
                 .operator bool() );

    REQUIRE( store.addLineageEdges(
                 { makeEdge( QStringLiteral( "inter" ), QStringLiteral( "raw" ) ),
                   makeEdge( QStringLiteral( "final" ), QStringLiteral( "inter" ) ) } )
                 .operator bool() );

    DatasetRecord dataset;
    dataset.id = DatasetId::generate();
    dataset.kind = DatasetKind::Training;
    dataset.header.name = QStringLiteral( "fixture-dataset" );
    dataset.memberAssetIds = QStringList{ QStringLiteral( "final" ) };
    REQUIRE( store.upsertDataset( dataset ).operator bool() );

    ResultRecord result;
    result.id = ResultId::generate();
    result.semanticType = ResultSemanticType::Classification;
    result.header.name = QStringLiteral( "fixture-result" );
    ResultInput input;
    input.assetId = QStringLiteral( "inter" );
    result.inputs.append( input );
    REQUIRE( store.upsertResult( result ).operator bool() );

    RunRecord run;
    run.id = QStringLiteral( "fixture-run" );
    run.workflowId = QStringLiteral( "wf" );
    run.outputAssetIds.append( QStringLiteral( "final" ) );
    REQUIRE( store.upsertRun( run ).operator bool() );
    REQUIRE( store.linkRunOutput( QStringLiteral( "fixture-run" ),
                                  QStringLiteral( "final" ) )
                 .operator bool() );

    // --- plan (dry run) -------------------------------------------------------
    CleanupService cleanup( service );
    CleanupReport plan = cleanup.plan();

    // inter (missing payload, referenced by the result) is PROTECTED, not a
    // removal candidate; orphan is the only removable row.
    const int orphanCandidates = std::count_if(
        plan.candidates.cbegin(), plan.candidates.cend(), []( const GovernanceDiagnostic &d )
        { return d.code == QLatin1String( "cleanup.orphan_row" ); } );
    CHECK( orphanCandidates == 1 );
    bool interProtected = false;
    for ( const GovernanceDiagnostic &d : plan.candidates )
        interProtected |= d.code == QLatin1String( "cleanup.protected_missing" )
                          && d.entityId == QLatin1String( "inter" );
    CHECK( interProtected );

    // Guard cross-check: the store refuses the same rows the plan protects.
    const auto guarded = store.removeAsset( QStringLiteral( "inter" ) );
    CHECK_FALSE( guarded.has_value() );
    CHECK( guarded.diagnostics().first().code == QLatin1String( "store.asset_referenced" ) );

    // --- execute, then prove plan ≡ execute ------------------------------------
    const QVector<QString> assetsBefore = store.assetIds();
    const int removed = plan.execute( service );
    CHECK( removed == orphanCandidates );

    const QSet<QString> before( assetsBefore.cbegin(), assetsBefore.cend() );
    const QVector<QString> assetsAfter = store.assetIds();
    QSet<QString> expected( before );
    for ( const GovernanceDiagnostic &d : plan.candidates )
        if ( d.code == QLatin1String( "cleanup.orphan_row" ) )
            expected.remove( d.entityId );
    // Executed deletions == dry-run plan: what remains is exactly
    // "before minus the planned removals".
    CHECK( QSet<QString>( assetsAfter.cbegin(), assetsAfter.cend() ) == expected );

    // The executed set is exactly the dry-run set: re-planning finds nothing.
    CleanupReport afterPlan = cleanup.plan();
    const int orphanAfter = std::count_if(
        afterPlan.candidates.cbegin(), afterPlan.candidates.cend(),
        []( const GovernanceDiagnostic &d )
        { return d.code == QLatin1String( "cleanup.orphan_row" ); } );
    CHECK( orphanAfter == 0 );

    // Every REACHABLE asset survived: raw (lineage root), inter (result
    // input + downstream lineage), final (dataset member + run output).
    CHECK( store.assetById( QStringLiteral( "raw" ) ).has_value() );
    CHECK( store.assetById( QStringLiteral( "inter" ) ).has_value() );
    CHECK( store.assetById( QStringLiteral( "final" ) ).has_value() );
    CHECK( store.datasetById( dataset.id.toString() )->memberAssetIds.size() == 1 );
    CHECK( store.runById( QStringLiteral( "fixture-run" ) )->outputAssetIds.size() == 1 );
    service.closeStore();
}

TEST_CASE( "experiment run retention: prune plan equals executed deletions; "
           "promoted and lineage-linked runs survive",
           "[gc][experiment][oracle]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId(
        sicnu::experiment::ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "retention" ) );
    experiment.runIds() = QStringList{ QStringLiteral( "twin-a" ), QStringLiteral( "twin-b" ),
                                       QStringLiteral( "promoted-run" ),
                                       QStringLiteral( "linked-run" ) };
    REQUIRE( store.upsertExperiment( experiment ).has_value() );

    // twin-a / twin-b share ONE identity; twin-b is the newer keeper.
    ExperimentRun twinA = makeRun( QStringLiteral( "twin-a" ), experiment.experimentId(), 7 );
    ExperimentRun twinB = twinA;
    twinB.setRunId( QStringLiteral( "twin-b" ) );
    REQUIRE( store.upsertRunsBatch( { twinA, twinB } ).has_value() );

    ExperimentRun promoted = makeRun( QStringLiteral( "promoted-run" ),
                                      experiment.experimentId(), 8 );
    REQUIRE( store.upsertRunsBatch( { promoted } ).has_value() );
    PromotionRecord promotion;
    promotion.promotionId = QStringLiteral( "promo-1" );
    promotion.runId = QStringLiteral( "promoted-run" );
    promotion.verdict = QStringLiteral( "eligible" );
    promotion.createdAtUtc = QDateTime::currentDateTimeUtc();
    REQUIRE( store.savePromotionRecord( promotion ).has_value() );

    ExperimentRun linked = makeRun( QStringLiteral( "linked-run" ), experiment.experimentId(), 9 );
    REQUIRE( store.upsertRunsBatch( { linked } ).has_value() );
    REQUIRE( store
                 .addLineageEdge( QStringLiteral( "run" ), QStringLiteral( "linked-run" ),
                                  QStringLiteral( "derived_from" ), QStringLiteral( "run" ),
                                  QStringLiteral( "twin-b" ) )
                 .has_value() );

    ExperimentStore::RunPrunePolicy policy;
    policy.collapseIdentityTwins = true;
    const auto plan = store.planRunPrune( policy );
    REQUIRE( plan.has_value() );
    CHECK( plan->scannedRuns == 4 );
    CHECK( plan->runIds == QStringList{ QStringLiteral( "twin-a" ) } );

    // Dry-run twice is stable (plan is a function of store state).
    const auto planAgain = store.planRunPrune( policy );
    REQUIRE( planAgain.has_value() );
    CHECK( planAgain->runIds == plan->runIds );

    // Execute removes exactly the plan, with no phantom leftovers.
    const auto removed = store.executeRunPrune( *plan );
    REQUIRE( removed.has_value() );
    CHECK( removed.value() == 1 );
    CHECK( !store.runById( QStringLiteral( "twin-a" ) ).has_value() );
    CHECK( store.runById( QStringLiteral( "twin-b" ) ).has_value() );
    CHECK( store.runById( QStringLiteral( "promoted-run" ) ).has_value() );
    CHECK( store.runById( QStringLiteral( "linked-run" ) ).has_value() );
    // The experiment's run_id list no longer cites the pruned run.
    const auto updated = store.experimentById( experiment.experimentId() );
    REQUIRE( updated.has_value() );
    CHECK( !updated->runIds().contains( QStringLiteral( "twin-a" ) ) );
    CHECK( updated->runIds().size() == 3 );

    // plan ≡ execute: re-planning with the same policy finds nothing more.
    const auto emptyPlan = store.planRunPrune( policy );
    REQUIRE( emptyPlan.has_value() );
    CHECK( emptyPlan->runIds.isEmpty() );

    // Stale-plan shrink: a plan built before a run becomes protected never
    // over-deletes. linked-run is planned for removal; promotion evidence
    // created BEFORE execution protects it (the guard re-derives at
    // execution time), so the executed set is smaller than the plan.
    ExperimentStore::RunPrunePolicy ignoreLineage;
    ignoreLineage.keepWithRunLineage = false;
    auto stalePlan = store.planRunPrune( ignoreLineage );
    REQUIRE( stalePlan.has_value() );
    CHECK( stalePlan->runIds.contains( QStringLiteral( "linked-run" ) ) );
    PromotionRecord latePromotion;
    latePromotion.promotionId = QStringLiteral( "promo-2" );
    latePromotion.runId = QStringLiteral( "linked-run" );
    latePromotion.verdict = QStringLiteral( "eligible" );
    latePromotion.createdAtUtc = QDateTime::currentDateTimeUtc();
    REQUIRE( store.savePromotionRecord( latePromotion ).has_value() );
    const auto shrunk = store.executeRunPrune( *stalePlan );
    REQUIRE( shrunk.has_value() );
    CHECK( shrunk.value() == 1 ); // twin-b only: linked-run grew protection
    CHECK( store.runById( QStringLiteral( "linked-run" ) ).has_value() );
    CHECK( !store.runById( QStringLiteral( "twin-b" ) ).has_value() );
}

TEST_CASE( "experiment run retention: benchmark-cited runs are not pruned (#1173)",
           "[gc][experiment][oracle][issue1173]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "exp-bench.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId(
        sicnu::experiment::ExperimentId::generate().toString() );
    experiment.setName( QStringLiteral( "bench-cite" ) );
    experiment.runIds() = QStringList{ QStringLiteral( "cited-run" ),
                                       QStringLiteral( "free-run" ) };
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
    REQUIRE( store
                 .upsertRunsBatch( { makeRun( QStringLiteral( "cited-run" ),
                                              experiment.experimentId(), 1 ),
                                     makeRun( QStringLiteral( "free-run" ),
                                              experiment.experimentId(), 2 ) } )
                 .has_value() );

    BenchmarkResult result;
    result.setResultId( QStringLiteral( "br-1" ) );
    result.setBenchmarkId( QStringLiteral( "bench-a" ) );
    result.setBenchmarkVersion( 1 );
    result.setExperimentRunId( QStringLiteral( "cited-run" ) );
    REQUIRE( store.saveBenchmarkResult( result ).has_value() );

    ExperimentStore::RunPrunePolicy policy;
    policy.collapseIdentityTwins = false;
    const auto plan = store.planRunPrune( policy );
    REQUIRE( plan.has_value() );
    CHECK( plan->runIds == QStringList{ QStringLiteral( "free-run" ) } );
    CHECK( !plan->runIds.contains( QStringLiteral( "cited-run" ) ) );

    REQUIRE( store.executeRunPrune( *plan ).has_value() );
    CHECK( store.runById( QStringLiteral( "cited-run" ) ).has_value() );
    CHECK( !store.runById( QStringLiteral( "free-run" ) ).has_value() );
    CHECK( store.benchmarkResultById( QStringLiteral( "br-1" ) ).has_value() );
}
