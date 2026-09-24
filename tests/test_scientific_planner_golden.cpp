// tests/test_scientific_planner_golden.cpp — slice G:
// self-contained offline golden scenarios: fail-closed readers, deterministic
// replay, byte-stable canonical plans, fingerprints, teaching masking.
// Regeneration is an explicit local opt-in (SICNU_PLANNER_UPDATE_GOLDENS=1)
// and NEVER happens on CI: without the env var a mismatch is a hard failure.
#include <catch2/catch_test_macros.hpp>

#include "planner/plan_teaching.h"
#include "planner/json_util.h"
#include "planner/planner_core.h"

#include <json/json.h>

#include <algorithm>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <map>
#include <string>

using namespace sicnu::planner;

namespace
{
class FixtureProvider : public CapabilityProvider
{
public:
    explicit FixtureProvider( const Json::Value &capabilities )
    {
        const Json::Value::Members families = capabilities.getMemberNames();
        for ( const auto &family : families )
        {
            for ( const auto &entry : capabilities[family] )
            {
                PlannerCapability capability;
                capability.operatorId = entry["operator_id"].asString();
                capability.family = family;
                capability.costClass = entry["cost_class"].asString();
                capability.inputDomain = entry["input_domain"].asString();
                capability.outputDomain = entry["output_domain"].asString();
                capability.deterministic = entry["deterministic"].asBool();
                capability.estimatedRamMb = entry["estimated_ram_mb"].asInt64();
                byFamily_[family].push_back( capability );
            }
        }
    }

    std::vector<PlannerCapability> capabilitiesForFamily( const std::string &family ) const override
    {
        auto it = byFamily_.find( family );
        return it == byFamily_.end() ? std::vector<PlannerCapability>{} : it->second;
    }

private:
    std::map<std::string, std::vector<PlannerCapability>> byFamily_;
};

bool boundedParse( const std::string &text, Json::Value &out, std::string &error )
{
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["stackLimit"] = 1000; // repo parse discipline (no unbounded nesting)
    Json::CharReader *reader = builder.newCharReader();
    const bool ok = reader->parse( text.data(), text.data() + text.size(), &out, &error );
    delete reader;
    return ok;
}

Json::Value loadScenario( const std::string &path )
{
    std::ifstream file( path );
    REQUIRE( file.is_open() );
    std::string text( ( std::istreambuf_iterator<char>( file ) ),
                      std::istreambuf_iterator<char>() );
    Json::Value doc;
    std::string error;
    REQUIRE( boundedParse( text, doc, error ) );
    return doc;
}

/// Splits into '\n'-joined lines (helper for the trailing-space strip below).
std::vector<std::string> &TextLines( std::string &text )
{
    static std::vector<std::string> lines;
    lines.clear();
    std::string current;
    for ( const char c : text )
    {
        if ( c == '\n' )
        {
            lines.push_back( current );
            current.clear();
        }
        else
        {
            current.push_back( c );
        }
    }
    lines.push_back( current );
    text.clear();
    for ( const auto &line : lines )
        text += line + "\n";
    return lines;
}

std::string scenarioPath( const char *name )
{
    return std::string( CMAKE_SOURCE_DIR ) + "/data/planner/golden_scenarios/" + name;
}

/// Rewrites the scenario file with the regenerated fingerprint + canonical
/// plan (pretty-printed into the "expected_plan" object slot).
void updateGolden( const std::string &path, Json::Value scenario, const ScientificPlan &plan )
{
    scenario["expected_fingerprint"] = scientificPlanFingerprint( plan );
    scenario["expected_plan"] = scientificPlanToJson( plan );
    std::ofstream file( path, std::ios::trunc );
    REQUIRE( file.is_open() );
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    builder["commentStyle"] = "None";
    std::string text = Json::writeString( builder, scenario );
    // jsoncpp pretty-printing leaves a trailing space after "key" : when the
    // value moves to the next line; strip it so git diff --check stays clean
    // and regenerated files are byte-stable against the committed form.
    for ( auto &line : TextLines( text ) )
        while ( !line.empty() && ( line.back() == ' ' || line.back() == '\t' ) )
            line.pop_back();
    file << text;
}
} // namespace

TEST_CASE( "golden planning scenarios replay byte-stable offline", "[scientific_planner][golden]" )
{
    const char *scenarios[] = {
        "01_ndvi_measurement.json",
        "02_water_change_two_date.json",
        "03_temporal_monitoring_insufficient.json",
        "04_classification_teaching_guided.json",
    };
    const bool updateMode = std::getenv( "SICNU_PLANNER_UPDATE_GOLDENS" ) != nullptr;

    for ( const char *name : scenarios )
    {
        const std::string path = scenarioPath( name );
        INFO( "scenario: " << name );
        Json::Value scenario = loadScenario( path );

        // scenario file self-validation (fail-closed)
        REQUIRE( scenario["kind"].asString() == std::string( "planner_golden_scenario" ) );
        REQUIRE( scenario["schema_version"].asString() == std::string( "1.0" ) );

        ScientificGoal goal;
        PlanningContext context;
        std::string error;
        REQUIRE( scientificGoalFromJson( scenario["goal"], goal, error ) );
        REQUIRE( planningContextFromJson( scenario["context"], context, error ) );
        FixtureProvider provider( scenario["capabilities"] );

        const PlanningResult result =
            planScientificWork( goal, context, PlannerProviders{ &provider } );
        const Json::Value &expected = scenario["expected"];
        REQUIRE( static_cast<int>( result.candidates.size() )
                 == expected["candidate_count"].asInt() );
        const ScientificPlan &primary = *result.primary();
        CHECK( primary.verdict == expected["verdict"].asString() );
        CHECK( scientificPlanSequenceSummary( primary ) == expected["sequence"].asString() );

        const int blocking = static_cast<int>( std::count_if(
            primary.openQuestions.begin(), primary.openQuestions.end(),
            []( const PlanOpenQuestion &q ) { return q.blocking; } ) );
        for ( const auto &question : primary.openQuestions )
            INFO( "question " << question.questionId << " [" << question.kind << "] blocking="
                              << question.blocking << ": " << question.detail );
        CHECK( blocking == expected["blocking_questions"].asInt() );

        // teaching projection expectations
        if ( expected.isMember( "teaching" ) )
        {
            const Json::Value &teaching = expected["teaching"];
            ModePolicy mode;
            mode.kind = teaching["mode_kind"].asString();
            mode.autonomy = teaching["autonomy"].asString();
            const TeachingViews views = teachingViews( primary, mode );
            CHECK( views.hiddenAnswer["masking_applied"].asBool()
                   == teaching["masking_applied"].asBool() );
        }

        if ( updateMode )
        {
            updateGolden( path, scenario, primary );
            continue;
        }

        // verify mode: fingerprint + canonical byte comparison are mandatory
        REQUIRE( scenario.isMember( "expected_fingerprint" ) );
        REQUIRE_FALSE( scenario["expected_fingerprint"].asString().empty() );
        CHECK( scientificPlanFingerprint( primary )
               == scenario["expected_fingerprint"].asString() );
        REQUIRE( scenario.isMember( "expected_plan" ) );
        ScientificPlan expectedPlan;
        REQUIRE( scientificPlanFromJson( scenario["expected_plan"], expectedPlan, error ) );
        // the regenerated plan must round-trip the pinned document exactly
        CHECK( json_util::canonicalCompact( scientificPlanToJson( primary ) )
               == json_util::canonicalCompact( scientificPlanToJson( expectedPlan ) ) );
        // same input replays identically
        const PlanningResult replay =
            planScientificWork( goal, context, PlannerProviders{ &provider } );
        CHECK( json_util::canonicalCompact( scientificPlanToJson( *replay.primary() ) )
               == json_util::canonicalCompact( scientificPlanToJson( primary ) ) );
    }
}
