// test_r33_t04_workflow_typing.cpp — R33-T04 / WP06 (#2110):
//
//   "workflow run" died in an uncaught Json::LogicError (exit 3) for
//   schema-passing documents with a non-string id/title or a non-string
//   inputs[].port/source, while "workflow validate" exited 0 on the same
//   documents. Both layers now reject present-but-mistyped members:
//
//   * the public schema refuses non-string root id/title/name and mistyped
//     input wiring in BOTH dialects (UI {port, source} and engine
//     {fromStepId, fromPort, toPort}) — validate exits ValidationFailure;
//   * the engine parser refuses a mistyped root identity instead of
//     silently defaulting it;
//   * legal legacy documents (missing members, both input dialects, params)
//     keep validating and parsing unchanged.
#include <catch2/catch_test_macros.hpp>

#include "exprs/workflow_schema.h"
#include "workflow/workflow_definition.h"

#include <json/json.h>

#include <string>
#include <vector>

using namespace exprs;

namespace
{
Json::Value parse( const std::string &json )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( json, root, false );
    return root;
}

const char *kMinimalDoc = R"({
    "schema_version": 1,
    "id": "wf_typed",
    "title": "Typed",
    "steps": [ { "id": "s1", "operator": "rs:ndvi", "params": {} } ]
})";
} // namespace

TEST_CASE( "workflow schema rejects mistyped identity and input wiring (#2110)",
           "[workflow][schema][r33t04]" )
{
    PluginDiagnosticLog log;

    SECTION( "non-string root id/title/name are refused" )
    {
        auto document = parse( kMinimalDoc );
        document["id"] = Json::Value( Json::objectValue );
        document["id"]["bad"] = 1;
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
        REQUIRE( log.hasErrors() );

        document = parse( kMinimalDoc );
        document["title"] = 42;
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );

        document = parse( kMinimalDoc );
        document["name"] = true;
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    }

    SECTION( "non-string inputs[].port/source are refused (the run-crash shape)" )
    {
        auto document = parse( kMinimalDoc );
        Json::Value input( Json::objectValue );
        input["port"] = 3;
        input["source"] = "s0.output";
        Json::Value inputs( Json::arrayValue );
        inputs.append( input );
        document["steps"][0]["inputs"] = inputs;
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
        REQUIRE( log.hasErrors() );

        document = parse( kMinimalDoc );
        Json::Value badSource( Json::objectValue );
        badSource["port"] = "input";
        badSource["source"] = Json::Value( Json::arrayValue );
        document["steps"][0]["inputs"] = Json::Value( Json::arrayValue );
        document["steps"][0]["inputs"][0] = badSource;
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    }

    SECTION( "non-string engine-shape wiring is refused" )
    {
        auto document = parse( kMinimalDoc );
        document["steps"][0]["inputs"] = parse( R"([{ "fromStepId": 7, "fromPort": "o", "toPort": "i" }])" );
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    }

    SECTION( "non-array inputs / non-object entries are refused" )
    {
        auto document = parse( kMinimalDoc );
        document["steps"][0]["inputs"] = "wiring";
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );

        document = parse( kMinimalDoc );
        document["steps"][0]["inputs"] = parse( R"([ 5 ])" );
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    }

    SECTION( "legal documents of both dialects still validate" )
    {
        auto document = parse( kMinimalDoc );
        document["steps"][0]["inputs"] = parse(
            R"([{ "port": "input", "source": "s0.output" }])" );
        PluginDiagnosticLog okLog;
        REQUIRE( validateWorkflowDocument( document, okLog ) );

        document["steps"][0]["inputs"] = parse(
            R"([{ "fromStepId": "s0", "fromPort": "output", "toPort": "input" }])" );
        okLog = PluginDiagnosticLog();
        REQUIRE( validateWorkflowDocument( document, okLog ) );
    }
}

TEST_CASE( "workflow engine parser refuses a mistyped root identity instead of defaulting",
           "[workflow][engine][r33t04]" )
{
    std::string error;

    SECTION( "non-string id is rejected, not defaulted to agent_plan" )
    {
        auto document = parse( kMinimalDoc );
        document["id"] = 9;
        sicnu::workflow::WorkflowDefinition definition;
        REQUIRE_FALSE( sicnu::workflow::workflowDefinitionFromJson( document, definition, error ) );
        REQUIRE( error.find( "id" ) != std::string::npos );
    }

    SECTION( "non-string title/name are rejected" )
    {
        auto document = parse( kMinimalDoc );
        document["title"] = Json::Value( Json::objectValue );
        sicnu::workflow::WorkflowDefinition definition;
        REQUIRE_FALSE( sicnu::workflow::workflowDefinitionFromJson( document, definition, error ) );

        document = parse( kMinimalDoc );
        document["name"] = 1.5;
        REQUIRE_FALSE( sicnu::workflow::workflowDefinitionFromJson( document, definition, error ) );
    }

    SECTION( "missing members keep their documented defaults" )
    {
        auto document = parse( kMinimalDoc );
        document.removeMember( "id" );
        document.removeMember( "title" );
        sicnu::workflow::WorkflowDefinition definition;
        REQUIRE( sicnu::workflow::workflowDefinitionFromJson( document, definition, error ) );
        REQUIRE( definition.id == "agent_plan" );
        REQUIRE( definition.title == "Agent Plan" );
    }
}
