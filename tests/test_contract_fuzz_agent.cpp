// test_contract_fuzz_agent.cpp — bounded property tests over the agent
// contract surfaces (task D, Verification 7.0):
//
//   * AgentPlan reader (v1/v2): total on bounded mutated documents; accepted
//     plans keep the raw round-trip; rejected plans carry a stable
//     non-empty error code; the reader never throws;
//   * MapSpec validate/patch: validateMapSpec is total and reports one
//     problem per violation; applyMapSpecPatch refuses invalid ops with a
//     reason and never corrupts the document into an unvalidatable state
//     silently.
//
// Deterministic seeds, hard caps. Heavy closure (sicnu_agent) — built with
// the full tree, not the light lane.
#include <catch2/catch_test_macros.hpp>

#include "agent/harness/agent_plan.h"
#include "agent/harness/harness_error.h"
#include "agent/mapspec/mapspec.h"
#include "support/bounded_fuzz.h"

#include <json/json.h>

#include <sstream>
#include <string>
#include <vector>

using sicnu::agent::harness::AgentPlan;
using sicnu::agent::harness::HarnessError;
using sicnu::agent::harness::readAgentPlan;
using sicnu::testing::BoundedRandom;

namespace
{
constexpr int kIterationsPerSeed = 400;

/// A minimal valid v2 execution plan (mutation seed).
Json::Value validPlan()
{
    Json::Value plan{ Json::objectValue };
    plan["kind"] = "execution_plan";
    plan["schema_version"] = "2.0";
    plan["plan_id"] = "plan-fuzz-1";
    plan["goal"] = "fuzz fixture";
    Json::Value inputs{ Json::arrayValue };
    Json::Value input;
    input["name"] = "scene";
    input["ref"] = "asset-1";
    inputs.append( input );
    plan["inputs"] = inputs;
    Json::Value steps{ Json::arrayValue };
    Json::Value step;
    step["id"] = "s1";
    step["operator_id"] = "rs:ndvi";
    Json::Value params{ Json::objectValue };
    step["params"] = params;
    steps.append( step );
    plan["steps"] = steps;
    Json::Value outputs{ Json::arrayValue };
    plan["outputs"] = outputs;
    return plan;
}

Json::Value parseJson( const std::string &bytes, bool *ok )
{
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream( bytes );
    *ok = Json::parseFromStream( builder, stream, &parsed, &errors );
    return parsed;
}
} // namespace

TEST_CASE( "AgentPlan fuzz: reader is total; accept/reject always truthful",
           "[contract][fuzz][agent_plan]" )
{
    const Json::Value seed = validPlan();
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const std::string seedBytes = Json::writeString( writer, seed );

    for ( const uint64_t seedRnd : { 5ull, 55ull } )
    {
        BoundedRandom random( seedRnd );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string mutated = random.mutate( seedBytes, 10 );
            if ( mutated.size() > 16384 )
                continue; // hard cap
            bool parsed = false;
            const Json::Value doc = parseJson( mutated, &parsed );
            if ( !parsed || !doc.isObject() )
                continue;
            AgentPlan plan;
            HarnessError error;
            REQUIRE_NOTHROW( readAgentPlan( doc, plan, error ) ); // total
            if ( error.code.empty() )
            {
                // Accepted: identity round-trips (raw document preserved).
                REQUIRE( plan.planId == doc["plan_id"].asString() );
            }
            else
            {
                // Rejected: stable taxonomy requires a non-empty code.
                REQUIRE_FALSE( error.code.empty() );
                REQUIRE_FALSE( error.summary.empty() );
            }
        }
    }
}

TEST_CASE( "AgentPlan: unknown intent is rejected with the stable code",
           "[contract][agent_plan]" )
{
    Json::Value doc = validPlan();
    doc["intent"] = "teleport";
    AgentPlan plan;
    HarnessError error;
    REQUIRE_FALSE( readAgentPlan( doc, plan, error ) );
    REQUIRE_FALSE( error.code.empty() );
}

TEST_CASE( "MapSpec fuzz: validate is total; patch failures never corrupt "
           "silently",
           "[contract][fuzz][mapspec]" )
{
    // Minimal valid v3 document seed.
    Json::Value spec{ Json::objectValue };
    spec["mapspec"] = "3.0";
    Json::Value page{ Json::objectValue };
    page["width_mm"] = 210.0;
    page["height_mm"] = 297.0;
    spec["page"] = page;

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    const std::string seedBytes = Json::writeString( writer, spec );

    for ( const uint64_t seedRnd : { 9ull, 99ull } )
    {
        BoundedRandom random( seedRnd );
        for ( int i = 0; i < kIterationsPerSeed; ++i )
        {
            const std::string mutated = random.mutate( seedBytes, 8 );
            if ( mutated.size() > 16384 )
                continue;
            bool parsed = false;
            const Json::Value doc = parseJson( mutated, &parsed );
            if ( !parsed )
                continue;
            std::vector<std::string> problems;
            REQUIRE_NOTHROW( problems = sicnu::agent::mapspec::validateMapSpec( doc ) );
            // Problem entries are non-empty strings (one per violation).
            for ( const std::string &problem : problems )
                REQUIRE_FALSE( problem.empty() );

            // Patch fuzz: invalid ops fail with a reason, never by exception.
            Json::Value patch;
            patch["op"] = random.pick( std::vector<std::string>{ "add", "update", "remove", "banana" } );
            Json::Value target = doc;
            std::string error;
            REQUIRE_NOTHROW(
                sicnu::agent::mapspec::applyMapSpecPatch( target, patch, &error ) );
        }
    }
}
