// tests/test_r27_t03_external_env_guard.cpp — #1761 (AS-009)
//
// The ExternalProcess env deny-list covered enumerated LD_*/DYLD_* exact
// names only; interpreter-adjacent injectors (BASH_ENV, ENV, NODE_OPTIONS,
// PYTHONSTARTUP, PERL5OPT, RUBYOPT, GLIBC_TUNABLES, …) flowed into every
// child — code execution for common interpreter tools without touching
// LD_PRELOAD. The guard now refuses the LD_/DYLD_ FAMILIES by prefix plus
// the interpreter injector names, through the same E5007 policy channel.
#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"

#include <json/json.h>
#include <string>

using namespace exprs;

namespace
{
bool keyIsRefused( const std::string &key )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/echo", "hello" };
    request.timeoutSeconds = 30;
    request.environment = Json::Value( Json::objectValue );
    request.environment[key] = "payload";
    const auto result = ExternalProcess::run( request );
    INFO( "key: " << key );
    return result.refusedByPolicy && !result.started
           && result.error.find( "env_injection" ) != std::string::npos;
}
} // namespace

TEST_CASE( "interpreter injector environment keys are refused (#1761)",
           "[sdk][external][security][r27t03]" )
{
    const std::string injectors[] = {
        "BASH_ENV", "bash_env", "ENV", "NODE_OPTIONS", "node_options",
        "PYTHONSTARTUP", "PYTHONINSPECT", "PYTHONHOME", "PYTHONPATH",
        "PERL5OPT", "PERL5LIB", "RUBYOPT", "RUBYLIB", "GLIBC_TUNABLES",
    };
    for ( const std::string &key : injectors )
    {
        REQUIRE( keyIsRefused( key ) );
    }
}

TEST_CASE( "the LD_/DYLD_ families are refused by prefix, not enumeration (#1761)",
           "[sdk][external][security][r27t03]" )
{
    // Exact names keep working (regression), and FUTURE family members the
    // old exact-match list never knew are covered by the prefix rule.
    const std::string familyKeys[] = {
        "LD_PRELOAD", "LD_AUDIT", "LD_LIBRARY_PATH", "LD_VERIFY_ADDR_SZ",
        "LD_LOCAL_TREE", "DYLD_NEW_UNKNOWN", "DYLD_VERSIONED_FRAMEWORK_PATH",
    };
    for ( const std::string &key : familyKeys )
    {
        REQUIRE( keyIsRefused( key ) );
    }
}

TEST_CASE( "benign tool configuration keys still reach the child (#1761 control)",
           "[sdk][external][r27t03]" )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "printf '%s' \"$R27_PROBE\"" };
    request.timeoutSeconds = 30;
    request.environment = Json::Value( Json::objectValue );
    request.environment["R27_PROBE"] = "benign";
    request.environment["GDAL_HTTP_TIMEOUT"] = "30";
    request.environment["OMP_NUM_THREADS"] = "2";
    const auto result = ExternalProcess::run( request );
    REQUIRE( result.started );
    REQUIRE( result.exitedCleanly() );
    REQUIRE( result.stdOut.find( "benign" ) != std::string::npos );
}
