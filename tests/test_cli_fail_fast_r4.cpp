// tests/test_cli_fail_fast_r4.cpp — Track 14 WP-E: parse-time rejection with
// zero side effects.
//
// Oracle: the fail-fast rule published by this track — a command whose
// arguments are invalid (unknown subcommand/flag, missing required flag,
// malformed value) must be rejected BEFORE any store is opened or any file
// is written, i.e. the process exit code is InvalidInput(6)/
// ValidationFailure(2) AND no artifact appears on disk. Exit code alone is
// not the assertion; the side-effect check (store file absent / unchanged)
// is.
//
// Public surface only: process exit code + filesystem effects.
#include <catch2/catch_test_macros.hpp>

#include "support/offline_probe.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
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
    FILE *pipe = ::popen( ( std::string( SICNU_TEST_CLI ) + " " + args + " 2>/dev/null" ).c_str(),
                          "r" );
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

struct TempDir
{
    std::filesystem::path path;
    explicit TempDir( const std::string &name )
        : path( std::filesystem::temp_directory_path() /
                ( "sicnu_cli_failfast_r4_" + name ) )
    {
        std::filesystem::remove_all( path );
        std::filesystem::create_directories( path );
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all( path, ec ); }
    std::string file( const std::string &name ) const { return ( path / name ).string(); }
};

constexpr int kInvalidInput = 6;

} // namespace

SICNU_OFFLINE_GUARD()

TEST_CASE( "cli unknown subcommands are rejected at parse time", "[cli][fail-fast][r4]" )
{
    // RED at Track 14 introduction: `algorithms <bogus>` and `models <bogus>`
    // silently fell through to `list` and exited 0 — the command "succeeded"
    // while doing something the user never asked for.
    SECTION( "algorithms bogus-sub" )
    {
        const auto result = runCli( "algorithms bogus-sub --json" );
        REQUIRE( result.exitCode == kInvalidInput );
    }
    SECTION( "models bogus-sub" )
    {
        const auto result = runCli( "models bogus-sub --json" );
        REQUIRE( result.exitCode == kInvalidInput );
    }
    SECTION( "models inspect without a name" )
    {
        const auto result = runCli( "models inspect --json" );
        REQUIRE( result.exitCode == kInvalidInput );
    }
    SECTION( "session unknown action" )
    {
        const auto result = runCli( "session bogus-action --json" );
        REQUIRE( result.exitCode == kInvalidInput );
    }
    SECTION( "env-doctor unknown argument" )
    {
        const auto result = runCli( "env-doctor --bogus --json" );
        REQUIRE( result.exitCode == kInvalidInput );
    }
}

TEST_CASE( "cli unknown flags are rejected, not silently ignored",
           "[cli][fail-fast][r4]" )
{
    // RED at introduction: `run` ignored flags it does not know and executed
    // the operator anyway.
    SECTION( "run with an unknown flag" )
    {
        const auto result = runCli( "run demo:stats --totally-bogus-flag --param 'values=[1]' --json" );
        REQUIRE( result.exitCode == kInvalidInput );
    }
    SECTION( "algorithms list with an unknown flag" )
    {
        const auto result = runCli( "algorithms list --totally-bogus-flag --json" );
        REQUIRE( result.exitCode == kInvalidInput );
    }
}

TEST_CASE( "cli missing required flags are rejected before any store is created",
           "[cli][fail-fast][r4][side-effects]" )
{
    // DatasetStore::open() uses SQLITE_OPEN_CREATE: opening a not-yet-existing
    // path creates the database file. A rejected command must therefore run
    // its argument validation BEFORE the open — the store file must still not
    // exist afterwards.
    SECTION( "dataset create without --name" )
    {
        TempDir dir( "ds_name" );
        const std::string db = dir.file( "store.db" );
        const auto result = runCli( "dataset create --dataset-db " + db + " --json" );
        REQUIRE( result.exitCode == kInvalidInput );
        INFO( "store file must not exist after a rejected command" );
        REQUIRE( !std::filesystem::exists( db ) );
    }
    SECTION( "experiment create without --name" )
    {
        TempDir dir( "exp_name" );
        const std::string db = dir.file( "store.db" );
        const auto result = runCli( "experiment create --experiment-db " + db + " --json" );
        REQUIRE( result.exitCode == kInvalidInput );
        REQUIRE( !std::filesystem::exists( db ) );
    }
    SECTION( "reproduce export without --run/--out" )
    {
        TempDir dir( "repro_run" );
        const std::string expDb = dir.file( "exp.db" );
        const std::string dsDb = dir.file( "ds.db" );
        const auto result =
            runCli( "reproduce export --experiment-db " + expDb + " --dataset-db " + dsDb + " --json" );
        REQUIRE( result.exitCode == kInvalidInput );
        REQUIRE( !std::filesystem::exists( expDb ) );
        REQUIRE( !std::filesystem::exists( dsDb ) );
    }
    SECTION( "reproduce validate without --bundle" )
    {
        TempDir dir( "repro_bundle" );
        const std::string expDb = dir.file( "exp.db" );
        const std::string dsDb = dir.file( "ds.db" );
        const auto result =
            runCli( "reproduce validate --experiment-db " + expDb + " --dataset-db " + dsDb + " --json" );
        REQUIRE( result.exitCode == kInvalidInput );
        REQUIRE( !std::filesystem::exists( expDb ) );
        REQUIRE( !std::filesystem::exists( dsDb ) );
    }
}

TEST_CASE( "cli malformed values are rejected at parse time", "[cli][fail-fast][r4]" )
{
    SECTION( "run --param without key=value" )
    {
        const auto result = runCli( "run demo:stats --param notapair --json" );
        REQUIRE( result.exitCode == kInvalidInput );
    }
    SECTION( "lab --max-bytes with a non-integer" )
    {
        const auto result = runCli( "lab --lab some-lab --grade some-file --max-bytes abc --json" );
        REQUIRE( result.exitCode == 2 ); // ValidationFailure: value schema
    }
    SECTION( "lab --max-submissions with a non-integer" )
    {
        const auto result =
            runCli( "lab --lab some-lab --grade some-file --max-submissions abc --json" );
        REQUIRE( result.exitCode == 2 );
    }
}
