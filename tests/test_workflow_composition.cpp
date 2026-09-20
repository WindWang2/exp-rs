// tests/test_workflow_composition.cpp — WP2 composition: subflow expansion
//
// Contract under test (DECISIONS D8): "workflow:subflow" nodes inline an
// embedded fragment document; expanded ids are "<instance>__<fragId>" (pure
// function of authored ids -> stable lineage); inlined nodes carry
// originNodeId for designer-level attribution; every edge touching the
// instance resolves through the interface map; all failures are closed and
// name the offending instance node.

#include <catch2/catch_test_macros.hpp>

#include "workflow/plan_optimizer.h"
#include "workflow/workflow_composer.h"
#include "workflow/workflow_dag_analyzer.h"
#include "workflow/workflow_ir_v2.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>

#include <algorithm>

using namespace sicnu::workflow;

namespace {

PortFact port( const char *name, bool required = true )
{
    PortFact p;
    p.portName = QLatin1String( name );
    p.dataType = QStringLiteral( "Raster" );
    p.crs = QStringLiteral( "*" );
    p.radiometricState = QStringLiteral( "None" );
    p.isRequired = required;
    return p;
}

NodeFact plainNode( const char *id, QVector<PortFact> in = {}, QVector<PortFact> out = {} )
{
    NodeFact n;
    n.nodeId = QLatin1String( id );
    n.operatorId = QStringLiteral( "rs:op" );
    n.inputPorts = std::move( in );
    n.outputPorts = std::move( out );
    return n;
}

/// fragment: f_in -(e_f)-> f_out ; one Raster in, one Raster out.
QJsonObject twoNodeFragment()
{
    WorkflowDocument frag;
    frag.version = QStringLiteral( "2.1" );
    NodeFact in = plainNode( "f_in", { port( "in" ) }, { port( "mid" ) } );
    NodeFact out = plainNode( "f_out", { port( "in" ) }, { port( "out" ) } );
    frag.nodes = { in, out };
    frag.edges = { EdgeFact{ QStringLiteral( "e_f" ), QStringLiteral( "f_in" ), QStringLiteral( "mid" ),
                             QStringLiteral( "f_out" ), QStringLiteral( "in" ) } };
    return WorkflowIR::toJson( frag );
}

NodeFact subflowNode( const char *id, const QJsonObject &fragment )
{
    NodeFact n;
    n.nodeId = QLatin1String( id );
    n.operatorId = QStringLiteral( "workflow:subflow" );
    n.inputPorts = { port( "in" ) };
    n.outputPorts = { port( "out" ) };
    n.parameters = QJsonObject{
        { QStringLiteral( "fragment" ), fragment },
        { QStringLiteral( "interface" ),
          QJsonObject{
              { QStringLiteral( "inputs" ),
                QJsonObject{ { QStringLiteral( "in" ), QJsonObject{ { QStringLiteral( "node" ), QStringLiteral( "f_in" ) },
                                                                  { QStringLiteral( "port" ), QStringLiteral( "in" ) } } } } },
              { QStringLiteral( "outputs" ),
                QJsonObject{ { QStringLiteral( "out" ), QJsonObject{ { QStringLiteral( "node" ), QStringLiteral( "f_out" ) },
                                                                     { QStringLiteral( "port" ), QStringLiteral( "out" ) } } } } } } },
    };
    return n;
}

/// A -> S -> B where S is the subflow instance.
WorkflowDocument parentDoc()
{
    WorkflowDocument def;
    def.version = QStringLiteral( "2.1" );
    def.workflowId = QStringLiteral( "wf-parent" );
    def.nodes = { plainNode( "A", {}, { port( "out" ) } ), subflowNode( "S", twoNodeFragment() ),
                  plainNode( "B", { port( "in" ) } ) };
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "A" ), QStringLiteral( "out" ),
                            QStringLiteral( "S" ), QStringLiteral( "in" ) },
                  EdgeFact{ QStringLiteral( "e2" ), QStringLiteral( "S" ), QStringLiteral( "out" ),
                            QStringLiteral( "B" ), QStringLiteral( "in" ) } };
    return def;
}

const NodeFact *find( const WorkflowDocument &def, const QString &id )
{
    return def.findNode( id );
}

} // namespace

TEST_CASE( "expandSubflows is identity on documents without subflows", "[d17][workflow][composition]" )
{
    WorkflowDocument def;
    def.nodes = { plainNode( "A" ), plainNode( "B" ) };
    REQUIRE_FALSE( WorkflowComposer::hasSubflowNodes( def ) );
    const auto result = WorkflowComposer::expandSubflows( def );
    REQUIRE( result.isSuccess() );
    REQUIRE( result.value() == def );
}

TEST_CASE( "Subflow instance flattens with namespaced ids and origin attribution", "[d17][workflow][composition]" )
{
    const auto result = WorkflowComposer::expandSubflows( parentDoc() );
    REQUIRE( result.isSuccess() );
    const WorkflowDocument &flat = result.value();

    REQUIRE( flat.nodes.size() == 4 );
    REQUIRE( find( flat, QStringLiteral( "S" ) ) == nullptr ); // instance consumed
    REQUIRE( find( flat, QStringLiteral( "S__f_in" ) ) != nullptr );
    REQUIRE( find( flat, QStringLiteral( "S__f_out" ) ) != nullptr );

    // Origin attribution: both inlined nodes point at the designer-level node.
    REQUIRE( find( flat, QStringLiteral( "S__f_in" ) )->originNodeId == QStringLiteral( "S" ) );
    REQUIRE( find( flat, QStringLiteral( "S__f_out" ) )->originNodeId == QStringLiteral( "S" ) );
    REQUIRE( find( flat, QStringLiteral( "A" ) )->originNodeId.isEmpty() );

    // Parent edges rewired through the interface map; internal edge namespaced.
    REQUIRE( flat.edges.size() == 3 );
    REQUIRE( flat.edges[0].edgeId == QStringLiteral( "e1" ) );
    REQUIRE( flat.edges[0].targetNodeId == QStringLiteral( "S__f_in" ) );
    REQUIRE( flat.edges[0].targetPortName == QStringLiteral( "in" ) );
    REQUIRE( flat.edges[1].edgeId == QStringLiteral( "e2" ) );
    REQUIRE( flat.edges[1].sourceNodeId == QStringLiteral( "S__f_out" ) );
    REQUIRE( flat.edges[1].sourcePortName == QStringLiteral( "out" ) );
    REQUIRE( flat.edges[2].edgeId == QStringLiteral( "S__e_f" ) );
    REQUIRE( flat.edges[2].sourceNodeId == QStringLiteral( "S__f_in" ) );
    REQUIRE( flat.edges[2].targetNodeId == QStringLiteral( "S__f_out" ) );

    REQUIRE( WorkflowIR::validateSemantics( flat ) );
    REQUIRE( WorkflowDagAnalyzer::analyzeDag( flat ).isAcyclic );
    REQUIRE( WorkflowIR::fromJson( WorkflowIR::toJson( flat ) ).value() == flat );
}

TEST_CASE( "Expansion is deterministic: signatures identical across repeated expansion", "[d17][workflow][composition]" )
{
    const auto first = WorkflowComposer::expandSubflows( parentDoc() );
    const auto second = WorkflowComposer::expandSubflows( parentDoc() );
    REQUIRE( first.isSuccess() );
    REQUIRE( second.isSuccess() );
    REQUIRE( first.value() == second.value() );
    REQUIRE( WorkflowPlanOptimizer::computeLineageSignatures( first.value() )
             == WorkflowPlanOptimizer::computeLineageSignatures( second.value() ) );
}

TEST_CASE( "Bindings substitute fragment node parameters before inlining", "[d17][workflow][composition]" )
{
    WorkflowDocument def = parentDoc();
    def.nodes[1].parameters.insert(
        QStringLiteral( "bindings" ),
        QJsonObject{ { QStringLiteral( "f_in" ), QJsonObject{ { QStringLiteral( "scale" ), 0.5 } } } } );

    const auto result = WorkflowComposer::expandSubflows( def );
    REQUIRE( result.isSuccess() );
    REQUIRE( find( result.value(), QStringLiteral( "S__f_in" ) )
                 ->parameters.value( QLatin1String( "scale" ) )
                 .toDouble()
             == 0.5 );
}

TEST_CASE( "A 2.0-authored document claims 2.1 after expansion", "[d17][workflow][composition]" )
{
    WorkflowDocument def = parentDoc();
    def.version = QStringLiteral( "2.0" );
    const auto result = WorkflowComposer::expandSubflows( def );
    REQUIRE( result.isSuccess() );
    REQUIRE( result.value().version == QStringLiteral( "2.1" ) );
}

TEST_CASE( "Nested subflows expand recursively", "[d17][workflow][composition]" )
{
    // Inner fragment wraps twoNodeFragment in its own instance.
    WorkflowDocument inner;
    inner.version = QStringLiteral( "2.1" );
    inner.nodes = { subflowNode( "inner_s", twoNodeFragment() ) };
    inner.edges = {};

    // Give the inner instance a closed interface (no ports used by edges).
    inner.nodes[0].inputPorts = {};
    inner.nodes[0].outputPorts = {};
    inner.nodes[0].parameters.remove( QStringLiteral( "interface" ) );

    WorkflowDocument def;
    def.version = QStringLiteral( "2.1" );
    def.nodes = { subflowNode( "outer_s", WorkflowIR::toJson( inner ) ) };
    def.nodes[0].inputPorts = {};
    def.nodes[0].outputPorts = {};
    def.nodes[0].parameters.remove( QStringLiteral( "interface" ) );

    const auto result = WorkflowComposer::expandSubflows( def );
    REQUIRE( result.isSuccess() );
    const WorkflowDocument &flat = result.value();
    REQUIRE( flat.nodes.size() == 2 );
    REQUIRE( find( flat, QStringLiteral( "outer_s__inner_s__f_in" ) ) != nullptr );
    REQUIRE( find( flat, QStringLiteral( "outer_s__inner_s__f_out" ) ) != nullptr );
    // Outermost instance is the designer-level attribution anchor.
    REQUIRE( find( flat, QStringLiteral( "outer_s__inner_s__f_in" ) )->originNodeId
             == QStringLiteral( "outer_s" ) );
}

TEST_CASE( "Runaway nesting is refused at the depth cap", "[d17][workflow][composition]" )
{
    // Build kMaxSubflowDepth + 2 shells, each wrapping the previous one.
    WorkflowDocument leaf;
    leaf.version = QStringLiteral( "2.1" );
    leaf.nodes = { plainNode( "leaf" ) };

    WorkflowDocument def = leaf;
    for ( int i = 0; i < WorkflowComposer::kMaxSubflowDepth + 2; ++i )
    {
        NodeFact shell;
        shell.nodeId = QStringLiteral( "shell_%1" ).arg( i );
        shell.operatorId = QStringLiteral( "workflow:subflow" );
        shell.parameters = QJsonObject{ { QStringLiteral( "fragment" ), WorkflowIR::toJson( def ) } };
        WorkflowDocument next;
        next.version = QStringLiteral( "2.1" );
        next.nodes = { shell };
        def = next;
    }

    const auto result = WorkflowComposer::expandSubflows( def );
    REQUIRE_FALSE( result.isSuccess() );
    REQUIRE( result.error().contains( QStringLiteral( "depth" ) ) );
}

TEST_CASE( "Expansion failures are closed and name the instance", "[d17][workflow][composition]" )
{
    SECTION( "missing fragment parameter" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes[1].parameters.remove( QStringLiteral( "fragment" ) );
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "'S'" ) ) );
        REQUIRE( result.error().contains( QStringLiteral( "fragment" ) ) );
    }
    SECTION( "file/path fragment reference refused" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes[1].parameters[QStringLiteral( "fragment" )] = QStringLiteral( "./frag.json" );
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "'S'" ) ) );
    }
    SECTION( "fragment fails IR parse" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes[1].parameters[QStringLiteral( "fragment" )] = QJsonObject{ { QStringLiteral( "version" ), QStringLiteral( "9.9" ) } };
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "'S'" ) ) );
        REQUIRE( result.error().contains( QStringLiteral( "9.9" ) ) );
    }
    SECTION( "interface maps an undeclared instance port" )
    {
        WorkflowDocument def = parentDoc();
        QJsonObject iface = def.nodes[1].parameters[QStringLiteral( "interface" )].toObject();
        QJsonObject inputs = iface[QStringLiteral( "inputs" )].toObject();
        inputs[QStringLiteral( "ghost" )] = QJsonObject{ { QStringLiteral( "node" ), QStringLiteral( "f_in" ) },
                                                         { QStringLiteral( "port" ), QStringLiteral( "in" ) } };
        iface[QStringLiteral( "inputs" )] = inputs;
        def.nodes[1].parameters[QStringLiteral( "interface" )] = iface;
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "ghost" ) ) );
    }
    SECTION( "interface targets a missing fragment port" )
    {
        WorkflowDocument def = parentDoc();
        QJsonObject iface = def.nodes[1].parameters[QStringLiteral( "interface" )].toObject();
        QJsonObject inputs = iface[QStringLiteral( "inputs" )].toObject();
        inputs[QStringLiteral( "in" )] = QJsonObject{ { QStringLiteral( "node" ), QStringLiteral( "f_in" ) },
                                                      { QStringLiteral( "port" ), QStringLiteral( "nope" ) } };
        iface[QStringLiteral( "inputs" )] = inputs;
        def.nodes[1].parameters[QStringLiteral( "interface" )] = iface;
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "f_in" ) ) );
    }
    SECTION( "edge onto an unmapped instance port" )
    {
        WorkflowDocument def = parentDoc();
        // Rebind e1 onto instance port "other" which has no interface entry.
        def.nodes[1].inputPorts.append( port( "other" ) );
        def.edges[0].targetPortName = QStringLiteral( "other" );
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "e1" ) ) );
        REQUIRE( result.error().contains( QStringLiteral( "other" ) ) );
    }
    SECTION( "bindings reference an unknown fragment node" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes[1].parameters.insert(
            QStringLiteral( "bindings" ),
            QJsonObject{ { QStringLiteral( "ghost_node" ), QJsonObject{ { QStringLiteral( "x" ), 1 } } } } );
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "ghost_node" ) ) );
    }
    SECTION( "expanded id collides with an existing node" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes.append( plainNode( "S__f_in" ) );
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "S__f_in" ) ) );
    }
    SECTION( "semantically invalid fragment refused" )
    {
        WorkflowDocument frag;
        frag.version = QStringLiteral( "2.1" );
        frag.nodes = { plainNode( "f_in" ), plainNode( "f_in" ) }; // duplicate id
        WorkflowDocument def = parentDoc();
        def.nodes[1].parameters[QStringLiteral( "fragment" )] = WorkflowIR::toJson( frag );
        const auto result = WorkflowComposer::expandSubflows( def );
        REQUIRE_FALSE( result.isSuccess() );
        REQUIRE( result.error().contains( QStringLiteral( "'S'" ) ) );
    }
}

TEST_CASE( "Two instances of the same fragment expand to disjoint namespaces", "[d17][workflow][composition]" )
{
    WorkflowDocument def;
    def.version = QStringLiteral( "2.1" );
    def.nodes = { plainNode( "A", {}, { port( "out" ) } ),
                  subflowNode( "S1", twoNodeFragment() ),
                  subflowNode( "S2", twoNodeFragment() ),
                  plainNode( "B", { port( "in" ) } ) };
    // A fans out to both instances; B2 joins them via two input ports so the
    // in-degree invariant holds.
    NodeFact join = plainNode( "B2", { port( "in" ), port( "aux" ) } );
    def.nodes.removeLast();
    def.nodes.append( join );
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "A" ), QStringLiteral( "out" ),
                            QStringLiteral( "S1" ), QStringLiteral( "in" ) },
                  EdgeFact{ QStringLiteral( "e2" ), QStringLiteral( "A" ), QStringLiteral( "out" ),
                            QStringLiteral( "S2" ), QStringLiteral( "in" ) },
                  EdgeFact{ QStringLiteral( "e3" ), QStringLiteral( "S1" ), QStringLiteral( "out" ),
                            QStringLiteral( "B2" ), QStringLiteral( "in" ) },
                  EdgeFact{ QStringLiteral( "e4" ), QStringLiteral( "S2" ), QStringLiteral( "out" ),
                            QStringLiteral( "B2" ), QStringLiteral( "aux" ) } };

    const auto result = WorkflowComposer::expandSubflows( def );
    REQUIRE( result.isSuccess() );
    const WorkflowDocument &flat = result.value();

    // Both fragment copies inlined under their own prefixes, no id sharing.
    REQUIRE( find( flat, QStringLiteral( "S1__f_in" ) ) != nullptr );
    REQUIRE( find( flat, QStringLiteral( "S2__f_in" ) ) != nullptr );
    REQUIRE( find( flat, QStringLiteral( "S1__f_in" ) )->originNodeId == QStringLiteral( "S1" ) );
    REQUIRE( find( flat, QStringLiteral( "S2__f_in" ) )->originNodeId == QStringLiteral( "S2" ) );
    REQUIRE( flat.nodes.size() == 6 ); // A + 2x2 inlined + B2

    // Diamond across the boundary rewires correctly; internal edges stay
    // inside their namespaces (no cross-talk between instances).
    REQUIRE( WorkflowIR::validateSemantics( flat ) );
    REQUIRE( WorkflowDagAnalyzer::analyzeDag( flat ).isAcyclic );
    int e3 = -1, e4 = -1, internal = 0;
    for ( int i = 0; i < flat.edges.size(); ++i )
    {
        const EdgeFact &e = flat.edges[i];
        if ( e.edgeId == QLatin1String( "e3" ) )
            e3 = i;
        if ( e.edgeId == QLatin1String( "e4" ) )
            e4 = i;
        if ( e.edgeId.contains( QLatin1String( "__" ) ) )
            ++internal;
    }
    REQUIRE( e3 >= 0 );
    REQUIRE( e4 >= 0 );
    REQUIRE( flat.edges[e3].sourceNodeId == QStringLiteral( "S1__f_out" ) );
    REQUIRE( flat.edges[e3].targetNodeId == QStringLiteral( "B2" ) );
    REQUIRE( flat.edges[e4].sourceNodeId == QStringLiteral( "S2__f_out" ) );
    REQUIRE( flat.edges[e4].targetPortName == QStringLiteral( "aux" ) );
    REQUIRE( internal == 2 ); // S1__e_f and S2__e_f
}

TEST_CASE( "An interface mapping two ports to one fragment input fails closed", "[d17][workflow][composition]" )
{
    // Both instance input ports map onto the SAME fragment port f_in.in ->
    // the merged doc would blow in-degree. The composer's own merged-document
    // revalidation must refuse, not defer the failure to the caller.
    WorkflowDocument def;
    def.version = QStringLiteral( "2.1" );
    def.nodes = { plainNode( "A", {}, { port( "out" ) } ),
                  plainNode( "B", {}, { port( "out" ) } ),
                  subflowNode( "S", twoNodeFragment() ) };
    def.nodes[2].inputPorts.append( port( "aux" ) );
    QJsonObject iface = def.nodes[2].parameters.value( QLatin1String( "interface" ) ).toObject();
    QJsonObject inputs = iface.value( QLatin1String( "inputs" ) ).toObject();
    inputs.insert( QStringLiteral( "aux" ),
                   QJsonObject{ { QStringLiteral( "node" ), QStringLiteral( "f_in" ) },
                                { QStringLiteral( "port" ), QStringLiteral( "in" ) } } );
    iface.insert( QStringLiteral( "inputs" ), inputs );
    def.nodes[2].parameters.insert( QStringLiteral( "interface" ), iface );
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "A" ), QStringLiteral( "out" ),
                            QStringLiteral( "S" ), QStringLiteral( "in" ) },
                  EdgeFact{ QStringLiteral( "e2" ), QStringLiteral( "B" ), QStringLiteral( "out" ),
                            QStringLiteral( "S" ), QStringLiteral( "aux" ) } };

    const auto result = WorkflowComposer::expandSubflows( def );
    REQUIRE_FALSE( result.isSuccess() );
}

// ---------------------------------------------------------------------------
// WP6 deterministic planning: the plan signature is a pure function of
// topology + parameters — invariant to serialization order and edge labels.
// ---------------------------------------------------------------------------

TEST_CASE( "Plan signature is invariant to serialization order and edge ids", "[d17][workflow][planning]" )
{
    WorkflowDocument def = parentDoc();
    const QString baseline = WorkflowPlanOptimizer::computePlanSignature( def );
    REQUIRE( baseline.size() == 64 );

    // Same graph, nodes and edges enumerated in reverse order.
    WorkflowDocument reordered = def;
    std::reverse( reordered.nodes.begin(), reordered.nodes.end() );
    std::reverse( reordered.edges.begin(), reordered.edges.end() );
    REQUIRE( WorkflowPlanOptimizer::computePlanSignature( reordered ) == baseline );

    // Edge ids are bookkeeping labels: renaming them changes nothing.
    WorkflowDocument relabeled = def;
    relabeled.edges[0].edgeId = QStringLiteral( "renamed_e1" );
    REQUIRE( WorkflowPlanOptimizer::computePlanSignature( relabeled ) == baseline );
}

TEST_CASE( "Plan signature changes on parameter, port and wiring edits", "[d17][workflow][planning]" )
{
    const QString baseline = WorkflowPlanOptimizer::computePlanSignature( parentDoc() );

    SECTION( "parameter change" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes[0].parameters.insert( QStringLiteral( "alpha" ), 1 );
        REQUIRE( WorkflowPlanOptimizer::computePlanSignature( def ) != baseline );
    }
    SECTION( "port rename" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes[0].outputPorts[0].portName = QStringLiteral( "renamed" );
        def.edges[0].sourcePortName = QStringLiteral( "renamed" );
        REQUIRE( WorkflowPlanOptimizer::computePlanSignature( def ) != baseline );
    }
    SECTION( "rewire to a different source" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes.append( plainNode( "A2", {}, { port( "out" ) } ) );
        def.edges[0].sourceNodeId = QStringLiteral( "A2" );
        REQUIRE( WorkflowPlanOptimizer::computePlanSignature( def ) != baseline );
    }
    SECTION( "node id rename" )
    {
        WorkflowDocument def = parentDoc();
        def.nodes[0].nodeId = QStringLiteral( "A_renamed" );
        def.edges[0].sourceNodeId = QStringLiteral( "A_renamed" );
        REQUIRE( WorkflowPlanOptimizer::computePlanSignature( def ) != baseline );
    }
}
