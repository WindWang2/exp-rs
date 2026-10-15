// tests/test_r14_C_external_process_env.cpp — R14-C FIX-7 regression.
//
// RED first: explicit request.environment entries were merged into the child
// environment with NO deny-list — a manifest-controlled plugin could set
// LD_PRELOAD / DYLD_INSERT_LIBRARIES for the spawned tool, turning the
// external-process permission into arbitrary code injection into every child
// (the header advertises an env allow-list; only the workspace policy's
// absolute-path check ever looked at env values, and only when
// SICNU_MCP_WORKSPACE was active — off by default).
#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"

#include <json/json.h>

using namespace exprs;

TEST_CASE( "loader-injection environment keys are a typed refusal (R14-C FIX-7)",
           "[sdk][external][r14_C]" )
{
    const char *injectors[] = { "LD_PRELOAD", "LD_AUDIT", "LD_LIBRARY_PATH",
                                "DYLD_INSERT_LIBRARIES", "ld_preload" };
    for ( const char *key : injectors )
    {
        ExternalProcessRequest request;
        request.argv = { "/bin/echo", "hello" };
        request.timeoutSeconds = 30;
        request.environment = Json::Value( Json::objectValue );
        request.environment[key] = "/tmp/attacker.so";

        const auto result = ExternalProcess::run( request );
        INFO( "key: " << key );
        REQUIRE_FALSE( result.started );
        REQUIRE( result.refusedByPolicy );
        REQUIRE( result.error.find( "env_injection" ) != std::string::npos );
        REQUIRE( result.error.find( key ) != std::string::npos );
    }
}

TEST_CASE( "benign explicit environment entries still reach the child (R14-C FIX-7 control)",
           "[sdk][external][r14_C]" )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "printf '%s' \"$R14C_PROBE\"" };
    request.timeoutSeconds = 30;
    request.environment = Json::Value( Json::objectValue );
    request.environment["R14C_PROBE"] = "benign";

    const auto result = ExternalProcess::run( request );
    REQUIRE( result.started );
    REQUIRE( result.exitedCleanly() );
    REQUIRE( result.stdOut == "benign" );
}
