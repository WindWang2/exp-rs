// tests/test_exprs_external_process_win.cpp — Windows external-process
// implementation (isolation runtime 5.0, M3). Parity lane for the scenarios
// the POSIX suite covers with shell fixtures; uses the exprs_ep_helper
// binary so assertions are identical on both platforms.
#include <catch2/catch_test_macros.hpp>

#include "exprs/external_process.h"

#include <json/json.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace exprs;

namespace {

const char *kHelper = SICNU_TEST_EP_HELPER;

ExternalProcessRequest basicRequest( const std::vector<std::string> &arguments )
{
    ExternalProcessRequest request;
    request.argv.push_back( kHelper );
    for ( const std::string &argument : arguments )
        request.argv.push_back( argument );
    request.timeoutSeconds = 20;
    return request;
}

std::string trim( const std::string &text )
{
    size_t start = 0;
    size_t end = text.size();
    while ( start < end && ( text[ start ] == '\n' || text[ start ] == '\r' ) )
        ++start;
    while ( end > start && ( text[ end - 1 ] == '\n' || text[ end - 1 ] == '\r' ) )
        --end;
    return text.substr( start, end - start );
}

/// Marks a process id dead (best effort): OpenProcess fails, or the handle
/// reports signaled within a short grace window. PIDs are reused by the OS,
/// so this checks promptly after the kill while reuse is unlikely.
bool processGone( long long pid )
{
#ifdef _WIN32
    const HANDLE handle = ::OpenProcess( SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, static_cast<DWORD>( pid ) );
    if ( !handle )
        return true; // already reaped
    const DWORD wait =
        ::WaitForSingleObject( handle, 2000 );
    ::CloseHandle( handle );
    return wait == WAIT_OBJECT_0 || wait == WAIT_FAILED;
#else
    (void)pid;
    return true; // POSIX kill-tree is asserted by the POSIX lane
#endif
}

} // namespace

TEST_CASE( "windows spawn runs argv exactly (quoting round-trip)", "[external][win]" )
{
    ExternalProcessRequest request = basicRequest( {
        "argv",
        "plain",
        "two words",
        "quote\"inside",
        "back\\slash",
        "trail\\",
        "",
        "tab\targ",
    } );
    const ExternalProcessResult result = ExternalProcess::run( request );
    REQUIRE( result.started );
    REQUIRE( result.exitedCleanly() );
    REQUIRE( result.exitCode == 0 );

    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string error;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( result.stdOut.data(), result.stdOut.data() + result.stdOut.size(),
                            &parsed, &error ) );
    REQUIRE( parsed.isArray() );
    REQUIRE( parsed.size() == 7 );
    REQUIRE( parsed[ 0 ].asString() == "plain" );
    REQUIRE( parsed[ 1 ].asString() == "two words" );
    REQUIRE( parsed[ 2 ].asString() == "quote\"inside" );
    REQUIRE( parsed[ 3 ].asString() == "back\\slash" );
    REQUIRE( parsed[ 4 ].asString() == "trail\\" );
    REQUIRE( parsed[ 5 ].asString() == "" );
    REQUIRE( parsed[ 6 ].asString() == "tab\targ" );
}

TEST_CASE( "windows env is allow-listed by default and explicit entries win",
           "[external][win]" )
{
    // Parent has a secret; the child must not see it.
#ifdef _WIN32
    ::SetEnvironmentVariableA( "SICNU_EP_SECRET", "leak-me" );
#else
    setenv( "SICNU_EP_SECRET", "leak-me", 1 );
#endif

    ExternalProcessRequest request = basicRequest( { "env", "SICNU_EP_SECRET" } );
    const ExternalProcessResult denied = ExternalProcess::run( request );
    REQUIRE( denied.exitedCleanly() );
    REQUIRE( trim( denied.stdOut ).empty() );

    // Explicit env passes through.
    request.environment[ "SICNU_EP_SECRET" ] = "declared";
    const ExternalProcessResult declared = ExternalProcess::run( request );
    REQUIRE( declared.exitedCleanly() );
    REQUIRE( trim( declared.stdOut ) == "declared" );
}

TEST_CASE( "windows workingDirectory is honored", "[external][win]" )
{
    const std::string workspace =
        std::filesystem::current_path().generic_string();
    ExternalProcessRequest request = basicRequest( { "cwd" } );
    request.workingDirectory = workspace;
    const ExternalProcessResult result = ExternalProcess::run( request );
    REQUIRE( result.exitedCleanly() );
    const std::string reported = trim( result.stdOut );
    // Case, separators and trailing separators may differ; normalize both.
    auto normalized = []( std::string text ) {
        for ( char &c : text )
        {
            c = static_cast<char>( ::tolower( static_cast<unsigned char>( c ) ) );
            if ( c == '\\' )
                c = '/';
        }
        while ( text.size() > 1 && text.back() == '/' )
            text.pop_back();
        return text;
    };
    REQUIRE( normalized( reported ) == normalized( workspace ) );
}

TEST_CASE( "windows exit codes and output caps", "[external][win]" )
{
    ExternalProcessRequest failure = basicRequest( { "exit", "3" } );
    const ExternalProcessResult failureResult = ExternalProcess::run( failure );
    REQUIRE( failureResult.started );
    REQUIRE_FALSE( failureResult.exitedCleanly() );
    REQUIRE( failureResult.exitCode == 3 );

    ExternalProcessRequest flood = basicRequest( { "write-bytes", "1000000" } );
    flood.stdoutLimitBytes = 1000;
    const ExternalProcessResult floodResult = ExternalProcess::run( flood );
    REQUIRE( floodResult.exitedCleanly() );
    REQUIRE( floodResult.truncatedStdout );
    REQUIRE( floodResult.stdOut.size() == 1000 );
}

TEST_CASE( "windows timeout kills the whole tree", "[external][win]" )
{
    ExternalProcessRequest request = basicRequest( { "spawn-sleep", "600000" } );
    request.timeoutSeconds = 1;
    const auto start = std::chrono::steady_clock::now();
    const ExternalProcessResult result = ExternalProcess::run( request );
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE( result.started );
    REQUIRE( result.timedOut );
    REQUIRE( result.exitCode == -1 );
    REQUIRE( result.exitSignal == 9 );
    REQUIRE( elapsed < std::chrono::seconds( 10 ) );

    // The grandchild (spawned by the helper) must have died with the tree.
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string error;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    REQUIRE( reader->parse( result.stdOut.data(), result.stdOut.data() + result.stdOut.size(),
                            &parsed, &error ) );
    REQUIRE( parsed.isMember( "child" ) );
    const long long childPid = std::stoll( parsed[ "child" ].asString() );
    int attempts = 0;
    while ( !processGone( childPid ) && attempts < 20 )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        ++attempts;
    }
    REQUIRE( processGone( childPid ) );
}

TEST_CASE( "windows cancellation kills the process", "[external][win]" )
{
    std::atomic<bool> cancelRequested{ false };
    ExternalProcessRequest request = basicRequest( { "spin" } );
    request.timeoutSeconds = 60;
    request.isCancelled = [&cancelRequested] {
        return cancelRequested.load();
    };
    std::thread canceller( [&cancelRequested] {
        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        cancelRequested = true;
    } );
    const ExternalProcessResult result = ExternalProcess::run( request );
    canceller.join();
    REQUIRE( result.started );
    REQUIRE( result.cancelled );
    REQUIRE( result.exitCode == -1 );
    REQUIRE( result.exitSignal == 9 );
}

TEST_CASE( "windows spawn failure is a typed non-crash", "[external][win]" )
{
    ExternalProcessRequest request;
    request.argv = { "Z:/definitely/not/a/real/program.exe", "--flag" };
    const ExternalProcessResult result = ExternalProcess::run( request );
    REQUIRE_FALSE( result.started );
    REQUIRE( result.exitCode == -1 );
    REQUIRE_FALSE( result.error.empty() );
}

TEST_CASE( "windows workspace effect policy refuses before spawn", "[external][win]" )
{
    // Activate the workspace policy against a scratch root; a working
    // directory outside it must be refused without executing anything.
    std::filesystem::path workspace =
        std::filesystem::temp_directory_path() / "sicnu-ep-policy";
    std::filesystem::create_directories( workspace );
    const std::string workspaceEnv =
        "SICNU_MCP_WORKSPACE=" + workspace.generic_string();
    // _putenv (CRT) so PathPolicy::workspaceRoot()'s getenv sees the value;
    // MSVC's _putenv keeps the Win32 block in sync for child inheritance.
    _putenv( workspaceEnv.c_str() );

    ExternalProcessRequest outside = basicRequest( { "cwd" } );
    outside.workingDirectory = std::filesystem::temp_directory_path().parent_path().generic_string();
    const ExternalProcessResult refused = ExternalProcess::run( outside );
    REQUIRE( refused.refusedByPolicy );
    REQUIRE_FALSE( refused.started );
    REQUIRE( refused.error.find( "E5005" ) != std::string::npos );

    ExternalProcessRequest inside = basicRequest( { "cwd" } );
    inside.workingDirectory = workspace.generic_string();
    const ExternalProcessResult allowed = ExternalProcess::run( inside );
    REQUIRE( allowed.started );
    REQUIRE( allowed.exitedCleanly() );
    REQUIRE_FALSE( allowed.refusedByPolicy );

    _putenv( "SICNU_MCP_WORKSPACE=" );
}
