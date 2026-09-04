// tests/test_exprs_workflow_schema.cpp — public workflow schema v1 + builder
#include <catch2/catch_test_macros.hpp>

#include "exprs/workflow_builder.h"
#include "exprs/workflow_schema.h"

using namespace exprs;

namespace {
Json::Value parse( const std::string &json )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( json, root, false );
    return root;
}
} // namespace

TEST_CASE( "legacy documents without schema_version are accepted as v1", "[workflow][schema]" )
{
    const auto document = parse( R"({
        "id": "legacy", "title": "Legacy",
        "steps": [ { "id": "s1", "operator": "rs:ndvi", "params": {} } ]
    })" );
    PluginDiagnosticLog log;
    REQUIRE( workflowSchemaVersion( document, log ) == 1 );
    REQUIRE( validateWorkflowDocument( document, log ) );
}

TEST_CASE( "future schema versions are rejected with a diagnostic", "[workflow][schema]" )
{
    const auto document = parse( R"({
        "schema_version": 99, "id": "future", "title": "F",
        "steps": [ { "id": "s1", "operator": "rs:ndvi" } ]
    })" );
    PluginDiagnosticLog log;
    REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    REQUIRE( log.hasErrors() );
}

TEST_CASE( "structural problems are diagnosed", "[workflow][schema]" )
{
    PluginDiagnosticLog log;

    SECTION( "empty steps" )
    {
        const auto document = parse( R"({"schema_version":1,"id":"x","steps":[]})" );
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    }
    SECTION( "duplicate step ids" )
    {
        const auto document = parse( R"({
            "schema_version":1,"id":"x",
            "steps":[{"id":"s","operator":"rs:ndvi"},{"id":"s","operator":"rs:ndvi"}]
        })" );
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    }
    SECTION( "operator step without operator id" )
    {
        const auto document = parse( R"({
            "schema_version":1,"id":"x","steps":[{"id":"s"}]
        })" );
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    }
    SECTION( "params must be an object" )
    {
        const auto document = parse( R"({
            "schema_version":1,"id":"x","steps":[{"id":"s","operator":"rs:ndvi","params":[1]}]
        })" );
        REQUIRE_FALSE( validateWorkflowDocument( document, log ) );
    }
}

TEST_CASE( "builder produces valid public documents", "[workflow][builder]" )
{
    WorkflowBuilder builder( "lab.demo", "Demo workflow" );
    builder.addStep( "cal", "rs:radiometric_calibration" )
        .withParam( "input", "in.tif" )
        .withParam( "output", "cal.tif" )
        .withResourceEstimateMb( 1024 );
    builder.addStep( "ndvi", "rs:ndvi" )
        .withInput( "input", "$cal.output" )
        .withParam( "output", "ndvi.tif" );

    PluginDiagnosticLog log;
    const Json::Value document = builder.toValidatedJson( log );
    REQUIRE_FALSE( document.isNull() );
    REQUIRE( document["schema_version"].asInt() == kWorkflowSchemaVersion );
    REQUIRE( document["steps"].size() == 2 );
    REQUIRE( document["steps"][1]["inputs"][0]["source"].asString() == "$cal.output" );
}

TEST_CASE( "migration stamps the target version", "[workflow][schema]" )
{
    const auto document = parse( R"({
        "id": "legacy", "steps": [ { "id": "s1", "operator": "rs:ndvi" } ]
    })" );
    PluginDiagnosticLog log;
    const auto migrated = migrateWorkflowDocument( document, kWorkflowSchemaVersion, log );
    REQUIRE( migrated["schema_version"].asInt() == kWorkflowSchemaVersion );

    PluginDiagnosticLog downLog;
    REQUIRE( migrateWorkflowDocument( document, -1, downLog ).isNull() );
    REQUIRE( downLog.hasErrors() );
}
