// tests/test_workflow_repair_rules.cpp — contract inspection & repair engine (D17 Package C)
//
// Ground truth: the hand-specified closed rule table in
// workflow_repair_engine.h and the radiometric ladder
// DN < Radiance < TOA/BOA. Fixtures are hand-built graphs whose port facts
// are chosen so the expected violations can be enumerated by inspection.
#include <catch2/catch_test_macros.hpp>
#include <QJsonDocument>
#include <QSet>

#include "workflow/workflow_repair_engine.h"
#include "workflow/workflow_ir_v2.h"

using namespace sicnu::workflow;

namespace {

NodeFact makeNode( const QString &id, QVector<PortFact> ins, QVector<PortFact> outs )
{
    NodeFact node;
    node.nodeId = id;
    node.operatorId = QStringLiteral( "rs:test_op" );
    node.displayName = id;
    node.inputPorts = std::move( ins );
    node.outputPorts = std::move( outs );
    node.canvasPosition = QPointF( 0, 0 );
    return node;
}

PortFact port( const QString &name, const QString &dtype, const QString &crs, const QString &state,
               double rx = 0.0, double ry = 0.0, int bands = 0, bool required = false )
{
    return PortFact{ name, dtype, crs, state, rx, ry, bands, required };
}

/// The brief's broken pipeline: EPSG:4326 DN source wired straight into an
/// EPSG:32649 BOA slope operator — a CRS and a radiometric break at once.
WorkflowDocument crsMismatchWorkflow()
{
    WorkflowDocument def;
    def.nodes = {
        makeNode( QStringLiteral( "node_src" ),
                  {},
                  { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:4326" ),
                          QStringLiteral( "DN" ), 30.0, 30.0, 4 ) } ),
        makeNode( QStringLiteral( "node_slope" ),
                  { port( QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:32649" ),
                          QStringLiteral( "BOA" ), 10.0, 10.0, 4, true ) },
                  { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:32649" ),
                          QStringLiteral( "BOA" ), 10.0, 10.0, 1 ) } ),
    };
    def.edges = { EdgeFact{ QStringLiteral( "e_broken" ), QStringLiteral( "node_src" ),
                            QStringLiteral( "output" ), QStringLiteral( "node_slope" ),
                            QStringLiteral( "input" ) } };
    return def;
}

} // namespace

TEST_CASE( "CRS mismatch alone injects exactly one reproject adapter", "[d17][workflow][repair]" )
{
    // Same resolution and state; only CRS differs.
    WorkflowDocument def;
    def.nodes = {
        makeNode( QStringLiteral( "src" ), {},
                  { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:4326" ),
                          QStringLiteral( "BOA" ), 10.0, 10.0, 4 ) } ),
        makeNode( QStringLiteral( "slope" ),
                  { port( QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:32649" ),
                          QStringLiteral( "BOA" ), 10.0, 10.0, 4, true ) },
                  {} ),
    };
    def.edges = { EdgeFact{ QStringLiteral( "e1" ), QStringLiteral( "src" ), QStringLiteral( "output" ),
                            QStringLiteral( "slope" ), QStringLiteral( "input" ) } };

    auto violations = WorkflowRepairEngine::inspectContracts( def );
    REQUIRE( violations.size() == 1 );
    REQUIRE( violations[0].mismatchType == ContractMismatchType::CrsMismatch );
    REQUIRE( violations[0].actualSpecification == QStringLiteral( "EPSG:4326" ) );
    REQUIRE( violations[0].expectedSpecification == QStringLiteral( "EPSG:32649" ) );

    const RepairPlan plan = WorkflowRepairEngine::inferRepairs( def );
    REQUIRE( plan.requiresRepair );
    REQUIRE( plan.suggestedActions.size() == 1 );
    REQUIRE( plan.suggestedActions[0].insertOperatorId == QStringLiteral( "rs:reproject" ) );
    REQUIRE( plan.suggestedActions[0].ruleId == QStringLiteral( "rule_crs_auto_reproject" ) );
    REQUIRE( plan.suggestedActions[0].adapterParameters["target_crs"].toString() == QStringLiteral( "EPSG:32649" ) );
    REQUIRE( plan.suggestedActions[0].adapterParameters["resampling"].toString() == QStringLiteral( "bilinear" ) );

    const WorkflowDocument healed = WorkflowRepairEngine::applyRepairPlan( def, plan );
    REQUIRE( healed.nodes.size() == def.nodes.size() + 1 );

    // The old edge is gone; source -> adapter -> target chain took its place.
    REQUIRE( healed.findEdge( "e1" ) == nullptr );
    REQUIRE( healed.edges.size() == 2 );
    REQUIRE( WorkflowIR::validateSemantics( healed ) );

    auto post = WorkflowRepairEngine::inspectContracts( healed );
    REQUIRE( post.isEmpty() ); // repair invariant
}

TEST_CASE( "DN into a BOA input injects the calibration + atmospheric chain", "[d17][workflow][repair]" )
{
    WorkflowDocument def;
    def.nodes = {
        makeNode( QStringLiteral( "dn_src" ), {},
                  { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                          QStringLiteral( "DN" ), 10.0, 10.0, 4 ) } ),
        makeNode( QStringLiteral( "ndvi" ),
                  { port( QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                          QStringLiteral( "BOA" ), 10.0, 10.0, 4, true ) },
                  {} ),
    };
    def.edges = { EdgeFact{ QStringLiteral( "e_rad" ), QStringLiteral( "dn_src" ), QStringLiteral( "output" ),
                            QStringLiteral( "ndvi" ), QStringLiteral( "input" ) } };

    auto violations = WorkflowRepairEngine::inspectContracts( def );
    REQUIRE( violations.size() == 1 );
    REQUIRE( violations[0].mismatchType == ContractMismatchType::RadiometricStateMismatch );
    REQUIRE( violations[0].actualSpecification == QStringLiteral( "DN" ) );
    REQUIRE( violations[0].expectedSpecification == QStringLiteral( "BOA" ) );

    const RepairPlan plan = WorkflowRepairEngine::inferRepairs( def );
    REQUIRE( plan.suggestedActions.size() == 2 );
    // Physics order: calibration first, atmospheric correction second.
    REQUIRE( plan.suggestedActions[0].insertOperatorId == QStringLiteral( "rs:radiometric_calibration" ) );
    REQUIRE( plan.suggestedActions[1].insertOperatorId == QStringLiteral( "rs:atmospheric_correction" ) );

    const WorkflowDocument healed = WorkflowRepairEngine::applyRepairPlan( def, plan );
    REQUIRE( healed.nodes.size() == def.nodes.size() + 2 );
    REQUIRE( WorkflowIR::validateSemantics( healed ) );

    // Chain order in the edge list: src -> calib -> atmosphere -> ndvi.
    REQUIRE( healed.findEdge( "re_e_rad_0" )->targetNodeId == QStringLiteral( "adapter_e_rad_radiometric_calibration" ) );
    REQUIRE( healed.findEdge( "re_e_rad_1" )->sourceNodeId == QStringLiteral( "adapter_e_rad_radiometric_calibration" ) );
    REQUIRE( healed.findEdge( "re_e_rad_1" )->targetNodeId == QStringLiteral( "adapter_e_rad_atmospheric_correction" ) );
    REQUIRE( healed.findEdge( "re_e_rad_final" )->sourceNodeId == QStringLiteral( "adapter_e_rad_atmospheric_correction" ) );

    REQUIRE( WorkflowRepairEngine::inspectContracts( healed ).isEmpty() );
}

TEST_CASE( "Radiance into TOA needs only the atmospheric adapter", "[d17][workflow][repair]" )
{
    WorkflowDocument def;
    def.nodes = {
        makeNode( QStringLiteral( "rad_src" ), {},
                  { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                          QStringLiteral( "Radiance" ), 10.0, 10.0, 4 ) } ),
        makeNode( QStringLiteral( "consumer" ),
                  { port( QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                          QStringLiteral( "TOA" ), 10.0, 10.0, 4, true ) },
                  {} ),
    };
    def.edges = { EdgeFact{ QStringLiteral( "e" ), QStringLiteral( "rad_src" ), QStringLiteral( "output" ),
                            QStringLiteral( "consumer" ), QStringLiteral( "input" ) } };

    const RepairPlan plan = WorkflowRepairEngine::inferRepairs( def );
    REQUIRE( plan.suggestedActions.size() == 1 );
    REQUIRE( plan.suggestedActions[0].insertOperatorId == QStringLiteral( "rs:atmospheric_correction" ) );

    REQUIRE( WorkflowRepairEngine::inspectContracts( WorkflowRepairEngine::applyRepairPlan( def, plan ) ).isEmpty() );
}

TEST_CASE( "Resolution gap injects a resample adapter with target cell size", "[d17][workflow][repair]" )
{
    WorkflowDocument def;
    def.nodes = {
        makeNode( QStringLiteral( "coarse" ), {},
                  { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                          QStringLiteral( "BOA" ), 30.0, 30.0, 1 ) } ),
        makeNode( QStringLiteral( "fine" ),
                  { port( QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "*" ),
                          QStringLiteral( "BOA" ), 10.0, 10.0, 1, true ) },
                  {} ),
    };
    def.edges = { EdgeFact{ QStringLiteral( "e_res" ), QStringLiteral( "coarse" ), QStringLiteral( "output" ),
                            QStringLiteral( "fine" ), QStringLiteral( "input" ) } };

    auto violations = WorkflowRepairEngine::inspectContracts( def );
    REQUIRE( violations.size() == 1 );
    REQUIRE( violations[0].mismatchType == ContractMismatchType::ResolutionMismatch );

    const RepairPlan plan = WorkflowRepairEngine::inferRepairs( def );
    REQUIRE( plan.suggestedActions.size() == 1 );
    REQUIRE( plan.suggestedActions[0].insertOperatorId == QStringLiteral( "rs:resample" ) );
    REQUIRE( plan.suggestedActions[0].adapterParameters["target_resolution_x"].toDouble() == 10.0 );
    REQUIRE( plan.suggestedActions[0].adapterParameters["target_resolution_y"].toDouble() == 10.0 );

    REQUIRE( WorkflowRepairEngine::inspectContracts( WorkflowRepairEngine::applyRepairPlan( def, plan ) ).isEmpty() );
}

TEST_CASE( "Wildcard facts and clean pipelines produce no violations", "[d17][workflow][repair]" )
{
    // "*" CRS / state / dtype on the consumer side accepts anything.
    WorkflowDocument def;
    def.nodes = {
        makeNode( QStringLiteral( "any_src" ), {},
                  { port( QStringLiteral( "output" ), QStringLiteral( "Table" ), QStringLiteral( "EPSG:9999" ),
                          QStringLiteral( "DN" ), 77.0, 77.0, 9 ) } ),
        makeNode( QStringLiteral( "lenient" ),
                  { port( QStringLiteral( "input" ), QStringLiteral( "*" ), QStringLiteral( "*" ),
                          QStringLiteral( "*" ), 0, 0, 0, true ) },
                  {} ),
    };
    def.edges = { EdgeFact{ QStringLiteral( "e_ok" ), QStringLiteral( "any_src" ), QStringLiteral( "output" ),
                            QStringLiteral( "lenient" ), QStringLiteral( "input" ) } };
    REQUIRE( WorkflowRepairEngine::inspectContracts( def ).isEmpty() );

    const RepairPlan plan = WorkflowRepairEngine::inferRepairs( def );
    REQUIRE_FALSE( plan.requiresRepair );
    REQUIRE( WorkflowRepairEngine::applyRepairPlan( def, plan ) == def ); // no-op plan is identity
}

TEST_CASE( "Compound break (CRS + resolution + radiometric) cascades in rule order", "[d17][workflow][repair]" )
{
    const WorkflowDocument broken = crsMismatchWorkflow(); // 4326/DN/30m into 32649/BOA/10m

    auto violations = WorkflowRepairEngine::inspectContracts( broken );
    REQUIRE( violations.size() == 3 );
    // Fixed enumeration order: Crs, Resolution, Radiometric.
    REQUIRE( violations[0].mismatchType == ContractMismatchType::CrsMismatch );
    REQUIRE( violations[1].mismatchType == ContractMismatchType::ResolutionMismatch );
    REQUIRE( violations[2].mismatchType == ContractMismatchType::RadiometricStateMismatch );

    const RepairPlan plan = WorkflowRepairEngine::inferRepairs( broken );
    REQUIRE( plan.suggestedActions.size() == 4 ); // reproject, resample, calibration, atmosphere
    REQUIRE( plan.suggestedActions[0].insertOperatorId == QStringLiteral( "rs:reproject" ) );
    REQUIRE( plan.suggestedActions[1].insertOperatorId == QStringLiteral( "rs:resample" ) );
    REQUIRE( plan.suggestedActions[2].insertOperatorId == QStringLiteral( "rs:radiometric_calibration" ) );
    REQUIRE( plan.suggestedActions[3].insertOperatorId == QStringLiteral( "rs:atmospheric_correction" ) );

    const WorkflowDocument healed = WorkflowRepairEngine::applyRepairPlan( broken, plan );
    REQUIRE( healed.nodes.size() == broken.nodes.size() + 4 );
    REQUIRE( WorkflowIR::validateSemantics( healed ) );
    REQUIRE( WorkflowRepairEngine::inspectContracts( healed ).isEmpty() ); // the invariant, compound case
}

TEST_CASE( "Two identical breaks on different edges get collision-free adapter ids", "[d17][workflow][repair]" )
{
    auto crsConsumer = []( const QString &id ) {
        return makeNode( id,
                         { port( QStringLiteral( "input" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:32649" ),
                                 QStringLiteral( "BOA" ), 10.0, 10.0, 1, true ) },
                         {} );
    };
    WorkflowDocument def;
    def.nodes = {
        makeNode( QStringLiteral( "wgs_src" ), {},
                  { port( QStringLiteral( "output" ), QStringLiteral( "Raster" ), QStringLiteral( "EPSG:4326" ),
                          QStringLiteral( "BOA" ), 10.0, 10.0, 1 ) } ),
        crsConsumer( QStringLiteral( "consumer_a" ) ),
        crsConsumer( QStringLiteral( "consumer_b" ) ),
    };
    def.edges = {
        EdgeFact{ QStringLiteral( "edge_a" ), QStringLiteral( "wgs_src" ), QStringLiteral( "output" ),
                  QStringLiteral( "consumer_a" ), QStringLiteral( "input" ) },
        EdgeFact{ QStringLiteral( "edge_b" ), QStringLiteral( "wgs_src" ), QStringLiteral( "output" ),
                  QStringLiteral( "consumer_b" ), QStringLiteral( "input" ) },
    };

    const RepairPlan plan = WorkflowRepairEngine::inferRepairs( def );
    REQUIRE( plan.suggestedActions.size() == 2 );

    const WorkflowDocument healed = WorkflowRepairEngine::applyRepairPlan( def, plan );
    REQUIRE( healed.nodes.size() == def.nodes.size() + 2 );
    // Distinct adapter node ids (per-edge naming, not a shared counter).
    QSet<QString> ids;
    for ( const NodeFact &node : healed.nodes )
    {
        REQUIRE_FALSE( ids.contains( node.nodeId ) ); // uniqueness also guards the IR invariant
        ids.insert( node.nodeId );
    }
    REQUIRE( WorkflowIR::validateSemantics( healed ) );
    REQUIRE( WorkflowRepairEngine::inspectContracts( healed ).isEmpty() );
}

TEST_CASE( "Repair is deterministic: same workflow, byte-identical healed document", "[d17][workflow][repair]" )
{
    const WorkflowDocument broken = crsMismatchWorkflow();
    const RepairPlan planA = WorkflowRepairEngine::inferRepairs( broken );
    const RepairPlan planB = WorkflowRepairEngine::inferRepairs( broken );

    const QJsonDocument docA = QJsonDocument( WorkflowIR::toJson( WorkflowRepairEngine::applyRepairPlan( broken, planA ) ) );
    const QJsonDocument docB = QJsonDocument( WorkflowIR::toJson( WorkflowRepairEngine::applyRepairPlan( broken, planB ) ) );
    REQUIRE( docA.toJson( QJsonDocument::Indented ) == docB.toJson( QJsonDocument::Indented ) );
}
