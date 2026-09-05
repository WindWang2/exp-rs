// tests/test_exprs_external_process.cpp — safe external process contract
#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"

#include <fstream>
#include <unistd.h>

using namespace exprs;

namespace {
bool writeFile( const std::string &path, const std::string &content )
{
    std::ofstream output( path, std::ios::trunc | std::ios::binary );
    output << content;
    return static_cast<bool>( output );
}
} // namespace

TEST_CASE( "external process runs argv tools and captures output", "[sdk][external]" )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/echo", "hello", "world" };
    request.timeoutSeconds = 30;
    const auto result = ExternalProcess::run( request );
    REQUIRE( result.started );
    REQUIRE( result.exitedCleanly() );
    REQUIRE( result.exitCode == 0 );
    REQUIRE( result.stdOut.find( "hello world" ) != std::string::npos );
    REQUIRE_FALSE( result.truncatedStdout );
}

TEST_CASE( "external process reports failing exit codes", "[sdk][external]" )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "exit 3" }; // argv-only: sh itself is fine
    const auto result = ExternalProcess::run( request );
    REQUIRE( result.started );
    REQUIRE_FALSE( result.exitedCleanly() );
    REQUIRE( result.exitCode == 3 );
}

TEST_CASE( "external process enforces the timeout kill ladder", "[sdk][external]" )
{
    ExternalProcessRequest request;
    // sleep in a subshell child so the process-group kill is observable.
    request.argv = { "/bin/sh", "-c", "sleep 30 & wait" };
    request.timeoutSeconds = 1;
    const auto result = ExternalProcess::run( request );
    REQUIRE( result.timedOut );
    REQUIRE_FALSE( result.exitedCleanly() );
    REQUIRE( result.durationMs < 30000 );
}

TEST_CASE( "external process honours cooperative cancellation", "[sdk][external]" )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/sleep", "30" };
    request.timeoutSeconds = 60;
    request.isCancelled = []() { return true; };
    const auto result = ExternalProcess::run( request );
    REQUIRE( result.cancelled );
    REQUIRE_FALSE( result.exitedCleanly() );
}

TEST_CASE( "external process bounds stdout capture", "[sdk][external]" )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "yes abcd | head -c 100000" };
    request.stdoutLimitBytes = 1024;
    const auto result = ExternalProcess::run( request );
    REQUIRE( result.exitedCleanly() );
    REQUIRE( result.truncatedStdout );
    REQUIRE( result.stdOut.size() <= 1024 );
}

TEST_CASE( "external process uses a minimal environment baseline", "[sdk][external]" )
{
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "printenv EXPRS_SECRET; printenv PATH" };
    const auto result = ExternalProcess::run( request );
    REQUIRE( result.exitedCleanly() );
    // PATH present (baseline), secret NOT inherited.
    REQUIRE( result.stdOut.find( "EXPRS_SECRET" ) == std::string::npos );
}

TEST_CASE( "external process validates argv", "[sdk][external]" )
{
    std::string error;
    REQUIRE_FALSE( ExternalProcess::validateArgv( {}, error ) );
    REQUIRE_FALSE( ExternalProcess::validateArgv( { "/nonexistent/binary-xyz" }, error ) );
    REQUIRE( ExternalProcess::validateArgv( { "/bin/echo" }, error ) );
}

TEST_CASE( "external process reports exec failures", "[sdk][external]" )
{
    ExternalProcessRequest request;
    request.argv = { "/nonexistent/binary-xyz" };
    const auto result = ExternalProcess::run( request );
    REQUIRE_FALSE( result.exitedCleanly() );
    REQUIRE_FALSE( result.error.empty() );
}
