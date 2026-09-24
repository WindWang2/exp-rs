// tests/test_scientific_planner_teaching.cpp — slice D:
// teaching projections: honest masking policy, guided/minimal masking with no
// answer leakage, standard/agent visibility, pure-function determinism.
#include <catch2/catch_test_macros.hpp>

#include "planner/json_util.h"
#include "planner/plan_teaching.h"
#include "planner/planner_rules.h"

#include <json/json.h>

using namespace sicnu::planner;

namespace
{
ScientificPlan teachingPlan()
{
    ScientificPlan plan;
    plan.planId = "plan-teach";
    plan.goalId = "goal-ndvi";
    plan.goalKind = "measurement";
    plan.rulesRevision = kPlannerRulesRevision;
    plan.modeKind = "teaching";
    plan.autonomy = "guided";
    plan.verdict = "feasible";

    PlannerStep import;
    import.stepId = "step-import-01";
    import.role = "import";
    import.operatorId = "rs:mosaic";
    import.family = "data_import";
    import.costClass = "low";
    import.params = Json::Value( Json::objectValue );

    PlannerStep analyze;
    analyze.stepId = "step-analyze-01";
    analyze.role = "analyze";
    analyze.operatorId = "rs:ndvi";
    analyze.family = "analysis";
    analyze.costClass = "low";
    analyze.studentDecision = true;
    analyze.params = Json::Value( Json::objectValue );
    analyze.params["nir_band"] = 4;
    analyze.params["red_band"] = 3;
    analyze.expectedTransitions = { ExpectedTransition{ "asset-a", "reflectance", "index" } };

    plan.steps = { import, analyze };
    plan.openQuestions = { PlanOpenQuestion{ "q-01-ambiguity", "ambiguity", false,
                                             "which index threshold separates water?", "" } };
    plan.cost = PlanCost{ "low", 256 };
    return plan;
}
} // namespace

TEST_CASE( "guided teaching masks student-decision parameters with no leak",
           "[scientific_planner][teaching]" )
{
    const ScientificPlan plan = teachingPlan();
    const ModePolicy guided{ "teaching", "guided", false };
    const TeachingViews views = teachingViews( plan, guided );

    CHECK( views.hiddenAnswer["masking_applied"].asBool() );
    CHECK( std::string( views.hiddenAnswer["masking_policy"].asString() ) == "teaching:guided" );
    REQUIRE( views.hiddenAnswer["steps"].size() == 2 );
    // infrastructure step stays visible
    CHECK( views.hiddenAnswer["steps"][0]["operator_id"].asString() == std::string( "rs:mosaic" ) );
    // student-decision step masked: no answer keys survive
    const Json::Value &maskedParams = views.hiddenAnswer["steps"][1]["params"];
    CHECK( maskedParams.isMember( "masked" ) );
    CHECK( maskedParams["masked"].asBool() );
    CHECK_FALSE( maskedParams.isMember( "nir_band" ) );
    CHECK_FALSE( maskedParams.isMember( "red_band" ) );
    // structure preserved: step id/role/operator remain
    CHECK( views.hiddenAnswer["steps"][1]["step_id"].asString() == std::string( "step-analyze-01" ) );
    CHECK( views.hiddenAnswer["steps"][1]["operator_id"].asString() == std::string( "rs:ndvi" ) );

    // the RAW plan still carries the answer (masking is a projection)
    const std::string raw = json_util::canonicalCompact( scientificPlanToJson( plan ) );
    CHECK( raw.find( "nir_band" ) != std::string::npos );
    // the masked view does not
    CHECK( json_util::canonicalCompact( views.hiddenAnswer ).find( "nir_band" )
           == std::string::npos );
}

TEST_CASE( "standard mode does not mask; teaching+full does not mask",
           "[scientific_planner][teaching]" )
{
    const ScientificPlan plan = teachingPlan();

    const ModePolicy standard{ "standard", "full", false };
    const TeachingViews standardViews = teachingViews( plan, standard );
    CHECK_FALSE( standardViews.hiddenAnswer["masking_applied"].asBool() );
    CHECK( standardViews.hiddenAnswer["steps"][1]["params"].isMember( "nir_band" ) );

    const ModePolicy teachingFull{ "teaching", "full", false };
    const TeachingViews fullViews = teachingViews( plan, teachingFull );
    CHECK_FALSE( fullViews.hiddenAnswer["masking_applied"].asBool() );
    CHECK( fullViews.hiddenAnswer["steps"][1]["params"].isMember( "nir_band" ) );

    const ModePolicy minimal{ "teaching", "minimal", false };
    const TeachingViews minimalViews = teachingViews( plan, minimal );
    CHECK( minimalViews.hiddenAnswer["masking_applied"].asBool() );
}

TEST_CASE( "explanation view carries rationale, transition whys and thinking questions",
           "[scientific_planner][teaching]" )
{
    const ScientificPlan plan = teachingPlan();
    const ModePolicy guided{ "teaching", "guided", false };
    const TeachingViews views = teachingViews( plan, guided );

    REQUIRE( views.explanation["explanations"].size() == 2 );
    const Json::Value &analyzeExplanation = views.explanation["explanations"][1];
    CHECK( std::string( analyzeExplanation["rationale"].asString() ).find( "measurement" )
           != std::string::npos );
    REQUIRE( analyzeExplanation["transition_whys"].size() == 1 );
    CHECK( std::string( analyzeExplanation["transition_whys"][0].asString() ).find( "reflectance→index" )
           != std::string::npos );
    REQUIRE( views.explanation["thinking_questions"].size() == 1 );
    CHECK( views.explanation["thinking_questions"][0]["prompt"].asString()
           == std::string( "which index threshold separates water?" ) );
}

TEST_CASE( "teaching views are pure functions (same inputs → identical bytes)",
           "[scientific_planner][teaching]" )
{
    const ScientificPlan plan = teachingPlan();
    const ModePolicy guided{ "teaching", "guided", false };
    const TeachingViews first = teachingViews( plan, guided );
    const TeachingViews second = teachingViews( plan, guided );
    CHECK( json_util::canonicalCompact( first.hiddenAnswer )
           == json_util::canonicalCompact( second.hiddenAnswer ) );
    CHECK( json_util::canonicalCompact( first.explanation )
           == json_util::canonicalCompact( second.explanation ) );
}
