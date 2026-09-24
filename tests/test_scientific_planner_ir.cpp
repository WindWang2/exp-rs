// tests/test_scientific_planner_ir.cpp — slice F:
// workflow_ir 1.0-shaped projection: field shapes conform to the harness
// reader's document contract (fixture-pinned), the contracts→artifact_facts
// map fully covers the linked domain vocabulary, honest unknowns carry
// warnings, state-less steps and empty plans refuse projection.
#include <catch2/catch_test_macros.hpp>

#include "agent/harness/intent_vocabulary.h"
#include "contracts/scientific_contract.h"
#include "planner/plan_ir_projection.h"
#include "planner/planner_rules.h"

#include <json/json.h>

#include <set>

using namespace sicnu::planner;

namespace
{
ScientificPlan projectablePlan()
{
    ScientificPlan plan;
    plan.planId = "plan-ir";
    plan.goalId = "goal-water";
    plan.goalKind = "change";
    plan.rulesRevision = kPlannerRulesRevision;
    plan.modeKind = "agent";
    plan.autonomy = "full";
    plan.verdict = "feasible";

    PlannerStep calibrate;
    calibrate.stepId = "step-preprocess-01";
    calibrate.role = "preprocess";
    calibrate.operatorId = "rs:brdf_normalization";
    calibrate.family = "calibration";
    calibrate.inputs = { StepInput{ "", "asset-a", "input" } };
    calibrate.expectedTransitions = { ExpectedTransition{ "asset-a", "dn", "reflectance" } };
    calibrate.costClass = "medium";
    calibrate.estimatedRamMb = 512;

    PlannerStep analyze;
    analyze.stepId = "step-analyze-01";
    analyze.role = "analyze";
    analyze.operatorId = "rs:mndwi";
    analyze.family = "analysis";
    analyze.inputs = { StepInput{ "step-preprocess-01", "", "input" } };
    analyze.expectedTransitions = { ExpectedTransition{ "asset-a", "reflectance", "index" } };
    analyze.costClass = "low";
    analyze.estimatedRamMb = 256;

    PlannerStep publish;
    publish.stepId = "step-publish-01";
    publish.role = "publish";
    publish.operatorId = "gdal:translate";
    publish.family = "publication";
    publish.inputs = { StepInput{ "step-analyze-01", "", "input" } };
    publish.costClass = "low";

    plan.steps = { calibrate, analyze, publish };
    plan.cost = PlanCost{ "medium", 768 };
    return plan;
}
} // namespace

TEST_CASE( "projected document carries the workflow_ir 1.0 field shape",
           "[scientific_planner][ir]" )
{
    const ScientificPlan plan = projectablePlan();
    std::vector<std::string> warnings;
    std::string error;
    const Json::Value doc = projectPlanToIr( plan, &warnings, &error );
    REQUIRE( error.empty() );
    CHECK( doc["kind"].asString() == std::string( "workflow_ir" ) );
    CHECK( doc["schema_version"].asString() == std::string( "1.0" ) );
    CHECK( doc["goal"].asString() == std::string( "goal-water" ) );
    // change → the harness "change" intent
    CHECK( doc["intent"].asString() == std::string( "change" ) );

    REQUIRE( doc["nodes"].size() == 3 );
    const Json::Value &calibrate = doc["nodes"][0];
    CHECK( calibrate["id"].asString() == std::string( "step-preprocess-01" ) );
    CHECK( calibrate["operator"].asString() == std::string( "rs:brdf_normalization" ) );
    CHECK( calibrate["resource_estimate_mb"].asInt64() == 512 );
    CHECK( std::string( calibrate["source"].asString() ).rfind( "planner:plan-ir", 0 ) == 0 );
    // contracts reflectance → the honest artifact_facts token surface_reflectance
    CHECK( calibrate["outputs"][0]["artifact"]["numeric_domain"].asString()
           == std::string( "surface_reflectance" ) );
    // node wiring uses the node/output/as form
    CHECK( doc["nodes"][1]["inputs"][0]["node"].asString() == std::string( "step-preprocess-01" ) );
    CHECK( doc["nodes"][1]["inputs"][0]["output"].asString() == std::string( "output" ) );
    // asset input becomes a document input slot
    REQUIRE( doc["inputs"].size() == 1 );
    CHECK( doc["inputs"][0]["reference"].asString() == std::string( "asset-a" ) );
    // publish step declares the output
    REQUIRE( doc["outputs"].size() == 1 );
    CHECK( doc["outputs"][0]["node"].asString() == std::string( "step-publish-01" ) );
    // expectations present
    CHECK( doc["expectations"]["max_ram_mb"].asInt64() == 768 );
    CHECK( warnings.empty() ); // dn→reflectance→index all map honestly
}

TEST_CASE( "domain map covers the FULL contracts vocabulary with honest unknowns",
           "[scientific_planner][ir][drift]" )
{
    // Every contracts domain must have a map verdict; the drift test fails
    // when contracts grows a domain the map does not know.
    const std::set<std::string> kDegrading = {
        "radiance", "temperature", "amplitude", "phase", "displacement",
        "probability", "features", "count", "vector", "table",
    };
    for ( const auto &domain : sicnu::contracts::kNumericDomains )
    {
        const std::string token = artifactFactsTokenForDomain( domain );
        if ( domain == "none" || domain == "any" )
            CHECK( token.empty() );
        else if ( kDegrading.count( domain ) )
        {
            CHECK( token == "unknown" );
            CHECK( domainProjectsToUnknown( domain ) );
        }
        else
        {
            CHECK_FALSE( token.empty() );
            CHECK_FALSE( domainProjectsToUnknown( domain ) );
        }
    }
    // spot-check the honest tokens
    CHECK( artifactFactsTokenForDomain( "dn" ) == "dn" );
    CHECK( artifactFactsTokenForDomain( "reflectance" ) == "surface_reflectance" );
    CHECK( artifactFactsTokenForDomain( "sigma0" ) == "linear_power" );
    CHECK( artifactFactsTokenForDomain( "mask" ) == "masked" );
    CHECK( artifactFactsTokenForDomain( "classes" ) == "categorical" );
}

TEST_CASE( "domains without an honest token project to unknown WITH a warning",
           "[scientific_planner][ir]" )
{
    ScientificPlan plan = projectablePlan();
    plan.steps[0].expectedTransitions = { ExpectedTransition{ "asset-a", "dn", "radiance" } };
    plan.steps[1].expectedTransitions = { ExpectedTransition{ "asset-a", "radiance", "index" } };
    std::vector<std::string> warnings;
    std::string error;
    const Json::Value doc = projectPlanToIr( plan, &warnings, &error );
    REQUIRE( error.empty() );
    // the transition to radiance degrades to unknown
    CHECK( doc["nodes"][0]["outputs"][0]["artifact"]["numeric_domain"].asString()
           == std::string( "unknown" ) );
    bool warned = false;
    for ( const auto &warning : warnings )
    {
        if ( warning.find( "radiance" ) != std::string::npos
             && warning.find( "unknown" ) != std::string::npos )
            warned = true;
    }
    CHECK( warned );
}

TEST_CASE( "state-less steps and empty plans refuse projection", "[scientific_planner][ir]" )
{
    ScientificPlan plan = projectablePlan();
    plan.steps.clear();
    std::string error;
    const Json::Value empty = projectPlanToIr( plan, nullptr, &error );
    CHECK( empty.isNull() );
    CHECK( error.find( "without steps" ) != std::string::npos );

    ScientificPlan stateless = projectablePlan();
    PlannerStep ghost;
    ghost.stepId = "step-ghost";
    ghost.role = "record";
    ghost.family = "record";
    ghost.costClass = "low";
    stateless.steps.push_back( ghost );
    error.clear();
    const Json::Value refused = projectPlanToIr( stateless, nullptr, &error );
    CHECK( refused.isNull() );
    CHECK( error.find( "no state basis" ) != std::string::npos );
}

TEST_CASE( "goal kinds without a lawful intent project to empty intent with a warning",
           "[scientific_planner][ir][drift]" )
{
    ScientificPlan plan = projectablePlan();
    plan.goalKind = "measurement";
    std::vector<std::string> warnings;
    std::string error;
    const Json::Value doc = projectPlanToIr( plan, &warnings, &error );
    REQUIRE( error.empty() );
    CHECK( doc["intent"].asString().empty() );
    CHECK_FALSE( warnings.empty() );

    // mapped intents must be members of the harness's closed intent
    // vocabulary (header-only data; the Qt-linked isKnownIntent stays final
    // authority at runtime — data-level conformance, same as the projection)
    const auto knownIntent = []( const std::string &intent )
    {
        for ( const char *candidate : sicnu::agent::harness::kIntentVocabulary )
        {
            if ( intent == candidate )
                return true;
        }
        return false;
    };
    CHECK( knownIntent( harnessIntentForGoalKind( "change" ) ) );
    CHECK( knownIntent( harnessIntentForGoalKind( "classification" ) ) );
    CHECK( knownIntent( harnessIntentForGoalKind( "temporal_analysis" ) ) );
    CHECK( knownIntent( harnessIntentForGoalKind( "monitoring" ) ) );
    CHECK( harnessIntentForGoalKind( "detection" ).empty() );
}

TEST_CASE( "stochastic steps project the stochastic determinism token",
           "[scientific_planner][ir]" )
{
    ScientificPlan plan = projectablePlan();
    plan.steps[1].deterministic = false;
    plan.risks = { PlanRisk{ "stochastic_operator", "analysis is stochastic", "step-analyze-01" } };
    std::string error;
    const Json::Value doc = projectPlanToIr( plan, nullptr, &error );
    REQUIRE( error.empty() );
    CHECK( doc["nodes"][1]["determinism"].asString() == std::string( "stochastic" ) );
    CHECK( doc["nodes"][0]["determinism"].asString().empty() );
    CHECK_FALSE( doc["expectations"]["deterministic"].asBool() );
}
