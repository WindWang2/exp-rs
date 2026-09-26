// tests/test_exprs_external_process.cpp — safe external process contract
#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"

#include <filesystem>
#include <fstream>

#include <algorithm>

#ifdef _WIN32
#include <cstdlib> // _exit
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

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

TEST_CASE( "exited child with a pipe-holding descendant is not a timeout (issue #1041)",
           "[sdk][external]" )
{
    // The shell exits immediately; the backgrounded sleep inherits the
    // stdout/stderr write ends and keeps the pipes open past the exit. The
    // pre-fix loop broke only on pipe EOF, spun to the deadline and reported
    // timedOut for a child that had completed successfully.
    ExternalProcessRequest request;
    request.argv = { "/bin/sh", "-c", "echo tail-output; sleep 5 & exit 0" };
    request.timeoutSeconds = 1;
    const auto result = ExternalProcess::run( request );
    REQUIRE( result.started );
    REQUIRE_FALSE( result.timedOut );
    REQUIRE_FALSE( result.cancelled );
    REQUIRE( result.exitCode == 0 );
    // Bounded post-exit drain: the tail is collected, and the descendant
    // cannot stall run() past the grace window.
    REQUIRE( result.stdOut.find( "tail-output" ) != std::string::npos );
    REQUIRE( result.durationMs < 10000 );
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

TEST_CASE( "PATH walk: pure helper resolves entries, empty entry is the "
           "current directory, entries beyond MAX_PATH are not truncated",
           "[sdk][external][path_lookup]" )
{
    std::vector<std::string> probes;
    const auto probe = [ & ]( const std::string &candidate ) {
        probes.push_back( candidate );
        return candidate.rfind( "/hit/program" ) != std::string::npos
               || candidate == "./program";
    };

    // First matching entry wins; later entries are still spelled correctly.
    probes.clear();
    REQUIRE( programInSearchPath( "program", "/a:/hit:/c", ':', probe ) );
    REQUIRE( probes.size() == 2 );
    REQUIRE( probes[0] == "/a/program" );
    REQUIRE( probes[1] == "/hit/program" );

    // An EMPTY entry is the current directory (POSIX exec convention):
    // ":/x" and "x:" and "::" all probe "./program", never "/program".
    for ( const std::string &searchPath : { std::string( ":/x" ), std::string( "x:" ),
                                            std::string( ":" ) } )
    {
        probes.clear();
        REQUIRE( programInSearchPath( "program", searchPath, ':', probe ) );
        INFO( "searchPath: " << searchPath );
        REQUIRE( std::find( probes.begin(), probes.end(), "./program" ) != probes.end() );
        REQUIRE( std::find( probes.begin(), probes.end(), "/program" ) == probes.end() );
    }

    // A single empty search path still probes the current directory.
    probes.clear();
    REQUIRE( programInSearchPath( "program", "", ':', probe ) );
    REQUIRE( probes == std::vector<std::string>{ "./program" } );

    // An entry far beyond MAX_PATH passes through untruncated: the mock
    // oracle pins the pure string contract that a filesystem probe on any
    // one platform could silently violate (static evidence for the Windows
    // SearchPathW ladder, which retries beyond its MAX_PATH stack buffer).
    const std::string huge( 5000, 'd' );
    probes.clear();
    REQUIRE( programInSearchPath( "program", huge + ":/hit", ':', probe ) );
    REQUIRE( probes.size() == 2 );
    REQUIRE( probes[0] == huge + "/program" );
    REQUIRE( probes[0].size() == 5000 + 8 );
    REQUIRE( probes[1] == "/hit/program" );

    // No entry matches -> false.
    REQUIRE_FALSE( programInSearchPath( "miss", "/a:/b", ':',
                                        []( const std::string & ) { return false; } ) );
}

#if !defined( _WIN32 )
TEST_CASE( "PATH walk: an empty PATH entry resolves a program in the "
           "current working directory",
           "[sdk][external][path_lookup]" )
{
    // The old inline walk spliced an empty entry into "/program" and probed
    // the filesystem ROOT, so a bare program name sitting in the CWD with
    // PATH=":<anything>" was misreported as "not found in PATH".
    const std::filesystem::path program = "sicnu-path-lookup-prog";
    {
        std::ofstream out( program, std::ios::trunc | std::ios::binary );
        REQUIRE( static_cast<bool>( out ) );
        out << "#!/bin/sh\nexit 0\n";
    }
    REQUIRE( ::chmod( program.c_str(), 0755 ) == 0 );

    const char *savedPath = ::getenv( "PATH" );
    const std::string savedValue = savedPath ? savedPath : "";
    REQUIRE( ::setenv( "PATH", ":/definitely/not/here", 1 ) == 0 );
    std::string error;
    const bool found = ExternalProcess::validateArgv( { program.string() }, error );
    if ( savedPath )
        REQUIRE( ::setenv( "PATH", savedValue.c_str(), 1 ) == 0 );
    else
        ::unsetenv( "PATH" );
    std::filesystem::remove( program );

    INFO( "validateArgv error: " << error );
    REQUIRE( found );
}
#endif

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
#ifdef _WIN32
        // MSVC has no POSIX setenv/unsetenv; _putenv with an empty value
        // removes the variable (documented CRT behavior).
        if ( value )
            ::_putenv_s( "SICNU_MCP_WORKSPACE", value );
        else
            ::_putenv( "SICNU_MCP_WORKSPACE=" );
#else
        if ( value )
            ::setenv( "SICNU_MCP_WORKSPACE", value, 1 );
        else
            ::unsetenv( "SICNU_MCP_WORKSPACE" );
#endif
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
