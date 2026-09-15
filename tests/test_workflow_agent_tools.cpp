// tests/test_workflow_agent_tools.cpp — agent orchestration (D17 Package H)
//
// Ground truth: the production rule chains are hand-specified (import ->
// calibration -> atmosphere -> NDVI for the index intent; +threshold for
// water; etc.), the tool schema is checked against the Draft-07 shape, and
// the heal log is a real PROJ error text from the brief.
#include <catch2/catch_test_macros.hpp>
#include <QJsonDocument>

#include "agent/tools/workflow_orchestrator_tool.h"
#include "workflow/contract_checker.h"
#include "workflow/workflow_ir_v2.h"
#include "workflow/workflow_repair_engine.h"

using namespace sicnu::agent::tools;
using namespace sicnu::workflow;

namespace {

NodeFact node( const QString &id, const QString &op, QVector<PortFact> ins, QVector<PortFact> outs )
{
    NodeFact n;
    n.nodeId = id;
    n.operatorId = op;
    n.displayName = id;
    n.canvasPosition = QPointF( 0, 0 );
    n.inputPorts = std::move( ins );
    n.outputPorts = std::move( outs );
    return n;
}

PortFact port( const QString &name, const QString &dtype, const QString &crs, const QString &state,
               double res = 10.0, int bands = 4, bool required = false )
{
    return PortFact{ name, dtype, crs, state, res, res, bands, required };
}

/// The brief's broken fixture: a WGS84 BOA raster (same resolution and
/// radiometry) wired into a UTM slope op — a SINGLE CRS break, so the heal
/// injects exactly one adapter (+1 node) as the brief specifies.
WorkflowDefinition crsMismatchWorkflow()
{
    WorkflowDefinition def;
    def.nodes = {
        node( QStringLiteral( "node_src" ), QStringLiteral( "rs:import_raster" ), {},
              { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:4326" ),
                      QStringLiteral( "BOA" ), 10.0 ) } ),
        node( QStringLiteral( "node_slope" ), QStringLiteral( "rs:slope_aspect" ),
              { port( QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:32649" ),
                      QStringLiteral( "BOA" ), 10.0, 4, true ) },
              { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:32649" ),
                      QStringLiteral( "BOA" ), 10.0, 1 ) } ),
    };
    def.edges = { EdgeFact{ QStringLiteral( "e_broken" ), QStringLiteral( "node_src" ),
                            QStringLiteral( "output" ), QStringLiteral( "node_slope" ),
                            QStringLiteral( "input" ) } };
    return def;
}

} // namespace

TEST_CASE( "Tool JSON schema is a Draft-07 object with the required fields", "[d17][workflow][agent]" )
{
    const QJsonObject schema = WorkflowOrchestratorTool::getToolJsonSchema();
    REQUIRE( schema["$schema"].toString() == QStringLiteral( "http://json-schema.org/draft-07/schema#" ) );
    REQUIRE( schema["type"].toString() == QStringLiteral( "object" ) );
    REQUIRE( schema["properties"].toObject()["goal"].toObject()["type"].toString() == QStringLiteral( "string" ) );
    const QJsonArray required = schema["required"].toArray();
    REQUIRE( required.contains( QJsonValue( QStringLiteral( "goal" ) ) ) );

    // Sensor enum pins the supported families.
    const QJsonArray sensors = schema["properties"].toObject()["sensor"].toObject()["enum"].toArray();
    REQUIRE( sensors.contains( QJsonValue( QStringLiteral( "GF-1" ) ) ) );
    REQUIRE( sensors.contains( QJsonValue( QStringLiteral( "Landsat8" ) ) ) );
    REQUIRE( sensors.contains( QJsonValue( QStringLiteral( "Sentinel2" ) ) ) );
}

TEST_CASE( "NDVI goal compiles to the radiometric index chain", "[d17][workflow][agent]" )
{
    AutonomousCompileRequest request;
    request.userNaturalLanguageGoal = QStringLiteral( "Calibrate GF-1 image and compute NDVI index" );
    request.sensorType = QStringLiteral( "GF-1" );

    const AutonomousCompileResult result = WorkflowOrchestratorTool::compileGoalToWorkflow( request );
    REQUIRE( result.isSuccess );
    REQUIRE( result.workflow.nodes.size() == 4 ); // import -> calib -> atmosphere -> ndvi
    REQUIRE( result.workflow.findNode( "node_calib" ) != nullptr );
    REQUIRE( result.workflow.findNode( "node_ndvi" ) != nullptr );
    REQUIRE( result.workflow.findNode( "node_ndvi" )->operatorId == QStringLiteral( "rs:spectral_index" ) );
    REQUIRE( result.workflow.findNode( "node_ndvi" )->parameters["index"].toString() == QStringLiteral( "NDVI" ) );

    // Contract-clean and executable by construction.
    REQUIRE( WorkflowIR::validateSemantics( result.workflow ) );
    REQUIRE( inspectContracts( result.workflow ).isEmpty() );
}

TEST_CASE( "Water extraction compiles the NDWI threshold chain", "[d17][workflow][agent]" )
{
    AutonomousCompileRequest request;
    request.userNaturalLanguageGoal = QStringLiteral( "Extract water bodies using NDWI water index" );
    request.sensorType = QStringLiteral( "Sentinel2" );

    const AutonomousCompileResult result = WorkflowOrchestratorTool::compileGoalToWorkflow( request );
    REQUIRE( result.isSuccess );
    REQUIRE( result.workflow.nodes.size() == 5 );
    REQUIRE( result.workflow.findNode( "node_ndwi" ) != nullptr );
    REQUIRE( result.workflow.findNode( "node_threshold" ) != nullptr );
    REQUIRE( WorkflowIR::validateSemantics( result.workflow ) );
    REQUIRE( inspectContracts( result.workflow ).isEmpty() );
}

TEST_CASE( "Change detection, fusion and classification intents each compile", "[d17][workflow][agent]" )
{
    struct Case
    {
        const char *goal;
        const char *terminalOperator;
    };
    const Case cases[] = {
        { "Detect change between two bi-temporal scenes", "rs:threshold" },
        { "Pansharpen fusion of GF-1 pan and ms", "rs:gs_fusion" },
        { "Supervised land cover classification of the scene", "rs:random_forest_classify" },
    };
    for ( const Case &testCase : cases )
    {
        INFO( "goal: " << testCase.goal );
        AutonomousCompileRequest request;
        request.userNaturalLanguageGoal = QString::fromLatin1( testCase.goal );
        request.sensorType = QStringLiteral( "GF-1" );
        const AutonomousCompileResult result = WorkflowOrchestratorTool::compileGoalToWorkflow( request );
        REQUIRE( result.isSuccess );
        REQUIRE( result.workflow.nodes.size() >= 3 );
        REQUIRE( result.workflow.nodes.last().operatorId == QString::fromLatin1( testCase.terminalOperator ) );
        REQUIRE( WorkflowIR::validateSemantics( result.workflow ) );
    }
}

TEST_CASE( "Unknown goals and empty goals fail without faking success", "[d17][workflow][agent]" )
{
    AutonomousCompileRequest request;
    request.userNaturalLanguageGoal = QStringLiteral( "Tell me a joke about contour lines" );
    REQUIRE_FALSE( WorkflowOrchestratorTool::compileGoalToWorkflow( request ).isSuccess );

    request.userNaturalLanguageGoal = QStringLiteral( "   " );
    REQUIRE_FALSE( WorkflowOrchestratorTool::compileGoalToWorkflow( request ).isSuccess );
}

TEST_CASE( "Same request compiles to an identical document (determinism)", "[d17][workflow][agent]" )
{
    AutonomousCompileRequest request;
    request.userNaturalLanguageGoal = QStringLiteral( "Calibrate GF-1 image and compute NDVI index" );
    request.sensorType = QStringLiteral( "GF-1" );

    const auto first = WorkflowOrchestratorTool::compileGoalToWorkflow( request );
    const auto second = WorkflowOrchestratorTool::compileGoalToWorkflow( request );
    REQUIRE( first.workflow == second.workflow );
    REQUIRE( first.textualExplanation == second.textualExplanation );
}

TEST_CASE( "Agent self-heals a CRS mismatch from a PROJ error log", "[d17][workflow][agent]" )
{
    const WorkflowDefinition broken = crsMismatchWorkflow();
    const QString errorLog =
        QStringLiteral( "ERROR 1: PROJ: proj_create: Different spatial reference system EPSG:4326 and EPSG:32649" );

    const AutonomousCompileResult healed = WorkflowOrchestratorTool::healWorkflow( broken, errorLog );
    REQUIRE( healed.isSuccess );
    REQUIRE( healed.injectedRepairRules.contains( QStringLiteral( "rule_crs_auto_reproject" ) ) );
    REQUIRE( inspectContracts( healed.workflow ).isEmpty() );

    // The healed document gained the reproject adapter between the two nodes.
    REQUIRE( healed.workflow.nodes.size() == broken.nodes.size() + 1 );
    REQUIRE( WorkflowIR::validateSemantics( healed.workflow ) );
    bool reprojectPresent = false;
    for ( const NodeFact &node : healed.workflow.nodes )
        if ( node.operatorId == QLatin1String( "rs:reproject" ) )
            reprojectPresent = true;
    REQUIRE( reprojectPresent );
}

TEST_CASE( "A matched CRS log with no offending wiring is a typed failure, not a fake heal",
           "[d17][workflow][agent]" )
{
    // A contract-CLEAN workflow whose ports never carry the log's source
    // CRS: the pattern matches, but there is nothing to heal.
    WorkflowDefinition clean;
    clean.nodes = {
        node( QStringLiteral( "src" ), QStringLiteral( "rs:import_raster" ), {},
              { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:32649" ),
                      QStringLiteral( "BOA" ), 10.0 ) } ),
        node( QStringLiteral( "dst" ), QStringLiteral( "rs:slope_aspect" ),
              { port( QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                      QStringLiteral( "BOA" ), 10.0, 4, true ) }, {} ),
    };
    clean.edges = { EdgeFact{ QStringLiteral( "e" ), QStringLiteral( "src" ), QStringLiteral( "output" ),
                              QStringLiteral( "dst" ), QStringLiteral( "input" ) } };
    REQUIRE( inspectContracts( clean ).isEmpty() );

    const AutonomousCompileResult result = WorkflowOrchestratorTool::healWorkflow(
        clean, QStringLiteral( "ERROR 1: PROJ: proj_create: Different spatial reference system EPSG:4326 and EPSG:32649" ) );
    REQUIRE_FALSE( result.isSuccess );
    REQUIRE( result.injectedRepairRules.isEmpty() );
    REQUIRE( result.workflow == clean );
}

TEST_CASE( "Unknown error logs produce a typed no-op heal", "[d17][workflow][agent]" )
{
    const WorkflowDefinition broken = crsMismatchWorkflow();
    const AutonomousCompileResult result = WorkflowOrchestratorTool::healWorkflow(
        broken, QStringLiteral( "ERROR 1303: out of memory allocating 4 GiB window" ) );
    REQUIRE_FALSE( result.isSuccess );
    REQUIRE( result.injectedRepairRules.isEmpty() );
    REQUIRE( result.workflow == broken ); // untouched on no-op
}
