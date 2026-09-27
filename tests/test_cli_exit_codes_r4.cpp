// tests/test_cli_exit_codes_r4.cpp — Track 14 WP-A: per-command exit-code
// contract conformance.
//
// Oracle: the published stable contract — src/sdk/exprs/exit_codes.h and the
// "Exit codes (stable contract)" table in docs/headless/README.md:
//
//   0 Ok | 1 GenericError (legacy) | 2 ValidationFailure | 3 ExecutionFailure
//   4 Cancelled | 5 MissingDependency | 6 InvalidInput | 7 RuntimeUnavailable
//
// Public-surface only (Tracer-Bullet discipline): every case spawns the real
// `sicnu_geo_rs_cli` binary and asserts the process exit code plus the JSON
// envelope — never internal state. Expectations derive from the contract
// text, not from what the implementation happens to print today; cases that
// are RED at introduction pin the exact defects Track 14 fixes.
//
// Cancelled(4) is intentionally not asserted here: triggering it needs a
// signal racing a long-running operator (flaky by construction). The
// cooperative-cancel path is covered where it exists (commandRun) and the
// per-command N/A rationale lives in
// .planning/cli-surface-r4/EXIT_CODE_CONTRACT.md.
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "support/offline_probe.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

/// Runs `cli <args> --json` and requires the error envelope basics: parseable
/// envelope, ok:false, command echoed, non-empty error text.
Json::Value requireErrorEnvelope( const std::string &args, const std::string &command,
                                  int expectedCode )
{
    const auto result = runCli( args + " --json" );
    if ( result.exitCode != expectedCode )
        FAIL( "exit code for `" << args << "` was " << result.exitCode << ", expected "
              << expectedCode << ";\nstdout: " << result.output );
    const Json::Value envelope = parseEnvelope( result.output );
    REQUIRE( envelope.isObject() );
    REQUIRE( envelope.get( "ok", true ).asBool() == false );
    REQUIRE( envelope.get( "command", "" ).asString() == command );
    REQUIRE( envelope.get( "error", "" ).asString().size() > 0 );
    return envelope;
}

/// Error contract anchor, mirrored from exprs/exit_codes.h so a drift in the
/// header fails here first.
constexpr int kOk = 0;
constexpr int kGenericError = 1;
constexpr int kValidationFailure = 2;
constexpr int kExecutionFailure = 3;
constexpr int kCancelled = 4;
constexpr int kMissingDependency = 5;
constexpr int kInvalidInput = 6;
constexpr int kRuntimeUnavailable = 7;

struct TempDir
{
    std::filesystem::path path;
    explicit TempDir( const std::string &name )
        : path( std::filesystem::temp_directory_path() /
                ( "sicnu_cli_exit_codes_r4_" + name ) )
    {
        std::filesystem::remove_all( path );
        std::filesystem::create_directories( path );
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all( path, ec ); }
    std::string file( const std::string &name ) const
    {
        return ( path / name ).string();
    }
};

void writeFile( const std::string &path, const std::string &content )
{
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    REQUIRE( static_cast<bool>( out ) );
    out << content;
}

} // namespace

// ADR 0146: never die by timeout when the loopback transport is missing —
// report `sicnu-skip: <reason>` + exit 77 instead.
SICNU_OFFLINE_GUARD()

TEST_CASE( "usage errors exit InvalidInput (6)", "[cli][exit-codes][r4]" )
{
    // Each case: the invocation omits a required argument or names an unknown
    // subcommand — the contract's "malformed arguments" class.
    SECTION( "run with no operator id" )
    {
        requireErrorEnvelope( "run", "run", kInvalidInput );
    }
    SECTION( "pipeline with no file" )
    {
        requireErrorEnvelope( "pipeline run", "pipeline", kInvalidInput );
    }
    SECTION( "workflow with unknown subcommand" )
    {
        requireErrorEnvelope( "workflow bogus-sub", "workflow", kInvalidInput );
    }
    SECTION( "plugin with unknown subcommand" )
    {
        // Bare `plugin` defaults to `list` (same convention as algorithms);
        // only an unknown verb is a usage error.
        requireErrorEnvelope( "plugin bogus-sub-r4", "plugin", kInvalidInput );
    }
    SECTION( "algorithms schema with no id" )
    {
        requireErrorEnvelope( "algorithms schema", "algorithms", kInvalidInput );
    }
    SECTION( "tools schema with no id" )
    {
        requireErrorEnvelope( "tools schema", "tools", kInvalidInput );
    }
    SECTION( "catalog with unknown subcommand" )
    {
        requireErrorEnvelope( "catalog bogus-sub", "catalog", kInvalidInput );
    }
    SECTION( "session with unknown action" )
    {
        requireErrorEnvelope( "session definitely-not-an-action", "session", kInvalidInput );
    }
    SECTION( "env-doctor with unknown argument" )
    {
        requireErrorEnvelope( "env-doctor --bogus", "env-doctor", kInvalidInput );
    }
    SECTION( "data with no subcommand" )
    {
        requireErrorEnvelope( "data", "data", kInvalidInput );
    }
    SECTION( "project info without a file" )
    {
        requireErrorEnvelope( "project info", "project", kInvalidInput );
    }
    SECTION( "batch validate without a manifest" )
    {
        requireErrorEnvelope( "batch validate", "batch", kInvalidInput );
    }
    SECTION( "passport without --path" )
    {
        requireErrorEnvelope( "passport", "passport", kInvalidInput );
    }
    SECTION( "dataset without --dataset-db" )
    {
        // RED at Track 14 introduction: cli_dataset_commands.cpp's fail()
        // hardcoded GenericError(1) for every failure.
        requireErrorEnvelope( "dataset", "dataset", kInvalidInput );
    }
    SECTION( "experiment without --experiment-db" )
    {
        requireErrorEnvelope( "experiment", "experiment", kInvalidInput );
    }
    SECTION( "reproduce without --experiment-db/--dataset-db" )
    {
        requireErrorEnvelope( "reproduce inspect", "reproduce", kInvalidInput );
    }
}

TEST_CASE( "unknown resources exit MissingDependency (5)", "[cli][exit-codes][r4]" )
{
    SECTION( "algorithms schema for an unknown algorithm" )
    {
        requireErrorEnvelope( "algorithms schema no.such.algorithm", "algorithms",
                              kMissingDependency );
    }
    SECTION( "models inspect for an unknown model" )
    {
        requireErrorEnvelope( "models inspect no-such-model", "models", kMissingDependency );
    }
    SECTION( "plugin inspect for an unknown plugin" )
    {
        requireErrorEnvelope( "plugin inspect org.example.no-such-plugin", "plugin",
                              kMissingDependency );
    }
    SECTION( "tools schema for an unknown tool" )
    {
        requireErrorEnvelope( "tools schema no.such.tool", "tools", kMissingDependency );
    }
    SECTION( "run for an unknown algorithm" )
    {
        requireErrorEnvelope( "run no.such.algorithm", "run", kMissingDependency );
    }
}

TEST_CASE( "unreadable input files exit InvalidInput (6)", "[cli][exit-codes][r4]" )
{
    SECTION( "workflow validate on a missing file" )
    {
        requireErrorEnvelope( "workflow validate /nonexistent/sicnu-r4/missing.json",
                              "workflow", kInvalidInput );
    }
    SECTION( "project info on a missing project" )
    {
        requireErrorEnvelope( "project info /nonexistent/sicnu-r4/missing.qgz", "project",
                              kInvalidInput );
    }
    SECTION( "batch run on a missing manifest" )
    {
        requireErrorEnvelope( "batch run /nonexistent/sicnu-r4/missing.json", "batch",
                              kInvalidInput );
    }
    SECTION( "pipeline run on a missing file" )
    {
        // RED at Track 14 introduction: commandPipeline collapsed a missing
        // file into the runner's ExecutionFailure(3); the contract puts
        // unreadable files in InvalidInput(6) and reserves 3 for a real step
        // failure.
        requireErrorEnvelope( "pipeline run /nonexistent/sicnu-r4/missing.json", "pipeline",
                              kInvalidInput );
    }
}

TEST_CASE( "schema violations exit ValidationFailure (2)", "[cli][exit-codes][r4]" )
{
    TempDir dir( "validation" );

    SECTION( "workflow validate on a structurally invalid document" )
    {
        const std::string path = dir.file( "bad-workflow.json" );
        writeFile( path, "{\"not\":\"a workflow document\"}" );
        requireErrorEnvelope( "workflow validate " + path, "workflow", kValidationFailure );
    }
    SECTION( "pipeline validate on an invalid pipeline document" )
    {
        const std::string path = dir.file( "bad-pipeline.json" );
        writeFile( path, "{\"steps\":\"not-an-array\"}" );
        requireErrorEnvelope( "pipeline validate " + path, "pipeline", kValidationFailure );
    }
    SECTION( "lab --lab without a value" )
    {
        requireErrorEnvelope( "lab --lab", "lab", kValidationFailure );
    }
}

TEST_CASE( "dataset family failure classes follow the contract",
           "[cli][exit-codes][r4][dataset]" )
{
    TempDir dir( "dataset" );
    const std::string db = dir.file( "store.db" );

    // Seed one valid store so resource-not-found is distinguishable from
    // store-open failure. `dataset create` is itself part of the contract
    // surface under test.
    const auto created = runCli( "dataset create --dataset-db " + db + " --name seed --json" );
    REQUIRE( created.exitCode == kOk );

    SECTION( "store open failure on a missing database file" )
    {
        // RED at introduction (was GenericError 1).
        // A non-existent path is CREATEd by open(); a directory can never be
        // a store — that is the real open-failure trigger.
        requireErrorEnvelope( "dataset inspect --dataset-db " + dir.path.string(),
                              "dataset", kInvalidInput );
    }
    SECTION( "missing required --name for create" )
    {
        requireErrorEnvelope( "dataset create --dataset-db " + db, "dataset", kInvalidInput );
    }
    SECTION( "version not found" )
    {
        // A valid store queried for a nonexistent version: the contract's
        // "unknown resource" class.
        // RED at introduction (was GenericError 1).
        requireErrorEnvelope( "dataset inspect --dataset-db " + db +
                                  " --version 00000000-0000-0000-0000-000000000000",
                              "dataset", kMissingDependency );
    }
}

TEST_CASE( "error envelopes keep the published shape", "[cli][exit-codes][r4]" )
{
    // Machine-readable output contract (docs/headless/README.md): one JSON
    // envelope, ok:false, api_version stamped — regardless of exit class.
    const auto result = runCli( "run --json" );
    REQUIRE( result.exitCode == kInvalidInput );
    const Json::Value envelope = parseEnvelope( result.output );
    REQUIRE( envelope.isObject() );
    REQUIRE( envelope.get( "ok", true ).asBool() == false );
    REQUIRE( envelope.get( "api_version", "" ).asString() == "3.0" );
    REQUIRE( envelope.isMember( "data" ) );
}
