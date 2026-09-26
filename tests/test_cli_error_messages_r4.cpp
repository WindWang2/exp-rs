// tests/test_cli_error_messages_r4.cpp — Track 14 WP-B: structured error
// message contract (the four-tuple) on command-level failure paths.
//
// Oracle: the error-message standard this track publishes (see
// .planning/cli-surface-r4/EXIT_CODE_CONTRACT.md and the added section in
// docs/headless/README.md):
//
//   every command-level error carries
//     (1) an error code  — "[E-<exit>:<SYMBOL>]" anchored to the published
//         exit-code contract (exprs/exit_codes.h),
//     (2) a one-sentence reason (the existing `error` text),
//     (3) expected/actual when the failure is a mismatch,
//     (4) a suggested action (usage line or fix hint).
//
// Text mode prints one stderr line:
//     [E-6:INVALID_INPUT] <reason>; expected: <x>; actual: <y>; hint: <z>
// (optional segments omitted when not applicable). JSON mode keeps the
// published envelope and adds `error_details` {code, expected, actual, hint}
// — additive only; `error`, `ok`, `command`, `data`, `diagnostics`,
// `api_version` keep their published meaning (asserted below).
//
// Public surface only: process stdout/stderr + exit code.
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "support/offline_probe.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

#ifndef SICNU_TEST_CLI
#error "SICNU_TEST_CLI must point at sicnu_geo_rs_cli"
#endif

namespace {

struct RunResult
{
    int exitCode = -1;
    std::string out;
    std::string err;
};

RunResult runCli( const std::string &args, bool mergeStderr )
{
    std::string redirect = mergeStderr ? " 2>&1" : " 2>/dev/null";
    FILE *pipe = ::popen( ( std::string( SICNU_TEST_CLI ) + " " + args + redirect ).c_str(),
                          "r" );
    REQUIRE( pipe != nullptr );
    char buffer[4096];
    size_t read = 0;
    RunResult result;
    while ( ( read = fread( buffer, 1, sizeof( buffer ), pipe ) ) > 0 )
        result.out.append( buffer, read );
    const int status = ::pclose( pipe );
    result.exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
    return result;
}

Json::Value parseJson( const std::string &text )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( text, root, false );
    return root;
}

/// Asserts the text-mode four-tuple on stderr's first line: the
/// "[E-<code>:<SYMBOL>]" prefix, then `expected:`/`actual:`/`hint:` segments
/// when @p wantExpected/@p wantActual/@p wantHint say the path carries them.
/// Free text after the anchors is deliberately not pinned.
void requireFourTuple( const std::string &args, int code, const std::string &symbol,
                       bool wantExpected, bool wantActual, bool wantHint )
{
    const auto result = runCli( args, true /* merge stderr */ );
    if ( result.exitCode != code )
        FAIL( "exit code for `" << args << "` was " << result.exitCode << ", expected " << code
              << ";\noutput: " << result.out );

    // First stderr line = first output line in merged mode only when stdout
    // is silent on error paths; be strict about that (stdout carries only the
    // final envelope in --json mode, and text-mode errors print to stderr).
    const std::string::size_type nl = result.out.find( '\n' );
    const std::string firstLine = nl == std::string::npos ? result.out : result.out.substr( 0, nl );

    const std::string prefix = "[E-" + std::to_string( code ) + ":" + symbol + "]";
    INFO( "first line was: " << firstLine );
    REQUIRE( firstLine.rfind( prefix, 0 ) == 0 );
    REQUIRE( firstLine.size() > prefix.size() + 2 ); // reason text present
    REQUIRE( ( wantExpected ) == ( firstLine.find( "expected:" ) != std::string::npos ) );
    REQUIRE( ( wantActual ) == ( firstLine.find( "actual:" ) != std::string::npos ) );
    REQUIRE( ( wantHint ) == ( firstLine.find( "hint:" ) != std::string::npos ) );
}

/// Asserts the JSON envelope carries additive `error_details` with the code
/// anchor while every published field keeps its meaning.
void requireErrorDetails( const std::string &args, const std::string &command, int code,
                          const std::string &symbol )
{
    const auto result = runCli( args + " --json", false );
    if ( result.exitCode != code )
        FAIL( "exit code for `" << args << "` was " << result.exitCode << ", expected " << code
              << ";\nstdout: " << result.out );
    const Json::Value envelope = parseJson( result.out );
    REQUIRE( envelope.isObject() );
    REQUIRE( envelope.get( "ok", true ).asBool() == false );
    REQUIRE( envelope.get( "command", "" ).asString() == command );
    REQUIRE( envelope.get( "error", "" ).asString().size() > 0 );
    REQUIRE( envelope.get( "api_version", "" ).asString() == "3.0" );

    const Json::Value details = envelope["error_details"];
    REQUIRE( details.isObject() );
    const std::string expectedCode =
        "E-" + std::to_string( code ) + ":" + symbol;
    if ( details.get( "code", "" ).asString() != expectedCode )
        FAIL( "error_details.code was `" << details.get( "code", "" ).asString()
              << "`, expected `" << expectedCode << "`" );
}

struct TempDir
{
    std::filesystem::path path;
    explicit TempDir( const std::string &name )
        : path( std::filesystem::temp_directory_path() /
                ( "sicnu_cli_err_r4_" + name ) )
    {
        std::filesystem::remove_all( path );
        std::filesystem::create_directories( path );
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all( path, ec ); }
    std::string file( const std::string &name ) const { return ( path / name ).string(); }
};

void writeFile( const std::string &path, const std::string &content )
{
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    REQUIRE( static_cast<bool>( out ) );
    out << content;
}

} // namespace

SICNU_OFFLINE_GUARD()

// ---------------------------------------------------------------------------
// The RED anchors: paths that today print a bare message (no code prefix, no
// expected/actual/hint). Each assertion below pins the published four-tuple;
// the accompanying implementation commit migrates the sites through the ONE
// finish-layer helper (no per-command string assembly).
// ---------------------------------------------------------------------------

TEST_CASE( "dataset argument errors carry the four-tuple", "[cli][errors][r4][dataset]" )
{
    SECTION( "missing --dataset-db" )
    {
        requireFourTuple( "dataset", 6, "INVALID_INPUT", /*expected*/ true, /*actual*/ false,
                          /*hint*/ true );
        requireErrorDetails( "dataset", "dataset", 6, "INVALID_INPUT" );
    }
    SECTION( "missing --name for create" )
    {
        requireFourTuple( "dataset create --dataset-db /nonexistent/sicnu-r4.db", 6,
                          "INVALID_INPUT", true, false, true );
    }
    SECTION( "unknown dataset subcommand" )
    {
        requireFourTuple( "dataset bogus-sub --dataset-db /nonexistent/sicnu-r4.db", 6,
                          "INVALID_INPUT", true, /*actual*/ true, true );
    }
    SECTION( "store open failure names the unreadable path" )
    {
        const std::string missing = "/nonexistent/sicnu-r4/missing.db";
        const auto result = runCli( "dataset --dataset-db " + missing + " inspect", true );
        REQUIRE( result.exitCode == 6 );
        REQUIRE( result.out.find( "[E-6:INVALID_INPUT]" ) == 0 );
        REQUIRE( result.out.find( missing ) != std::string::npos );
    }
}

TEST_CASE( "experiment and reproduce argument errors carry the four-tuple",
           "[cli][errors][r4]" )
{
    SECTION( "experiment without --experiment-db" )
    {
        requireFourTuple( "experiment", 6, "INVALID_INPUT", true, false, true );
    }
    SECTION( "reproduce without required stores" )
    {
        requireFourTuple( "reproduce inspect", 6, "INVALID_INPUT", true, false, true );
    }
}

TEST_CASE( "resource-not-found errors name expected vs actual",
           "[cli][errors][r4]" )
{
    TempDir dir( "notfound" );
    const std::string db = dir.file( "store.db" );
    const auto seeded = runCli( "dataset create --dataset-db " + db + " --name seed --json",
                                false );
    REQUIRE( seeded.exitCode == 0 );

    SECTION( "dataset version not found" )
    {
        // expected: an existing version id; actual: the unknown id.
        requireFourTuple(
            "dataset --dataset-db " + db +
                " version --version 00000000-0000-0000-0000-000000000000",
            5, "MISSING_DEPENDENCY", true, true, false );
    }
    SECTION( "algorithms schema for unknown id (already 5; pins the code prefix)" )
    {
        requireFourTuple( "algorithms schema no.such.algorithm", 5, "MISSING_DEPENDENCY",
                          false, false, false );
    }
}

TEST_CASE( "usage hints ride along on usage errors", "[cli][errors][r4]" )
{
    SECTION( "run with no operator id" )
    {
        requireFourTuple( "run", 6, "INVALID_INPUT", false, false, true );
    }
    SECTION( "pipeline run without a file" )
    {
        requireFourTuple( "pipeline run", 6, "INVALID_INPUT", false, false, true );
    }
    SECTION( "pipeline run on a missing file (class fixed by WP-A)" )
    {
        requireFourTuple( "pipeline run /nonexistent/sicnu-r4/missing.json", 6,
                          "INVALID_INPUT", false, false, false );
    }
}

TEST_CASE( "plugin uninstall failure is classified, not bare", "[cli][errors][r4]" )
{
    // RED at introduction: hardcoded GenericError(1) with a bare
    // "uninstall failed" message. Contract class for "unknown plugin id" is
    // MissingDependency(5) — same as `plugin inspect` on an unknown id.
    requireFourTuple( "plugin uninstall org.example.no-such-plugin", 5, "MISSING_DEPENDENCY",
                      false, false, false );
}
