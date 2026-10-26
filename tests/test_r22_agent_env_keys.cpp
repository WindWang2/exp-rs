// tests/test_r22_agent_env_keys.cpp — R22-04 (#1690) regression.
//
// RED first: ExternalProcess::run()'s loader-injection deny-list compared the
// WHOLE env key case-insensitively, but the child envp entry is assembled as
// key + "=" + value and the dynamic linker splits at the FIRST '='. A
// manifest env key "LD_PRELOAD=/path/evil.so " (value arbitrary) therefore
// passed the exact-match deny-list and spliced into the child as
// "LD_PRELOAD=/path/evil.so =v" — ld.so preloaded the attacker object (live
// reproduction in the issue: constructor wrote PWNED). The same splice
// applies to empty keys and embedded NULs. Fail-closed key validation now
// refuses them through the existing E5007 channel, and the manifest parser
// rejects malformed names at ingestion time.
#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"
#include "exprs/plugin_manifest.h"

#include <json/json.h>

using namespace exprs;

TEST_CASE( "environment keys carrying '=' splice-refuse before spawn (#1690)",
           "[sdk][external][security][r22]" )
{
    const std::string evil = "/tmp/exp_rs_r22_nonexistent_evil.so";
    const std::string spliceKeys[] = {
        "LD_PRELOAD=" + evil + " ",
        "LD_PRELOAD=" + evil,
        "ld_preload=" + evil + " ",
        "LD_AUDIT=" + evil,
        "DYLD_INSERT_LIBRARIES=" + evil,
        "A=B",
    };
    for ( const std::string &key : spliceKeys )
    {
        ExternalProcessRequest request;
        request.argv = { "/bin/echo", "hello" };
        request.timeoutSeconds = 30;
        request.environment = Json::Value( Json::objectValue );
        request.environment[key] = "v";

        const auto result = ExternalProcess::run( request );
        INFO( "key: " << key );
        REQUIRE_FALSE( result.started );
        REQUIRE( result.refusedByPolicy );
        REQUIRE( result.error.find( "env_injection" ) != std::string::npos );
    }
}

TEST_CASE( "empty and NUL-bearing environment keys are refused (#1690)",
           "[sdk][external][security][r22]" )
{
    const std::string nulKey = std::string( "LD_PRELOAD\0junk", 15 );
    const std::string badKeys[] = { std::string(), nulKey, std::string( "X\0Y", 3 ) };
    for ( const std::string &key : badKeys )
    {
        ExternalProcessRequest request;
        request.argv = { "/bin/echo", "hello" };
        request.timeoutSeconds = 30;
        request.environment = Json::Value( Json::objectValue );
        request.environment[key] = "v";

        const auto result = ExternalProcess::run( request );
        INFO( "key bytes: " << key.size() );
        REQUIRE_FALSE( result.started );
        REQUIRE( result.refusedByPolicy );
        REQUIRE( result.error.find( "env_injection" ) != std::string::npos );
    }
}

TEST_CASE( "well-formed environment keys still reach the child (#1690 control)",
           "[sdk][external][r22]" )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "printf '%s' \"$R22_PROBE\"" };
    request.timeoutSeconds = 30;
    request.environment = Json::Value( Json::objectValue );
    request.environment["R22_PROBE"] = "benign";
    request.environment["GDAL_HTTP_TIMEOUT"] = "30";

    const auto result = ExternalProcess::run( request );
    REQUIRE( result.started );
    REQUIRE( result.exitedCleanly() );
    REQUIRE( result.stdOut == "benign" );
}

TEST_CASE( "manifest env keys are validated at parse time (#1690)",
           "[sdk][manifest][r22]" )
{
    auto parseEnv = []( const Json::Value &environment, std::string &error ) {
        Json::Value external( Json::objectValue );
        external["argv"] = Json::Value( Json::arrayValue );
        external["argv"].append( "/bin/echo" );
        external["environment"] = environment;
        ManifestExternalTool tool;
        return ManifestExternalTool::fromJson( external, tool, error );
    };

    SECTION( "malformed keys are a typed manifest error" )
    {
        for ( const std::string &key :
              { std::string( "LD_PRELOAD=/tmp/x.so " ), std::string( "A=B" ), std::string( "" ),
                std::string( "1BAD" ), std::string( "BAD-NAME" ), std::string( "BAD.NAME" ) } )
        {
            Json::Value env( Json::objectValue );
            env[key] = "v";
            std::string error;
            REQUIRE_FALSE( parseEnv( env, error ) );
            INFO( "key: " << key );
            REQUIRE( error.find( "'external.environment' key" ) != std::string::npos );
        }
    }
    SECTION( "well-formed manifest keys parse" )
    {
        Json::Value env( Json::objectValue );
        env["PATH"] = "/usr/bin";
        env["_UNDER_score2"] = "ok";
        std::string error;
        REQUIRE( parseEnv( env, error ) );
        REQUIRE( error.empty() );
    }
    SECTION( "non-object environment is a typed error" )
    {
        Json::Value env( Json::arrayValue );
        env.append( "PATH=/usr/bin" );
        std::string error;
        REQUIRE_FALSE( parseEnv( env, error ) );
        REQUIRE( error.find( "'external.environment' must be an object" ) != std::string::npos );
    }
}
