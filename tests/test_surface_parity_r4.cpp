// tests/test_surface_parity_r4.cpp — Track 14 WP-D: automated parity
// assertions for the CLI/MCP/GUI three-surface operation table
// (.planning/cli-surface-r4/SURFACE_PARITY.md).
//
// The three surfaces share one truth per operation family —
// AtomicAlgorithmRegistry, DatasetStore/ExperimentStore,
// WorkflowRunCoordinator+TaskCenter (see SURFACE_PARITY.md §1). These
// assertions pin the CLI side of each shared invariant: the envelope data
// shapes the MCP projections mirror, the filter/parameter vocabulary, the
// resource-not-found vs invalid-input vs validation error classes, and the
// fail-fast store semantics. GUI-side rows are manual cross-checks recorded
// in SURFACE_PARITY.md §3.
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

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

Json::Value parseJson( const std::string &text )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( text, root, false );
    return root;
}

Json::Value runForEnvelope( const std::string &args, int expectedCode )
{
    const auto result = runCli( args );
    if ( result.exitCode != expectedCode )
        FAIL( "exit code for `" << args << "` was " << result.exitCode << ", expected "
              << expectedCode << ";\nstdout: " << result.output );
    return parseJson( result.output );
}

struct TempDir
{
    std::filesystem::path path;
    explicit TempDir( const std::string &name )
        : path( std::filesystem::temp_directory_path() /
                ( "sicnu_surface_parity_r4_" + name ) )
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

TEST_CASE( "parity 1-2: algorithm search shares one authoritative engine shape",
           "[parity][r4]" )
{
    // MCP search_algorithms projects {algorithms, count, total, limit,
    // cursor, next_cursor} from the same engine; the CLI envelope must carry
    // the same shape so both surfaces answer the identical contract.
    const Json::Value envelope = runForEnvelope( "algorithms search --json", 0 );
    const Json::Value data = envelope["data"];
    REQUIRE( data.isObject() );
    for ( const char *key : { "algorithms", "count", "total", "limit", "cursor" } )
    {
        INFO( "search data must carry " << key );
        REQUIRE( data.isMember( key ) );
    }

    // Zero-hit search stays ok:true with count:0 (hints vocabulary is the
    // MCP parity behavior documented in docs/headless/README.md).
    const Json::Value zero = runForEnvelope( "algorithms search __definitely_no_algo_r4__ --json", 0 );
    REQUIRE( zero["data"].get( "count", -1 ).asInt64() == 0 );

    // Non-integer --limit is InvalidInput(6), matching the typed limit
    // parameter on the MCP side (no silent coercion).
    runForEnvelope( "algorithms search --limit abc --json", 6 );
}

TEST_CASE( "parity 3-4: unknown algorithm = missing dependency on both surfaces",
           "[parity][r4]" )
{
    // MCP: "Algorithm not found: <id>" isError; CLI: MissingDependency(5)
    // with the structured anchor. Same semantics, per-surface encoding.
    const Json::Value envelope = runForEnvelope( "run no.such.algorithm_parity --json", 5 );
    REQUIRE( envelope.get( "error", "" ).asString().find( "no.such.algorithm_parity" ) !=
             std::string::npos );
    const Json::Value schema = runForEnvelope( "algorithms schema no.such.algorithm_parity --json", 5 );
    REQUIRE( schema.get( "ok", true ).asBool() == false );
}

TEST_CASE( "parity 8-11: dataset family store semantics are the MCP dataset:* truth",
           "[parity][r4][dataset]" )
{
    TempDir dir( "parity_ds" );
    const std::string db = dir.file( "store.db" );

    // Missing --dataset-db is rejected before anything happens (the CLI form
    // of MCP's "dataset_db is required").
    runForEnvelope( "dataset list --json", 6 );

    // A fresh store lists empty, then create->stats round-trips through the
    // same DatasetStore the MCP dataset:* family and the GUI panel open.
    const Json::Value listed = runForEnvelope( "dataset list --dataset-db " + db + " --json", 0 );
    REQUIRE( listed.get( "ok", true ).asBool() == true );

    const Json::Value created = runForEnvelope(
        "dataset create --dataset-db " + db + " --name parity-seed --json", 0 );
    REQUIRE( created["data"].isMember( "dataset_id" ) );
    const std::string datasetId = created["data"]["dataset_id"].asString();
    REQUIRE_FALSE( datasetId.empty() );

    // stats on the created id: shared store truth, sample_count shape.
    const Json::Value stats = runForEnvelope(
        "dataset stats --dataset-db " + db + " --dataset " + datasetId + " --json", 0 );
    REQUIRE( stats["data"].isMember( "sample_count" ) );

    // Unknown version id -> MissingDependency(5), matching the not-found
    // semantics of the MCP dataset:version tool.
    runForEnvelope( "dataset --dataset-db " + db +
                        " inspect --version 00000000-0000-0000-0000-000000000000 --json",
                    5 );
}

TEST_CASE( "parity 5-6: workflow documents validate as schema failures",
           "[parity][r4]" )
{
    TempDir dir( "parity_wf" );
    const std::string bad = dir.file( "bad.json" );
    writeFile( bad, "{\"not\":\"a workflow\"}" );
    // MCP run_workflow rejects a bad pipeline with INVALID_PIPELINE; the CLI
    // validate path answers ValidationFailure(2) on the same document class.
    runForEnvelope( "workflow validate " + bad + " --json", 2 );
}

TEST_CASE( "parity 15: session verb vocabulary is the OpsDriver action set",
           "[parity][r4][session]" )
{
    // scientific:agent_session forwards `action` to the same OpsDriver; the
    // CLI rejects unknown actions with the full accepted verb list.
    const auto result = runCli( "session __not_an_action_parity__ --json" );
    REQUIRE( result.exitCode == 6 );
    for ( const char *action : { "run", "resume", "reconcile", "status", "timeline",
                                 "export", "pause", "cancel", "clear-pause",
                                 "clear-cancel", "approve-repair", "actions" } )
    {
        INFO( "session usage must enumerate " << action );
        REQUIRE( result.output.find( action ) != std::string::npos );
    }
}

TEST_CASE( "parity 16: passport names its input, invalid input is 6",
           "[parity][r4]" )
{
    runForEnvelope( "passport /nonexistent/sicnu-r4/passport.json --json", 6 );
}
