// tests/test_exprs_external_process.cpp — safe external process contract
#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"

#include <filesystem>
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

TEST_CASE( "workspace effect policy contains resolved execution effects (issue #757)",
           "[sdk][external][policy]" )
{
    // Env var is process-global: save/restore around the whole test.
    const char *saved = ::getenv( "SICNU_MCP_WORKSPACE" );
    const std::string savedValue = saved ? saved : "";
    auto setWorkspace = []( const char *value ) {
        if ( value )
            ::setenv( "SICNU_MCP_WORKSPACE", value, 1 );
        else
            ::unsetenv( "SICNU_MCP_WORKSPACE" );
    };
    struct Restore
    {
        ~Restore()
        {
            if ( !savedValue.empty() )
                ::setenv( "SICNU_MCP_WORKSPACE", savedValue.c_str(), 1 );
            else
                ::unsetenv( "SICNU_MCP_WORKSPACE" );
        }
        const char *saved;
        const std::string &savedValue;
    } restore{ saved, savedValue };

    namespace fs = std::filesystem;
    const std::string root = "/tmp/exprs_test_ws_root";
    const std::string outside = "/tmp/exprs_test_ws_outside";
    fs::remove_all( root );
    fs::remove_all( outside );
    fs::create_directories( root + "/inside" );
    fs::create_directories( outside );
    setWorkspace( root.c_str() );

    auto runEcho = []( const std::string &cwd, std::vector<std::string> extraArgs = {},
                       std::vector<std::string> allowedRoots = {} ) {
        ExternalProcessRequest request;
        request.argv = { "/bin/echo", "-n", "ok" };
        for ( const std::string &argument : extraArgs )
            request.argv.push_back( argument );
        request.workingDirectory = cwd;
        request.additionalAllowedRoots = allowedRoots;
        return ExternalProcess::run( request );
    };

    SECTION( "no workspace policy: escapes are none of the policy's business" )
    {
        setWorkspace( nullptr );
        const auto result = runEcho( outside );
        REQUIRE( result.exitedCleanly() );
        REQUIRE_FALSE( result.refusedByPolicy );
    }
    SECTION( "working directory inside the root runs" )
    {
        const auto result = runEcho( root + "/inside" );
        REQUIRE( result.exitedCleanly() );
        REQUIRE_FALSE( result.refusedByPolicy );
    }
    SECTION( "manifest-constant working directory outside the root is refused before spawn" )
    {
        const auto result = runEcho( outside );
        REQUIRE_FALSE( result.started );
        REQUIRE( result.refusedByPolicy );
        REQUIRE( result.error.find( "workspace_escape" ) != std::string::npos );
    }
    SECTION( "argv path outside the root is refused" )
    {
        const auto result = runEcho( root, { "--output", outside + "/x.txt" } );
        REQUIRE_FALSE( result.started );
        REQUIRE( result.refusedByPolicy );
    }
    SECTION( "argv path inside the root passes" )
    {
        const auto result = runEcho( root, { "--output", root + "/inside/x.txt" } );
        REQUIRE( result.exitedCleanly() );
    }
    SECTION( "additional allowed root (plugin dir) passes" )
    {
        const auto result =
            runEcho( root, { "--payload", outside + "/payload.bin" }, { outside } );
        REQUIRE( result.exitedCleanly() );
    }
    SECTION( "env value pointing outside the root is refused" )
    {
        ExternalProcessRequest request;
        request.argv = { "/bin/echo", "-n", "ok" };
        request.workingDirectory = root;
        request.environment["HOME"] = outside;
        const auto result = ExternalProcess::run( request );
        REQUIRE_FALSE( result.started );
        REQUIRE( result.refusedByPolicy );
        REQUIRE( result.error.find( "environment" ) != std::string::npos );
    }
    SECTION( "relative .. argument escaping the contained cwd is refused" )
    {
        const auto result = runEcho( root + "/inside", { "../../../etc/passwd" } );
        REQUIRE_FALSE( result.started );
        REQUIRE( result.refusedByPolicy );
    }

    fs::remove_all( root );
    fs::remove_all( outside );
}
