// tests/test_ir2_port_param_mapping.cpp — D18 D-W6 multi-input port→param + unbound refuse
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "workflow/ir2_port_param_mapping.h"
#include "workflow/ir2_registry_node_executor.h"
#include "workflow/pipeline_run_coordinator.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <json/json.h>

#include <memory>

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

namespace
{

// Returns the declared output path WITHOUT writing any file — models a
// metadata-only success or a swallowed write failure (#1002).
class GhostArtifactOperator : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "test:ir2_ghost_artifact"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "declares an output without writing it"; }
    Json::Value schema() const override { return Json::Value( Json::objectValue ); }
    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext & ) override
    {
        Json::Value result( Json::objectValue );
        result["output"] = params.get( "output", "" ).asString();
        return result;
    }
};

// Writes the declared output file — the honest bound-operator contract.
class WritingOperator : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "test:ir2_writes_artifact"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "writes the declared output file"; }
    Json::Value schema() const override { return Json::Value( Json::objectValue ); }
    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext & ) override
    {
        const std::string output = params.get( "output", "" ).asString();
        QFile file( QString::fromStdString( output ) );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
            throw sicnu::operators::RSOperatorError(
                sicnu::operators::ErrorCode::ComputationError,
                "cannot write output: " + output );
        file.write( "artifact" );
        file.close();
        Json::Value result( Json::objectValue );
        result["output"] = output;
        return result;
    }
};

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app )
    {
        static int fakeArgc = 1;
        static char fakeArgv[] = "test_ir2_port_param_mapping";
        static char *fakeArgvPtr[] = { fakeArgv };
        app = new QApplication( fakeArgc, fakeArgvPtr );
    }
    return app;
}

bool waitForCompleted( PipelineRunCoordinator &coordinator, int timeoutMs = 30000 )
{
    if ( coordinator.hasCompleted() )
        return true;
    QSignalSpy spy( &coordinator, &PipelineRunCoordinator::pipelineCompleted );
    QEventLoop loop;
    QObject::connect( &coordinator, &PipelineRunCoordinator::pipelineCompleted, &loop,
                      &QEventLoop::quit, Qt::QueuedConnection );
    QTimer::singleShot( timeoutMs, &loop, &QEventLoop::quit );
    loop.exec();
    return spy.count() >= 1 || coordinator.hasCompleted();
}

WorkflowDocument singleNodeDef( const QString &operatorId )
{
    WorkflowDocument def;
    def.workflowId = QStringLiteral( "wf-ir2-executor" );
    NodeFact n;
    n.nodeId = QStringLiteral( "n1" );
    n.operatorId = operatorId;
    n.displayName = n.nodeId;
    n.outputPorts = { makePort( QStringLiteral( "output" ), false ) };
    def.nodes.append( n );
    return def;
}

} // namespace

TEST_CASE( "Registry executor fails closed when the declared artifact is absent",
           "[d18][ir2][executor][1002]" )
{
    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    if ( !registry.hasOperator( "test:ir2_ghost_artifact" ) )
        registry.registerOperator(
            "test:ir2_ghost_artifact", [] { return std::make_unique<GhostArtifactOperator>(); } );
    if ( !registry.hasOperator( "test:ir2_writes_artifact" ) )
        registry.registerOperator(
            "test:ir2_writes_artifact", [] { return std::make_unique<WritingOperator>(); } );

    const NodeExecutor executor = makeRegistryNodeExecutor();
    QTemporaryDir runDir;
    REQUIRE( runDir.isValid() );

    // Bound operator that returns success JSON without the file → refusal,
    // not a Succeeded node publishing a phantom path (#1002).
    NodeFact ghost = makeNode( QStringLiteral( "ghost" ), { makePort( QStringLiteral( "input" ) ) } );
    ghost.operatorId = QStringLiteral( "test:ir2_ghost_artifact" );
    const NodeExecutionResult ghostResult = executor( ghost, {}, runDir.path() );
    REQUIRE_FALSE( ghostResult.success );
    REQUIRE( ghostResult.artifactPath.isEmpty() );
    REQUIRE( ghostResult.errorMessage.startsWith(
        QLatin1String( "ir2.operator_failed: missing artifact" ) ) );
    REQUIRE( ghostResult.errorMessage.contains( QStringLiteral( "ghost" ) ) );

    // Bound operator whose artifact exists → success.
    NodeFact real = makeNode( QStringLiteral( "real" ), { makePort( QStringLiteral( "input" ) ) } );
    real.operatorId = QStringLiteral( "test:ir2_writes_artifact" );
    const NodeExecutionResult realResult = executor( real, {}, runDir.path() );
    REQUIRE( realResult.success );
    REQUIRE( QFile::exists( realResult.artifactPath ) );

    registry.unregisterOperator( "test:ir2_ghost_artifact" );
    registry.unregisterOperator( "test:ir2_writes_artifact" );
}

TEST_CASE( "Coordinator without a bound executor fails nodes instead of synthesizing",
           "[d18][ir2][executor][1006]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator; // deliberately no setExecutor
    QTemporaryDir runDir;
    REQUIRE( runDir.isValid() );
    REQUIRE( coordinator.startRun( singleNodeDef( QStringLiteral( "rs:step" ) ), runDir.path() ) );
    REQUIRE( waitForCompleted( coordinator ) );

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 1 );
    const NodeStatusSnapshot snapshot = statuses.value( QStringLiteral( "n1" ) );
    REQUIRE( snapshot.state == ExecutionState::Failed );
    REQUIRE( snapshot.errorMessage.startsWith( QLatin1String( "ir2.executor_missing:" ) ) );
    REQUIRE( snapshot.outputArtifactPath.isEmpty() );
}

TEST_CASE( "Registry executor end-to-end: unbound node fails the run",
           "[d18][ir2][executor][1006]" )
{
    ensureApp();
    PipelineRunCoordinator coordinator;
    coordinator.setExecutor( makeRegistryNodeExecutor() );
    QTemporaryDir runDir;
    REQUIRE( runDir.isValid() );
    REQUIRE( coordinator.startRun(
        singleNodeDef( QStringLiteral( "rs:definitely_not_registered_d18_e2e" ) ),
        runDir.path() ) );
    REQUIRE( waitForCompleted( coordinator ) );

    const auto statuses = coordinator.getAllStatuses();
    REQUIRE( statuses.size() == 1 );
    const NodeStatusSnapshot snapshot = statuses.value( QStringLiteral( "n1" ) );
    REQUIRE( snapshot.state == ExecutionState::Failed );
    REQUIRE( snapshot.errorMessage.startsWith( QLatin1String( kIr2OperatorUnboundPrefix ) ) );
    REQUIRE( snapshot.outputArtifactPath.isEmpty() );
}
