// tests/test_ir2_port_param_mapping.cpp — D18 D-W6 multi-input port→param + unbound refuse
#include <catch2/catch_test_macros.hpp>

#include "workflow/ir2_port_param_mapping.h"
#include "workflow/ir2_registry_node_executor.h"

#include <QHash>
#include <QString>

#include <json/json.h>

using namespace sicnu::workflow;

namespace {

PortFact makePort( const QString &name, bool required = true )
{
    PortFact p;
    p.portName = name;
    p.dataType = QStringLiteral( "Raster" );
    p.crs = QStringLiteral( "*" );
    p.radiometricState = QStringLiteral( "None" );
    p.isRequired = required;
    return p;
}

NodeFact makeNode( const QString &id, const QVector<PortFact> &inputs )
{
    NodeFact n;
    n.nodeId = id;
    n.operatorId = QStringLiteral( "rs:test_op" );
    n.displayName = id;
    n.inputPorts = inputs;
    n.outputPorts = { makePort( QStringLiteral( "output" ), false ) };
    return n;
}

} // namespace

TEST_CASE( "Multi-input maps by explicit IR2 target port names", "[d18][ir2][port_map]" )
{
    const NodeFact node = makeNode(
        QStringLiteral( "fuse" ),
        { makePort( QStringLiteral( "input" ) ), makePort( QStringLiteral( "reference" ) ),
          makePort( QStringLiteral( "mask" ) ) } );

    QHash<QString, QString> artifacts;
    artifacts.insert( QStringLiteral( "mask" ), QStringLiteral( "/tmp/mask.tif" ) );
    artifacts.insert( QStringLiteral( "reference" ), QStringLiteral( "/tmp/ref.tif" ) );
    artifacts.insert( QStringLiteral( "input" ), QStringLiteral( "/tmp/primary.tif" ) );

    Json::Value params( Json::objectValue );
    applyIr2InputPortMapping( node, artifacts, params );

    REQUIRE( params["input"].asString() == "/tmp/primary.tif" );
    REQUIRE( params["reference"].asString() == "/tmp/ref.tif" );
    REQUIRE( params["mask"].asString() == "/tmp/mask.tif" );
    REQUIRE( params.isMember( "ir2_input_artifacts" ) );
    REQUIRE( params["ir2_input_artifacts"]["input"].asString() == "/tmp/primary.tif" );
    REQUIRE( params["ir2_input_artifacts"]["reference"].asString() == "/tmp/ref.tif" );
    REQUIRE( params["ir2_input_artifacts"]["mask"].asString() == "/tmp/mask.tif" );
}

TEST_CASE( "Explicit node.parameters are not overwritten by port mapping", "[d18][ir2][port_map]" )
{
    const NodeFact node = makeNode( QStringLiteral( "n" ), { makePort( QStringLiteral( "input" ) ),
                                                             makePort( QStringLiteral( "dem" ) ) } );

    QHash<QString, QString> artifacts;
    artifacts.insert( QStringLiteral( "input" ), QStringLiteral( "/tmp/from_edge.tif" ) );
    artifacts.insert( QStringLiteral( "dem" ), QStringLiteral( "/tmp/dem_edge.tif" ) );

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/explicit_input.tif";

    applyIr2InputPortMapping( node, artifacts, params );

    REQUIRE( params["input"].asString() == "/tmp/explicit_input.tif" );
    REQUIRE( params["dem"].asString() == "/tmp/dem_edge.tif" );
}

TEST_CASE( "Primary input aliases first declared bound port when no port named input",
           "[d18][ir2][port_map]" )
{
    const NodeFact node = makeNode(
        QStringLiteral( "ratio" ),
        { makePort( QStringLiteral( "inputA" ) ), makePort( QStringLiteral( "inputB" ) ) } );

    QHash<QString, QString> artifacts;
    artifacts.insert( QStringLiteral( "inputB" ), QStringLiteral( "/tmp/b.tif" ) );
    artifacts.insert( QStringLiteral( "inputA" ), QStringLiteral( "/tmp/a.tif" ) );

    Json::Value params( Json::objectValue );
    applyIr2InputPortMapping( node, artifacts, params );

    REQUIRE( params["inputA"].asString() == "/tmp/a.tif" );
    REQUIRE( params["inputB"].asString() == "/tmp/b.tif" );
    // Declaration order: inputA is primary alias for params["input"].
    REQUIRE( params["input"].asString() == "/tmp/a.tif" );
}

TEST_CASE( "Legacy sourceNodeId keys fall back to declaration-order zip", "[d18][ir2][port_map]" )
{
    const NodeFact node = makeNode(
        QStringLiteral( "legacy" ),
        { makePort( QStringLiteral( "input" ) ), makePort( QStringLiteral( "reference" ) ) } );

    // Historical coordinator keyed by source node id — not port names.
    QHash<QString, QString> artifacts;
    artifacts.insert( QStringLiteral( "node_z" ), QStringLiteral( "/tmp/z.tif" ) );
    artifacts.insert( QStringLiteral( "node_a" ), QStringLiteral( "/tmp/a.tif" ) );

    Json::Value params( Json::objectValue );
    applyIr2InputPortMapping( node, artifacts, params );

    // Sorted legacy keys: node_a then node_z → zip onto input, reference.
    REQUIRE( params["input"].asString() == "/tmp/a.tif" );
    REQUIRE( params["reference"].asString() == "/tmp/z.tif" );
}

TEST_CASE( "Unbound empty and unknown still refuse with stable prefix", "[d18][ir2][unbound]" )
{
    // makeRegistryNodeExecutor calls makeIr2UnboundRefusal BEFORE
    // applyIr2InputPortMapping — mapping never yields synthetic success.
    NodeFact emptyNode = makeNode( QStringLiteral( "orphan" ), { makePort( QStringLiteral( "input" ) ) } );
    emptyNode.operatorId.clear();

    const NodeExecutionResult emptyRefusal =
        makeIr2UnboundRefusal( emptyNode, Ir2OperatorBinding::UnboundEmpty );
    REQUIRE_FALSE( emptyRefusal.success );
    REQUIRE( emptyRefusal.artifactPath.isEmpty() );
    REQUIRE( emptyRefusal.errorMessage.startsWith( QLatin1String( kIr2OperatorUnboundPrefix ) ) );
    REQUIRE( emptyRefusal.errorMessage.contains( QStringLiteral( "empty operatorId" ) ) );
    REQUIRE( emptyRefusal.errorMessage.contains( QStringLiteral( "orphan" ) ) );

    NodeFact unknownNode = makeNode( QStringLiteral( "typo" ), { makePort( QStringLiteral( "input" ) ) } );
    unknownNode.operatorId = QStringLiteral( "rs:definitely_not_registered_d18_xyz" );

    const NodeExecutionResult unknownRefusal =
        makeIr2UnboundRefusal( unknownNode, Ir2OperatorBinding::UnboundUnknown );
    REQUIRE_FALSE( unknownRefusal.success );
    REQUIRE( unknownRefusal.artifactPath.isEmpty() );
    REQUIRE( unknownRefusal.errorMessage.startsWith( QLatin1String( kIr2OperatorUnboundPrefix ) ) );
    REQUIRE( unknownRefusal.errorMessage.contains( QStringLiteral( "no registry binding" ) ) );
    REQUIRE( unknownRefusal.errorMessage.contains( QStringLiteral( "rs:definitely_not_registered_d18_xyz" ) ) );

    // Mapping on an unbound-shaped node still only fills params — it does not
    // invent success; the executor refuses via the helper above first.
    Json::Value params( Json::objectValue );
    QHash<QString, QString> artifacts;
    artifacts.insert( QStringLiteral( "input" ), QStringLiteral( "/tmp/should_not_matter.tif" ) );
    applyIr2InputPortMapping( emptyNode, artifacts, params );
    REQUIRE( params["input"].asString() == "/tmp/should_not_matter.tif" );
    // Refusal result remains failed regardless of mapped params.
    REQUIRE_FALSE( emptyRefusal.success );
}
