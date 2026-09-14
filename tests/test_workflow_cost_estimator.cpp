// tests/test_workflow_cost_estimator.cpp — optimizer & cost estimator (D17 Package D)
//
// Ground truth: sha256sum-computed digests for fixed lineage strings,
// hand-counted DNE/CSE on the redundant fixture, and the analytic cost
// arithmetic evaluated by hand for the 1000x1000x4 pipeline.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "workflow/plan_optimizer.h"
#include "workflow/workflow_cost_estimator.h"

using namespace sicnu::workflow;

namespace {

NodeFact node( const QString &id, const QString &op, QJsonObject params = {} )
{
    NodeFact n;
    n.nodeId = id;
    n.operatorId = op;
    n.parameters = params;
    n.canvasPosition = QPointF( 0, 0 );
    // Wired nodes read one input; roots (nothing feeds them) have none.
    if ( id != QLatin1String( "input_raster" ) )
        n.inputPorts = { PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                   QStringLiteral( "None" ), 0, 0, 4, true } };
    n.outputPorts = { PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                                QStringLiteral( "None" ), 0, 0, 4, false } };
    return n;
}

EdgeFact edge( const QString &from, const QString &to, const QString &id = QString() )
{
    return EdgeFact{ id.isEmpty() ? QStringLiteral( "e_%1_%2" ).arg( from, to ) : id,
                     from, QStringLiteral( "output" ), to, QStringLiteral( "input" ) };
}

/// input -> {filter_a, filter_b (identical twins)} -> sink, plus two dead
/// branch nodes hanging off the input. Hand-counted: DNE prunes 2, CSE
/// merges 1, optimized node count = original - 3.
WorkflowDefinition redundantWorkflow()
{
    WorkflowDefinition def;
    def.nodes = {
        node( "input_raster", "rs:import_raster" ),
        node( "filter_a", "rs:spatial_filter", QJsonObject{ { "kernel", QStringLiteral( "gaussian" ) } } ),
        node( "filter_b", "rs:spatial_filter", QJsonObject{ { "kernel", QStringLiteral( "gaussian" ) } } ),
        node( "sink_ndvi", "rs:spectral_index", QJsonObject{ { "index", QStringLiteral( "NDVI" ) } } ),
        node( "dead_branch_1", "rs:threshold", QJsonObject{ { "threshold", 0.2 } } ),
        node( "dead_branch_2", "rs:recode", QJsonObject{ { "table", 1 } } ),
    };
    def.edges = {
        edge( "input_raster", "filter_a" ),
        edge( "input_raster", "filter_b" ),
        edge( "filter_a", "sink_ndvi" ),
        edge( "filter_b", "sink_ndvi" ),
        edge( "input_raster", "dead_branch_1" ),
        edge( "dead_branch_1", "dead_branch_2" ),
    };
    return def;
}

} // namespace

TEST_CASE( "Node signature matches an externally precomputed SHA-256", "[d17][workflow][optimizer]" )
{
    // Lineage string for a parentless node with empty params is
    // "rs:noop\x1f{}" — its digest was computed with sha256sum.
    const NodeFact lonely = node( "n", "rs:noop" );
    const QString signature = WorkflowPlanOptimizer::computeNodeSignature( lonely, {} );
    REQUIRE( signature == QStringLiteral( "4b352b75d29277e84a9a6429cf6d2cc56afde9b989659faa022667df9660f664" ) );

    // A second pinned digest: "rs:spectral_index\x1f{\"index\":\"NDVI\"}".
    const NodeFact ndvi = node( "n", "rs:spectral_index", QJsonObject{ { "index", QStringLiteral( "NDVI" ) } } );
    REQUIRE( WorkflowPlanOptimizer::computeNodeSignature( ndvi, {} )
             == QStringLiteral( "8469560275df38c4673bcec6ca9b6b858b18f794d81f57def0170fe1a5206e0d" ) );
}

TEST_CASE( "Signatures are deterministic, parameter-sensitive and parent-order-insensitive", "[d17][workflow][optimizer]" )
{
    const NodeFact n = node( "child", "rs:spatial_filter", QJsonObject{ { "kernel", QStringLiteral( "mean" ) } } );

    QMap<QString, QString> parentsA;
    parentsA.insert( QStringLiteral( "pa" ), QStringLiteral( "aaaa" ) );
    parentsA.insert( QStringLiteral( "pb" ), QStringLiteral( "bbbb" ) );
    QMap<QString, QString> parentsB;
    parentsB.insert( QStringLiteral( "pb" ), QStringLiteral( "bbbb" ) );
    parentsB.insert( QStringLiteral( "pa" ), QStringLiteral( "aaaa" ) );

    REQUIRE( WorkflowPlanOptimizer::computeNodeSignature( n, parentsA )
             == WorkflowPlanOptimizer::computeNodeSignature( n, parentsB ) );
    REQUIRE( WorkflowPlanOptimizer::computeNodeSignature( n, parentsA )
             != WorkflowPlanOptimizer::computeNodeSignature( n, {} ) );

    // Different parameters -> different signature.
    NodeFact other = n;
    other.parameters = QJsonObject{ { "kernel", QStringLiteral( "sobel" ) } };
    REQUIRE( WorkflowPlanOptimizer::computeNodeSignature( other, parentsA )
             != WorkflowPlanOptimizer::computeNodeSignature( n, parentsA ) );

    // Parent value changes propagate.
    parentsA[QStringLiteral( "pa" )] = QStringLiteral( "cccc" );
    REQUIRE( WorkflowPlanOptimizer::computeNodeSignature( n, parentsA )
             != WorkflowPlanOptimizer::computeNodeSignature( n, parentsB ) );
}

TEST_CASE( "Identical twins share one lineage signature through the parent", "[d17][workflow][optimizer]" )
{
    const WorkflowDefinition def = redundantWorkflow();
    const QMap<QString, QString> signatures = WorkflowPlanOptimizer::computeLineageSignatures( def );
    REQUIRE( signatures.size() == def.nodes.size() );
    REQUIRE( signatures.value( QStringLiteral( "filter_a" ) )
             == signatures.value( QStringLiteral( "filter_b" ) ) );
    // The NDVI sink consumes both twins -> its own signature exists and differs.
    REQUIRE( signatures.value( QStringLiteral( "sink_ndvi" ) )
             != signatures.value( QStringLiteral( "filter_a" ) ) );
}

TEST_CASE( "Dead node elimination and CSE on the redundant fixture", "[d17][workflow][optimizer]" )
{
    const WorkflowDefinition unoptimized = redundantWorkflow();
    OptimizationReport report;
    const WorkflowDefinition optimized = WorkflowPlanOptimizer::optimizePlan(
        unoptimized, QSet<QString>{ QStringLiteral( "sink_ndvi" ) }, &report );

    REQUIRE( report.deadNodesPruned == 2 );
    REQUIRE( report.commonSubexpressionsMerged == 1 );
    REQUIRE( optimized.nodes.size() == unoptimized.nodes.size() - 3 );
    REQUIRE( optimized.findNode( "filter_b" ) == nullptr );
    REQUIRE( optimized.findNode( "dead_branch_1" ) == nullptr );
    REQUIRE( optimized.findNode( "dead_branch_2" ) == nullptr );
    REQUIRE( optimized.findNode( "filter_a" ) != nullptr );
    REQUIRE( optimized.findNode( "sink_ndvi" ) != nullptr );

    // The sink still receives exactly one edge (via the canonical twin).
    int sinkEdges = 0;
    for ( const EdgeFact &e : optimized.edges )
        if ( e.targetNodeId == QLatin1String( "sink_ndvi" ) )
            ++sinkEdges;
    REQUIRE( sinkEdges == 1 );

    // Structural validity of the optimized document.
    QString error;
    REQUIRE( WorkflowIR::validateSemantics( optimized, &error ) );
    INFO( error.toStdString() );

    // Deterministic: optimize twice, identical graphs.
    const WorkflowDefinition again = WorkflowPlanOptimizer::optimizePlan(
        unoptimized, QSet<QString>{ QStringLiteral( "sink_ndvi" ) } );
    REQUIRE( again == optimized );
}

TEST_CASE( "Optimize is a no-op when every node feeds the sink", "[d17][workflow][optimizer]" )
{
    WorkflowDefinition def;
    def.nodes = { node( "a", "rs:import_raster" ), node( "b", "rs:threshold" ) };
    def.edges = { edge( "a", "b" ) };

    OptimizationReport report;
    const WorkflowDefinition optimized = WorkflowPlanOptimizer::optimizePlan(
        def, QSet<QString>{ QStringLiteral( "b" ) }, &report );
    REQUIRE( report.deadNodesPruned == 0 );
    REQUIRE( report.commonSubexpressionsMerged == 0 );
    REQUIRE( optimized == def );
}

TEST_CASE( "Cache hits are reported and keep the node unmerged", "[d17][workflow][optimizer]" )
{
    const WorkflowDefinition def = redundantWorkflow();
    const QMap<QString, QString> signatures = WorkflowPlanOptimizer::computeLineageSignatures( def );

    OptimizationReport report;
    const WorkflowDefinition optimized = WorkflowPlanOptimizer::optimizePlan(
        def, QSet<QString>{ QStringLiteral( "sink_ndvi" ) }, &report,
        QSet<QString>{ signatures.value( QStringLiteral( "filter_a" ) ) } );

    // Both twins carry the cached signature and both stay (a cached
    // signature is never merged away — its artifact is already on disk).
    REQUIRE( report.cachedNodesHit.size() == 2 );
    REQUIRE( report.cachedNodesHit.contains( QStringLiteral( "filter_a" ) ) );
    REQUIRE( report.cachedNodesHit.contains( QStringLiteral( "filter_b" ) ) );
    REQUIRE( report.commonSubexpressionsMerged == 0 );
    REQUIRE( optimized.findNode( "filter_b" ) != nullptr );
}

TEST_CASE( "Flops and peak RSS on the 1000x1000x4 analytic pipeline", "[d17][workflow][cost]" )
{
    // input -> calibration -> index. Hand arithmetic:
    //   working set per node = 1000*1000*4 px * 4 B = 16 MiB
    //   widest tier = 1 -> PeakRSS = 16 MiB + 64 MiB overhead = 80 MiB
    WorkflowDefinition def;
    def.nodes = {
        node( "input_raster", "rs:import_raster" ),
        node( "calib", "rs:radiometric_calibration" ),
        node( "index", "rs:spectral_index" ),
    };
    def.edges = { edge( "input_raster", "calib" ), edge( "calib", "index" ) };

    QMap<QString, QSize> dims;
    dims.insert( QStringLiteral( "input_raster" ), QSize( 1000, 1000 ) );
    dims.insert( QStringLiteral( "calib" ), QSize( 1000, 1000 ) );
    dims.insert( QStringLiteral( "index" ), QSize( 1000, 1000 ) );

    const CostEstimate cost = WorkflowCostEstimator::estimatePipelineCost( def, dims );

    // Working set per tier node = 1000*1000 px * 4 bands * 4 B = 16,000,000
    // bytes (decimal — the grid is 1000, not 1024). Widest tier = 1 node.
    const qint64 expectedPeak = qint64( 1000 ) * 1000 * 4 * 4
        + WorkflowCostEstimator::kBaseEngineOverheadBytes;
    REQUIRE( cost.peakRssBytes == expectedPeak );

    // Flops: per node W*H*bands*K(op) = 1000*1000*4*K; K = 0.5, 1.0, 2.0.
    const double expectedFlops = 1000.0 * 1000.0 * 4.0 * ( 0.5 + 1.0 + 2.0 );
    REQUIRE_THAT( cost.totalFlops, Catch::Matchers::WithinRel( expectedFlops, 1e-9 ) );

    // Duration is totalFlops / 200e6, by the pinned constant.
    REQUIRE_THAT( cost.estimatedDurationSeconds,
                  Catch::Matchers::WithinRel( expectedFlops / 200.0e6, 1e-9 ) );

    // 80 MiB is far below the waterline; the chain is serial (tier width 1),
    // so the recommendation equals the tier width.
    REQUIRE( cost.recommendedMaxParallelism == 1 );
}

TEST_CASE( "Oversized working sets degrade recommended parallelism to 1", "[d17][workflow][cost]" )
{
    // Two independent branches -> tier width 2; each raster is 40000x40000
    // with a 4-band output, so the tier working set (2 x 40000*40000*4b*4B
    // = 47.8 GiB) crosses the 70 % waterline of this 62 GB host. Pure
    // arithmetic: nothing is allocated.
    WorkflowDefinition def;
    def.nodes = {
        node( "input_raster", "rs:import_raster" ),
        node( "branch_a", "rs:spatial_filter" ),
        node( "branch_b", "rs:spatial_filter" ),
    };
    def.edges = { edge( "input_raster", "branch_a" ), edge( "input_raster", "branch_b" ) };

    QMap<QString, QSize> dims;
    dims.insert( QStringLiteral( "branch_a" ), QSize( 40000, 40000 ) );
    dims.insert( QStringLiteral( "branch_b" ), QSize( 40000, 40000 ) );

    const CostEstimate cost = WorkflowCostEstimator::estimatePipelineCost( def, dims );
    const qint64 working = 2LL * qint64( 40000 ) * 40000 * 4 * WorkflowCostEstimator::kBytesPerPixel;
    REQUIRE( cost.peakRssBytes == working + WorkflowCostEstimator::kBaseEngineOverheadBytes );
    REQUIRE( cost.recommendedMaxParallelism == 1 );
}

TEST_CASE( "Unknown operators default to complexity 1.0", "[d17][workflow][cost]" )
{
    REQUIRE( WorkflowCostEstimator::operatorComplexity( QStringLiteral( "rs:totally_unknown" ) ) == 1.0 );
    REQUIRE( WorkflowCostEstimator::operatorComplexity( QStringLiteral( "rs:spatial_filter" ) ) == 9.0 );
}
