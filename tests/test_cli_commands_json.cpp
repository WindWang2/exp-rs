// tests/test_cli_commands_json.cpp — CLI machine-readable contract smoke:
// envelope shape, api_version stamp, and stable exit codes (Phase L).
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

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
