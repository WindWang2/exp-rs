// tests/test_cli_commands_json.cpp — CLI machine-readable contract smoke:
// envelope shape, api_version stamp, and stable exit codes (Phase L).
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "support/http_range_server.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef SICNU_TEST_CLI
#error "SICNU_TEST_CLI must point at sicnu_geo_rs_cli"
#endif

namespace {

struct RunResult
{
    int exitCode = -1;
    std::string output;
};

RunResult runCli( const std::string &args )
{
    std::string command = std::string( SICNU_TEST_CLI ) + " " + args + " 2>/dev/null";
    FILE *pipe = ::popen( command.c_str(), "r" );
    REQUIRE( pipe != nullptr );
    char buffer[4096];
    size_t read = 0;
    RunResult result;
    while ( ( read = fread( buffer, 1, sizeof( buffer ), pipe ) ) > 0 )
        result.output.append( buffer, read );
    const int status = ::pclose( pipe );
    result.exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
    return result;
}

Json::Value parseEnvelope( const std::string &text )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( text, root, false );
    return root;
}
} // namespace

TEST_CASE( "algorithms list produces the documented envelope", "[cli][json]" )
{
    const auto result = runCli( "algorithms list --json" );
    REQUIRE( result.exitCode == 0 );
    const Json::Value envelope = parseEnvelope( result.output );
    REQUIRE( envelope.isObject() );
    REQUIRE( envelope.get( "ok", false ).asBool() );
    REQUIRE( envelope.get( "command", "" ).asString() == "algorithms" );
    REQUIRE( envelope["data"].isArray() );
    REQUIRE( envelope.get( "api_version", "" ).asString() == "3.0" );
}

TEST_CASE( "unknown algorithms map to the missing-dependency exit code", "[cli][json]" )
{
    const auto result = runCli( "algorithms schema no:such_operator --json" );
    REQUIRE( result.exitCode == 5 ); // exprs::ExitCode::MissingDependency
    const Json::Value envelope = parseEnvelope( result.output );
    REQUIRE_FALSE( envelope.get( "ok", true ).asBool() );
}

TEST_CASE( "workflow validate enforces the public schema", "[cli][json]" )
{
    // Write an invalid workflow and validate it through the CLI.
    const char *path = "/tmp/exprs_cli_test_workflow.json";
    {
        std::ofstream output( path, std::ios::trunc );
        output << R"({"schema_version": 1, "id": "t", "steps": []})";
    }
    const auto result = runCli( std::string( "workflow validate " ) + path );
    REQUIRE( result.exitCode == 2 ); // exprs::ExitCode::ValidationFailure
}

// ---------------------------------------------------------------------------
// 8.0 — data identity / data cache check (Data Fabric track, pkg I): the CLI
// projection of the remote-identity contract over a live loopback origin.
// ---------------------------------------------------------------------------

TEST_CASE( "data identity reports a redacted, provable remote identity",
           "[cli][json][identity][utc8]" )
{
    sicnu::geo::testsupport::HttpRangeServer server( std::vector<unsigned char>( 4096, 0x11 ) );
    server.setEtag( "\"cli-identity-1\"" );

    const auto result = runCli( "data identity " + server.url() );
    REQUIRE( result.exitCode == 0 );
    const Json::Value envelope = parseEnvelope( result.output );
    REQUIRE( envelope.get( "ok", false ).asBool() );
    REQUIRE( envelope.get( "command", "" ).asString() == "data" );
    const Json::Value &identity = envelope["data"]["identity"];
    REQUIRE( identity.isObject() );
    CHECK( identity.get( "state", "" ).asString() == "fresh" );
    // Redaction contract: the display URL carries no credentials and the
    // payload never echoes raw query secrets.
    CHECK( envelope["data"]["token_provable"].asBool() );
    CHECK( result.output.find( "secret" ) == std::string::npos );
}

TEST_CASE( "data identity folds offline origins into an honest state",
           "[cli][json][identity][utc8]" )
{
    // Port 1 on loopback: nothing listens there; the probe must answer with
    // an offline identity, not a crash or a fabricated fresh state.
    const auto result = runCli( "data identity http://127.0.0.1:1/offline.tif" );
    REQUIRE( result.exitCode == 0 );
    const Json::Value envelope = parseEnvelope( result.output );
    REQUIRE( envelope.get( "ok", false ).asBool() );
    CHECK( envelope["data"]["identity"].get( "state", "" ).asString() == "offline" );
    CHECK_FALSE( envelope["data"]["token_provable"].asBool() );
}

TEST_CASE( "data cache check reports byte accounting through the range cache",
           "[cli][json][cache][utc8]" )
{
    sicnu::geo::testsupport::HttpRangeServer server( std::vector<unsigned char>( 8192, 0x22 ) );
    server.setEtag( "\"cli-cache-1\"" );

    const auto result = runCli( "data cache check " + server.url() + " --bytes 2048" );
    REQUIRE( result.exitCode == 0 );
    const Json::Value envelope = parseEnvelope( result.output );
    REQUIRE( envelope.get( "ok", false ).asBool() );
    const Json::Value &data = envelope["data"];
    REQUIRE( data.isObject() );
    CHECK( data.get( "bytes_read", Json::Value( 0 ) ).asUInt64() == 2048 );
    // The bytes served came from the origin once (bounded ranged GETs).
    CHECK( data["telemetry_delta"].get( "bytes_served", Json::Value( 0 ) ).asUInt64() == 2048 );
    CHECK( data["telemetry_delta"].get( "bytes_fetched", Json::Value( 0 ) ).asUInt64() >= 2048 );
    CHECK( data["config"].get( "stale_policy", "" ).asString() == "revalidate_on_open" );
}
