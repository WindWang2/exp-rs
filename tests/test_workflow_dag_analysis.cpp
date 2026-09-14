// tests/test_workflow_dag_analysis.cpp — DAG analyzer contract tests (D17 Package B)
//
// Ground truth: hand-derived tier partitions (diamond: T0={S} T1={A,B}
// T2={M}, C_max=2), a hand-traced 3-node loop [N1,N2,N3,N1], and a
// synthetic 10x10 grid whose tier sizes are known by construction.
#include <catch2/catch_test_macros.hpp>
#include <QSet>

#include <chrono>

#include "workflow/workflow_dag_analyzer.h"

using sicnu::workflow::ConcurrencyTier;
using sicnu::workflow::DagAnalysisResult;
using sicnu::workflow::EdgeFact;
using sicnu::workflow::NodeFact;
using sicnu::workflow::PortFact;
using sicnu::workflow::WorkflowDagAnalyzer;
using sicnu::workflow::WorkflowDefinition;

namespace {

PortFact outPort()
{
    return PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                     QStringLiteral( "None" ), 0, 0, 0, false };
}

PortFact inPort( const QString &name )
{
    return PortFact{ name, QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                     QStringLiteral( "None" ), 0, 0, 0, true };
}

/// Builds a graph from (nodes, edges) given as id pairs — the test-side
/// hand model; nothing in the analyzer is reused to build it.
WorkflowDefinition graph( const QStringList &nodeIds, const QList<QPair<QString, QString>> &edges,
                          const QStringList &extraPorts = {} )
{
    WorkflowDefinition def;
    int index = 0;
    for ( const QString &id : nodeIds )
    {
        NodeFact node;
        node.nodeId = id;
        node.operatorId = QStringLiteral( "rs:noop" );
        node.canvasPosition = QPointF( ( index % 4 ) * 280.0, ( index / 4 ) * 140.0 );
        node.outputPorts = { outPort() };
        for ( const QString &extra : extraPorts )
            node.inputPorts.append( inPort( extra ) );
        if ( node.inputPorts.isEmpty() )
            node.inputPorts = { inPort( QStringLiteral( "input" ) ) };
        def.nodes.append( node );
        ++index;
    }
    int edgeIndex = 0;
    for ( const auto &[from, to] : edges )
    {
        def.edges.append( EdgeFact{ QStringLiteral( "e%1" ).arg( ++edgeIndex ), from,
                                    QStringLiteral( "output" ), to, QStringLiteral( "input" ) } );
    }
    return def;
}

} // namespace

TEST_CASE( "Kahn tiers on the hand-analyzed diamond DAG", "[d17][workflow][dag]" )
{
    // S -> A, S -> B, A -> M, B -> M: T0={node_src}, T1={node_a,node_b}, T2={node_sink}.
    const WorkflowDefinition def = graph(
        { QStringLiteral( "node_src" ), QStringLiteral( "node_a" ), QStringLiteral( "node_b" ), QStringLiteral( "node_sink" ) },
        { { "node_src", "node_a" }, { "node_src", "node_b" }, { "node_a", "node_sink" }, { "node_b", "node_sink" } } );

    const DagAnalysisResult result = WorkflowDagAnalyzer::analyzeDag( def );

    REQUIRE( result.isAcyclic );
    REQUIRE( result.executionTiers.size() == 3 );
    REQUIRE( result.executionTiers[0].nodeIds == QVector<QString>{ QStringLiteral( "node_src" ) } );
    REQUIRE( result.executionTiers[1].nodeIds.size() == 2 );
    REQUIRE( result.executionTiers[1].nodeIds.contains( QStringLiteral( "node_a" ) ) );
    REQUIRE( result.executionTiers[1].nodeIds.contains( QStringLiteral( "node_b" ) ) );
    REQUIRE( result.executionTiers[2].nodeIds == QVector<QString>{ QStringLiteral( "node_sink" ) } );
    REQUIRE( WorkflowDagAnalyzer::calculateMaxParallelism( result.executionTiers ) == 2 );

    // Linear schedule: every edge points strictly forward in the schedule.
    QHash<QString, int> position;
    for ( int i = 0; i < result.linearSchedule.size(); ++i )
        position.insert( result.linearSchedule[i], i );
    for ( const EdgeFact &edge : def.edges )
        REQUIRE( position[edge.sourceNodeId] < position[edge.targetNodeId] );
}

TEST_CASE( "Linear chain yields one node per tier in chain order", "[d17][workflow][dag]" )
{
    const WorkflowDefinition def = graph(
        { "n1", "n2", "n3", "n4" },
        { { "n1", "n2" }, { "n2", "n3" }, { "n3", "n4" } } );

    const DagAnalysisResult result = WorkflowDagAnalyzer::analyzeDag( def );
    REQUIRE( result.isAcyclic );
    REQUIRE( result.executionTiers.size() == 4 );
    REQUIRE( result.linearSchedule == ( QVector<QString>{ "n1", "n2", "n3", "n4" } ) );
    REQUIRE( WorkflowDagAnalyzer::calculateMaxParallelism( result.executionTiers ) == 1 );
}

TEST_CASE( "DFS cycle diagnosis closes the hand-traced 3-node loop", "[d17][workflow][dag]" )
{
    // N1 -> N2 -> N3 -> N1, plus downstream N3 -> N4 (outside the loop).
    const WorkflowDefinition def = graph(
        { "N1", "N2", "N3", "N4" },
        { { "N1", "N2" }, { "N2", "N3" }, { "N3", "N1" }, { "N3", "N4" } } );

    const auto started = std::chrono::high_resolution_clock::now();
    const DagAnalysisResult result = WorkflowDagAnalyzer::analyzeDag( def );
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::high_resolution_clock::now() - started )
                               .count();

    REQUIRE_FALSE( result.isAcyclic );
    REQUIRE( result.cyclePath.size() == 4 );
    REQUIRE( result.cyclePath.first() == result.cyclePath.last() );
    // The loop members in order, independent of the DFS start point:
    QSet<QString> loopMembers( result.cyclePath.cbegin(), result.cyclePath.cend() - 1 );
    REQUIRE( loopMembers == QSet<QString>{ QStringLiteral( "N1" ), QStringLiteral( "N2" ), QStringLiteral( "N3" ) } );
    // The downstream node must not be reported inside the cycle.
    REQUIRE_FALSE( loopMembers.contains( QStringLiteral( "N4" ) ) );
    REQUIRE( result.errorMessage.contains( QStringLiteral( "cycle" ), Qt::CaseInsensitive ) );
    REQUIRE( result.executionTiers.isEmpty() );
    REQUIRE( elapsedUs < 1000 ); // sub-millisecond fail-fast on a 4-node graph
}

TEST_CASE( "Reported cycle path is a real closed walk of the graph", "[d17][workflow][dag]" )
{
    // Diamond pushes + a cycle: a->b, a->c, c->d, d->b, b->a. The 3-color
    // DFS may gray b via a and re-reach it via d; the extracted path must
    // still be a walk whose every consecutive pair is an edge (P1 pin).
    const WorkflowDefinition def = graph(
        { "a", "b", "c", "d" },
        { { "a", "b" }, { "a", "c" }, { "c", "d" }, { "d", "b" }, { "b", "a" } } );

    QVector<QString> cycle;
    REQUIRE_FALSE( WorkflowDagAnalyzer::detectCycleDFS( def, cycle ) );
    REQUIRE( cycle.size() >= 2 );
    REQUIRE( cycle.first() == cycle.last() );
    QHash<QPair<QString, QString>, bool> edges;
    for ( const EdgeFact &e : def.edges )
        edges.insert( qMakePair( e.sourceNodeId, e.targetNodeId ), true );
    for ( int i = 1; i < cycle.size(); ++i )
    {
        INFO( "step " << cycle[i - 1].toStdString() << " -> " << cycle[i].toStdString() );
        REQUIRE( edges.value( qMakePair( cycle[i - 1], cycle[i] ), false ) );
    }
}

TEST_CASE( "Self-loop is diagnosed as a one-node cycle", "[d17][workflow][dag]" )
{
    WorkflowDefinition def = graph( { "solo" }, {} );
    // Manual self edge solo -> solo (graph() only emits distinct pairs).
    def.edges.append( EdgeFact{ QStringLiteral( "self" ), QStringLiteral( "solo" ),
                                QStringLiteral( "output" ), QStringLiteral( "solo" ),
                                QStringLiteral( "input" ) } );
    QVector<QString> cycle;
    REQUIRE_FALSE( WorkflowDagAnalyzer::detectCycleDFS( def, cycle ) );
    REQUIRE( cycle == ( QVector<QString>{ QStringLiteral( "solo" ), QStringLiteral( "solo" ) } ) );
}

TEST_CASE( "Disconnected components partition independently", "[d17][workflow][dag]" )
{
    // a1 -> a2 and b1 -> b2: two independent chains, same tier pairs.
    const WorkflowDefinition def = graph(
        { "a1", "a2", "b1", "b2" },
        { { "a1", "a2" }, { "b1", "b2" } } );

    const DagAnalysisResult result = WorkflowDagAnalyzer::analyzeDag( def );
    REQUIRE( result.isAcyclic );
    REQUIRE( result.executionTiers.size() == 2 );
    REQUIRE( result.executionTiers[0].nodeIds.size() == 2 );
    REQUIRE( result.executionTiers[1].nodeIds.size() == 2 );
    REQUIRE( WorkflowDagAnalyzer::calculateMaxParallelism( result.executionTiers ) == 2 );
}

TEST_CASE( "Deep 100-node chain: C_max = 1, 100 tiers, acyclic", "[d17][workflow][dag]" )
{
    QStringList nodes;
    QList<QPair<QString, QString>> edges;
    for ( int i = 0; i < 100; ++i )
        nodes << QStringLiteral( "chain_%1" ).arg( i, 3, 10, QLatin1Char( '0' ) );
    for ( int i = 1; i < 100; ++i )
        edges << qMakePair( nodes[i - 1], nodes[i] );

    const DagAnalysisResult result = WorkflowDagAnalyzer::analyzeDag( graph( nodes, edges ) );
    REQUIRE( result.isAcyclic );
    REQUIRE( result.executionTiers.size() == 100 );
    REQUIRE( WorkflowDagAnalyzer::calculateMaxParallelism( result.executionTiers ) == 1 );
    REQUIRE( result.linearSchedule == nodes );
}

TEST_CASE( "Empty graph analyses to zero tiers and zero parallelism", "[d17][workflow][dag]" )
{
    const WorkflowDefinition def;
    const DagAnalysisResult result = WorkflowDagAnalyzer::analyzeDag( def );
    REQUIRE( result.isAcyclic );
    REQUIRE( result.executionTiers.isEmpty() );
    REQUIRE( result.linearSchedule.isEmpty() );
    REQUIRE( WorkflowDagAnalyzer::calculateMaxParallelism( result.executionTiers ) == 0 );
}

TEST_CASE( "Wide layer-cake graph (10 tiers x 10 nodes) has known tier sizes", "[d17][workflow][dag]" )
{
    // Layer L_i holds nodes L<i>_<k>. Every node reads up to 3 predecessors
    // from the previous layer — tier sizes 10 across the board by construction.
    constexpr int kLayers = 10;
    constexpr int kWidth = 10;
    QStringList nodes;
    QList<QPair<QString, QString>> edges;
    auto nodeId = []( int layer, int k ) {
        return QStringLiteral( "L%1_%2" ).arg( layer ).arg( k );
    };
    for ( int layer = 0; layer < kLayers; ++layer )
        for ( int k = 0; k < kWidth; ++k )
            nodes << nodeId( layer, k );
    for ( int layer = 1; layer < kLayers; ++layer )
        for ( int k = 0; k < kWidth; ++k )
        {
            edges << qMakePair( nodeId( layer - 1, k ), nodeId( layer, k ) );
            edges << qMakePair( nodeId( layer - 1, ( k + 1 ) % kWidth ), nodeId( layer, k ) );
        }

    const DagAnalysisResult result = WorkflowDagAnalyzer::analyzeDag( graph( nodes, edges ) );
    REQUIRE( result.isAcyclic );
    REQUIRE( result.executionTiers.size() == kLayers );
    for ( const ConcurrencyTier &tier : result.executionTiers )
    {
        REQUIRE( tier.nodeIds.size() == kWidth );
        REQUIRE( tier.tierIndex == result.executionTiers.indexOf( tier ) );
    }
    REQUIRE( WorkflowDagAnalyzer::calculateMaxParallelism( result.executionTiers ) == kWidth );
}

TEST_CASE( "A cycle injected into a large layer-cake is caught quickly", "[d17][workflow][dag]" )
{
    constexpr int kLayers = 10;
    constexpr int kWidth = 10;
    QStringList nodes;
    QList<QPair<QString, QString>> edges;
    auto nodeId = []( int layer, int k ) {
        return QStringLiteral( "C%1_%2" ).arg( layer ).arg( k );
    };
    for ( int layer = 0; layer < kLayers; ++layer )
        for ( int k = 0; k < kWidth; ++k )
            nodes << nodeId( layer, k );
    for ( int layer = 1; layer < kLayers; ++layer )
        for ( int k = 0; k < kWidth; ++k )
            edges << qMakePair( nodeId( layer - 1, k ), nodeId( layer, k ) );
    // This fixture chains ONLY the k-spine edges (layer l-1, k) -> (l, k),
    // so C5_3 reaches exactly C9_3; the back edge therefore closes the
    // hand-verified cycle C5_3 -> C6_3 -> C7_3 -> C8_3 -> C9_3 -> C5_3.
    edges << qMakePair( nodeId( 9, 3 ), nodeId( 5, 3 ) );

    const auto started = std::chrono::high_resolution_clock::now();
    const DagAnalysisResult result = WorkflowDagAnalyzer::analyzeDag( graph( nodes, edges ) );
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::high_resolution_clock::now() - started )
                               .count();

    REQUIRE_FALSE( result.isAcyclic );
    REQUIRE_FALSE( result.cyclePath.isEmpty() );
    REQUIRE( result.cyclePath.first() == result.cyclePath.last() );
    REQUIRE( elapsedUs < 5000 );
}
