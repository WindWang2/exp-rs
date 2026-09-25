// tests/test_planner_handoff_e2e.cpp — R3 track 14, seams 2/4/6 (Qt-free lane).
//
// The handoff chain up to the WorkflowIR document boundary:
//
//   science_context bundle ─┐
//   user goal               ├→ planner inputs → planScientificWork (live
//   selected assets ────────┘   capability authority) → ScientificPlan
//     → projectPlanToIr (the workflow_ir 1.0-shaped document)
//     → per-node preflight (real rules + real mirror + real repair routing)
//     → repair planning when findings exist
//     → teaching / proposal / identity round-trips.
//
// The steps BEYOND the document boundary (readWorkflowIr → AgentPlan v2 →
// engine JSON → compile/run/verify) need the full harness link; they live in
// test_workflow_planner (R3 cases at the bottom of that file), which is the
// one target allowed to pull the QGIS-linked chain.
//
// Oracles pinned here:
//   * per family (measurement/change/classification/temporal): the projected
//     plan lowers to a workflow_ir document whose every operator is
//     contract-declared and whose artifact domains ride the explicit
//     contracts→artifact_facts map (degradation stays loud);
//   * plan identity and provenance survive every projection (planId in
//     node.source, ir_id, teaching views, JSON round-trip);
//   * a hostile external proposal cannot pass validation (unknown operator,
//     forged identity, contract-contradicting transitions) and therefore
//     cannot reach lowering with borrowed authority;
//   * preflight gates the plan's analysis node over the REAL mirror, and a
//     blocking finding routes into repair planning over the REAL routing
//     table — offered or typed-refused, never dropped;
//   * the whole chain replans byte-identically for the same input + authority.
#include <catch2/catch_test_macros.hpp>

#include "contracts/scientific_contract.h"

#include "planner/json_util.h"
#include "planner/live/capability_live.h"
#include "planner/planner_core.h"
#include "planner/plan_ir_projection.h"
#include "planner/plan_teaching.h"
#include "planner/planner_proposal.h"
#include "planner/planning_context.h"
#include "planner/scientific_goal.h"
#include "planner/scientific_plan.h"

#include "preflight/capability_mirror.h"
#include "preflight/engine.h"
#include "preflight/provider.h"
#include "preflight/report.h"
#include "preflight/rules.h"

#include "repair_planner/repair_planner.h"
#include "repair_planner/repair_provider.h"
#include "repair_planner/repair_requirement.h"

#include "science_context/bundle.h"
#include "science_context/planner_goal_projection.h"

#include <json/json.h>

#include <string>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

using namespace sicnu::planner;
using namespace sicnu::science_context;
using namespace sicnu::preflight;
using namespace sicnu::repair;

namespace
{

struct FamilyScenario
{
    std::string intent;        // science_context bundle intent
    std::string radiometric;   // passport unit of the selected asset
    std::string domain;        // expected contracts numeric domain after projection
    std::string expectedKind;  // expected planner goal kind
};

const sicnu::planner::LiveCapabilityProvider &liveProvider()
{
    static const sicnu::planner::LiveCapabilityProvider provider = [] {
        CapabilityMirrorProjection mirror;
        REQUIRE( mirror.loadDirectory( std::string( CMAKE_SOURCE_DIR )
                                       + "/data/agent/capabilities" ) > 0 );
        sicnu::planner::LiveCapabilityProvider provider;
        std::string error;
        REQUIRE( sicnu::planner::LiveCapabilityProvider::create( mirror, provider, error ) );
        return provider;
    }();
    return provider;
}

PlannerProjectionResult project( const FamilyScenario &scenario, bool readyAsset )
{
    ScientificContextBundle bundle;
    bundle.bundleId = "bnd-e2e-" + scenario.intent;
    bundle.goal = "e2e " + scenario.intent + " over scene-a";
    bundle.intent = scenario.intent;
    AssetSummary asset;
    asset.assetId = "scene-a";
    asset.modality = "optical";
    asset.radiometricUnit = scenario.radiometric;
    asset.crsAuthid = "EPSG:32633";
    asset.bandRoles = { "blue", "green", "red", "nir" };
    asset.evidence = EvidenceBucket::Known;
    bundle.assets.push_back( asset );
    bundle.sources.assets = SectionSource{ ContentSource::LiveAuthority,
                                           "scientific_state.passports", 7, false };

    PlannerAssetEnrichment enrichment;
    if ( readyAsset )
    {
        PlannerAssetFacts facts;
        facts.state = "ready";
        facts.resolutionM = 10.0;
        enrichment.assets["scene-a"] = facts;
        enrichment.source = "scientific_state.passports";
        enrichment.revision = 7;
    }
    return projectPlannerInputs( bundle, enrichment );
}

PlanningResult plan( const PlannerProjectionResult &projection )
{
    PlannerProviders providers;
    providers.capability = &liveProvider();
    return planScientificWork( projection.goal, projection.context, providers );
}

PlanningResult plannedPrimary( const FamilyScenario &scenario )
{
    const PlannerProjectionResult projection = project( scenario, true );
    REQUIRE( projection.goal.kind == scenario.expectedKind );
    REQUIRE( projection.unresolved.empty() );
    REQUIRE( projection.context.assets[0].numericDomain == scenario.domain );
    const PlanningResult result = plan( projection );
    REQUIRE( result.primary() != nullptr );
    REQUIRE( result.primary()->verdict != "infeasible" );
    return result;
}

PreflightReport preflightPlanAnalysis( const ScientificPlan &plan, bool withNir )
{
    CapabilityMirrorProjection mirror;
    REQUIRE( mirror.loadDirectory( std::string( CMAKE_SOURCE_DIR )
                                   + "/data/agent/capabilities" ) > 0 );

    MemoryFactsProvider facts;
    SlotFacts slot;
    slot.kind = "raster";
    slot.modality = "optical";
    slot.hasCrs = true;
    slot.crsAuthid = "EPSG:32633";
    slot.crsProjected = true;
    slot.hasPixelSize = true;
    slot.pixelSizeX = slot.pixelSizeY = 10.0;
    slot.radiometricUnit = "surface_reflectance";
    int index = 0;
    for ( const char *role : { "blue", "green", "red" } )
    {
        BandFacts band;
        band.index = ++index;
        band.role = role;
        slot.bands.push_back( band );
    }
    if ( withNir )
    {
        BandFacts band;
        band.index = ++index;
        band.role = "nir";
        slot.bands.push_back( band );
    }
    facts.set( "scene-a", slot );

    PreflightEngine engine;
    for ( auto &rule : builtinRules() )
        REQUIRE( engine.registerRule( std::move( rule ) ) == RegistrationResult::Ok );

    PreflightReport last;
    bool ran = false;
    for ( const auto &step : plan.steps )
    {
        if ( step.role != "analyze" )
            continue;
        PreflightRequest request;
        request.operatorId = step.operatorId;
        request.operatorParams = step.params;
        request.mode = plan.modeKind; // request validity gates on agent|teaching
        request.inputs = { { "input", "scene-a" } };
        last = engine.evaluate( request, facts, mirror );
        ran = true;
    }
    REQUIRE( ran );
    return last;
}

} // namespace

TEST_CASE( "Each family's projected plan lowers to a contract-aligned IR document",
           "[handoff_e2e]" )
{
    const std::vector<FamilyScenario> scenarios = {
        { "ndvi", "surface_reflectance", "reflectance", "measurement" },
        { "change", "surface_reflectance", "reflectance", "change" },
        { "classify", "surface_reflectance", "reflectance", "classification" },
        { "temporal", "surface_reflectance", "reflectance", "temporal_analysis" },
    };
    for ( const auto &scenario : scenarios )
    {
        INFO( "intent " << scenario.intent );
        const PlanningResult result = plannedPrimary( scenario );
        const ScientificPlan &primary = *result.primary();

        std::vector<std::string> warnings;
        std::string error;
        const Json::Value irDoc = projectPlanToIr( primary, &warnings, &error );
        REQUIRE( error.empty() );
        REQUIRE( irDoc["kind"].asString() == "workflow_ir" );
        REQUIRE( irDoc["schema_version"].asString() == "1.0" );
        REQUIRE( irDoc["ir_id"].asString().rfind( "wir-", 0 ) == 0 );
        REQUIRE( irDoc["goal"].asString() == primary.goalId );

        // Every node names its planner plan; every operator survives the
        // contracts registry (checked node-side, mirror-side in the S1 suite).
        bool sawAnalysis = false;
        for ( const auto &node : irDoc["nodes"] )
        {
            REQUIRE( node["source"].asString().rfind( "planner:" + primary.planId, 0 ) == 0 );
            if ( node["semantic_output"].asString().rfind( "analyze", 0 ) == 0 )
            {
                sawAnalysis = true;
                REQUIRE( sicnu::contracts::findScientificContract(
                             node["operator"].asString() ) != nullptr );
            }
        }
        REQUIRE( sawAnalysis );

        // Honesty: measurement goals degrade to no harness intent, WITH the
        // documented warning — never a claimed preflight.
        if ( scenario.expectedKind == "measurement" )
            REQUIRE_FALSE( warnings.empty() );
    }
}

TEST_CASE( "Plan identity round-trips through JSON, IR and teaching projections",
           "[handoff_e2e]" )
{
    const FamilyScenario scenario{ "ndvi", "surface_reflectance", "reflectance",
                                   "measurement" };
    const PlanningResult result = plannedPrimary( scenario );
    const ScientificPlan &primary = *result.primary();

    // JSON round-trip preserves identity (planId is content-derived).
    ScientificPlan reread;
    std::string error;
    REQUIRE( scientificPlanFromJson( scientificPlanToJson( primary ), reread, error ) );
    REQUIRE( reread.planId == primary.planId );
    REQUIRE( scientificPlanFingerprint( reread ) == scientificPlanFingerprint( primary ) );

    // A re-projection of the reread plan is the same IR document
    // (provenance-stable identity, byte-equal).
    const Json::Value irOne = projectPlanToIr( primary, nullptr, &error );
    const Json::Value irTwo = projectPlanToIr( reread, nullptr, &error );
    REQUIRE( irOne["ir_id"].asString() == irTwo["ir_id"].asString() );
    REQUIRE( json_util::canonicalCompact( irOne ) == json_util::canonicalCompact( irTwo ) );

    // Teaching projection exists for the plan's own mode (masking_applied
    // stays honest in either direction).
    ModePolicy mode;
    mode.kind = primary.modeKind;
    mode.autonomy = primary.autonomy;
    const TeachingViews teaching = teachingViews( primary, mode );
    REQUIRE( teaching.hiddenAnswer.isObject() );
    REQUIRE( teaching.explanation.isObject() );
}

TEST_CASE( "Hostile proposals are rejected before any lowering can borrow authority",
           "[handoff_e2e]" )
{
    const FamilyScenario scenario{ "ndvi", "surface_reflectance", "reflectance",
                                   "measurement" };
    const PlanningResult result = plannedPrimary( scenario );
    PlannerProviders providers;
    providers.capability = &liveProvider();

    SECTION( "proposal naming an operator no authority declares" )
    {
        Json::Value hostile = scientificPlanToJson( *result.primary() );
        REQUIRE( hostile["steps"].isArray() );
        REQUIRE( hostile["steps"].size() > 0 );
        hostile["steps"][0]["operator_id"] = "rs:total_wipe";
        const ProposalOutcome outcome =
            validateProposal( hostile, project( scenario, true ).goal,
                              project( scenario, true ).context, providers );
        REQUIRE_FALSE( outcome.accepted );
        REQUIRE( isKnownProposalRejectionCode( outcome.rejection.code ) );
        REQUIRE( outcome.rejection.code == "planner:proposal_unknown_operator" );
    }

    SECTION( "proposal with a forged identity is re-minted from content" )
    {
        Json::Value forged = scientificPlanToJson( *result.primary() );
        forged["plan_id"] = "plan-forged-by-attacker";
        const ProposalOutcome outcome =
            validateProposal( forged, project( scenario, true ).goal,
                              project( scenario, true ).context, providers );
        REQUIRE( outcome.accepted ); // content was lawful...
        REQUIRE( outcome.plan.planId == result.primary()->planId ); // ...identity was NOT taken
        REQUIRE( outcome.plan.planId != "plan-forged-by-attacker" );
    }

    SECTION( "proposal claiming a transition the contracts forbid" )
    {
        Json::Value contradicting = scientificPlanToJson( *result.primary() );
        REQUIRE( contradicting["steps"].isArray() );
        // Rewrite the analysis step into a contracts violation: index-family
        // rs:ndvi cannot honestly produce "classes".
        for ( auto &step : contradicting["steps"] )
        {
            if ( step["role"].asString() == "analyze" )
            {
                step["expected_transitions"] = Json::Value( Json::arrayValue );
                Json::Value transition( Json::objectValue );
                transition["ref"] = "scene-a";
                transition["from_domain"] = "reflectance";
                transition["to_domain"] = "classes";
                step["expected_transitions"].append( transition );
            }
        }
        const ProposalOutcome outcome =
            validateProposal( contradicting, project( scenario, true ).goal,
                              project( scenario, true ).context, providers );
        REQUIRE_FALSE( outcome.accepted );
        REQUIRE( isKnownProposalRejectionCode( outcome.rejection.code ) );
    }
}

TEST_CASE( "Preflight gates the plan's analysis node and repair planning answers findings",
           "[handoff_e2e]" )
{
    const FamilyScenario scenario{ "ndvi", "surface_reflectance", "reflectance",
                                   "measurement" };
    const PlanningResult result = plannedPrimary( scenario );
    REQUIRE( result.primary() != nullptr );

    SECTION( "lawful facts: no blocking findings" )
    {
        const PreflightReport report = preflightPlanAnalysis( *result.primary(), true );
        for ( const auto &finding : report.findings )
            INFO( finding.toJson()["code"].asString() << " @ "
                  << finding.toJson()["subject"].asString() );
        INFO( "verdict " << report.verdict );
        REQUIRE( report.verdict != "blocked" );
    }

    SECTION( "missing nir on a variant-typed request blocks and routes into repair planning" )
    {
        // The planner's own step carries no params (decision layer); the
        // executed science is the goal's VARIANT — rs:spectral_index with
        // index=NDVI, whose capability knowledge demands red+nir. Preflight
        // gates the variant, not the bare id.
        PreflightReport report;
        {
            CapabilityMirrorProjection mirror;
            REQUIRE( mirror.loadDirectory( std::string( CMAKE_SOURCE_DIR )
                                           + "/data/agent/capabilities" ) > 0 );
            MemoryFactsProvider facts;
            SlotFacts slot;
            slot.kind = "raster";
            slot.modality = "optical";
            slot.hasCrs = true;
            slot.crsAuthid = "EPSG:32633";
            slot.crsProjected = true;
            slot.radiometricUnit = "surface_reflectance";
            int index = 0;
            for ( const char *role : { "blue", "green", "red" } )
            {
                BandFacts band;
                band.index = ++index;
                band.role = role;
                slot.bands.push_back( band );
            }
            facts.set( "scene-a", slot );

            PreflightEngine engine;
            for ( auto &rule : builtinRules() )
                REQUIRE( engine.registerRule( std::move( rule ) ) == RegistrationResult::Ok );

            PreflightRequest request;
            request.operatorId = "rs:spectral_index";
            request.operatorParams = Json::Value( Json::objectValue );
            request.operatorParams["index"] = "NDVI";
            request.mode = "agent";
            request.inputs = { { "input", "scene-a" } };
            report = engine.evaluate( request, facts, mirror );
        }
        REQUIRE( report.verdict == "blocked" );
        bool sawMissingBand = false;
        std::vector<Json::Value> findingDocs;
        for ( const auto &finding : report.findings )
        {
            const Json::Value doc = finding.toJson();
            findingDocs.push_back( doc );
            if ( doc["code"].asString() == "SPF_BAND_ROLE_MISSING" )
                sawMissingBand = true;
        }
        REQUIRE( sawMissingBand );

        // The real mirror documents route the finding to serving operators.
        CapabilityMirrorProjection mirror;
        REQUIRE( mirror.loadDirectory( std::string( CMAKE_SOURCE_DIR )
                                       + "/data/agent/capabilities" ) > 0 );
        std::vector<Json::Value> entries;
        for ( const auto &id : mirror.entryIds() )
        {
            const CapabilityEntryResult entry = mirror.entryForOperator( id, {} );
            if ( entry.status == FactStatus::Available )
                entries.push_back( entry.entry );
        }
        JsonRepairCapabilityProvider provider;
        RepairError providerError;
        REQUIRE( JsonRepairCapabilityProvider::buildFromCapabilityEntries( entries, provider,
                                                                          providerError ) );

        RepairPolicyContext policy;
        RepairPlannerOptions options;
        options.intent = "ndvi";
        RepairPlannerOutcome outcome;
        RepairError repairError;
        REQUIRE( planRepairsForFindings( findingDocs, provider, policy, options, outcome,
                                         repairError ) );
        // The band-role requirement is planned over the real authority:
        // offered, or typed-refused — never dropped, never guessed.
        REQUIRE( outcome.plan.requirements.isArray() );
        REQUIRE( outcome.plan.requirements.size() >= 1 );
        bool bandRolePlanned = false;
        for ( const auto &requirement : outcome.plan.requirements )
        {
            if ( requirement["kind"].asString() == "band_role" )
                bandRolePlanned = true;
        }
        REQUIRE( bandRolePlanned );
    }
}

TEST_CASE( "Every actionable engine finding code routes into a repair requirement kind",
           "[handoff_e2e]" )
{
    // Drift pin for the R3 seam fix: the Scientific Preflight Engine's
    // _MISMATCH/_MISSING/_INVALID family describes actionable data-prep
    // needs and MUST route to a requirement kind — an engine blocking
    // finding that degrades to `unsupported` kills the preflight→repair
    // seam. UNKNOWN codes describe facts the authority could not answer and
    // stay deliberately unmapped (a repair over an unknown would fabricate
    // feasibility).
    struct Route
    {
        const char *code;
        const char *kind;
    };
    const std::vector<Route> actionable = {
        { "SPF_BAND_ROLE_MISSING", "band_role" },
        { "SPF_CRS_MISMATCH", "crs_align" },
        { "SPF_GRID_RESOLUTION_MISMATCH", "grid_align" },
        { "SPF_RADIOMETRIC_STATE_MISMATCH", "radiometric_state" },
        { "SPF_MODALITY_MISMATCH", "modality_check" },
        { "SPF_MODEL_INCOMPATIBLE", "model_contract" },
        { "SPF_TRAIN_EVAL_LEAKAGE", "training_data" },
        { "SPF_TEMPORAL_ORDER_INVALID", "temporal_align" },
        { "SPF_TEMPORAL_GAP_EXCEEDED", "temporal_align" },
        { "SPF_TEMPORAL_SCENES_INSUFFICIENT", "temporal_align" },
        { "SPF_CLOUD_COVER_HIGH", "quality_mask" },
    };
    for ( const auto &route : actionable )
    {
        INFO( route.code );
        REQUIRE( requirementKindForFindingCode( route.code ) == route.kind );
    }
    for ( const char *unknown : { "SPF_BAND_ROLE_UNKNOWN", "SPF_CRS_UNKNOWN",
                                  "SPF_RADIOMETRIC_STATE_UNKNOWN", "SPF_MODALITY_UNKNOWN",
                                  "SPF_MODEL_UNKNOWN", "SPF_RESOLUTION_UNKNOWN",
                                  "SPF_TEMPORAL_UNKNOWN", "SPF_LEAKAGE_UNKNOWN",
                                  "SPF_OPERATOR_UNKNOWN", "SPF_REQUEST_INVALID",
                                  "SPF_BUDGET_EXCEEDED",
                                  "SPF_CAPABILITY_MIRROR_UNAVAILABLE" } )
    {
        INFO( unknown );
        REQUIRE( requirementKindForFindingCode( unknown ).empty() );
    }
}

TEST_CASE( "The whole handoff chain replans byte-identically", "[handoff_e2e]" )
{
    const FamilyScenario scenario{ "ndvi", "surface_reflectance", "reflectance",
                                   "measurement" };
    auto runChain = [&]() {
        const PlanningResult result = plannedPrimary( scenario );
        std::string error;
        const Json::Value irDoc = projectPlanToIr( *result.primary(), nullptr, &error );
        REQUIRE( error.empty() );
        return json_util::canonicalCompact( scientificPlanToJson( *result.primary() ) ) + "|"
               + json_util::canonicalCompact( irDoc );
    };
    REQUIRE( runChain() == runChain() );
}
