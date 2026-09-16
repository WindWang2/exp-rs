// tests/test_cli_batch_manifest.cpp — Surface-11 WP-B: batch manifest runner.
// Known-answer + negative coverage over parse/validation, variable
// interpolation, fail-fast/continue aggregation, cancellation, and the NDJSON
// result index. Execution paths run against unknown operator ids (never touch
// GDAL) and against dry-run so the orchestration contract is hermetic.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <json/json.h>

#include "cli/cli_batch_runner.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

sicnu::cli::batch::Callbacks silentCallbacks()
{
    sicnu::cli::batch::Callbacks callbacks;
    return callbacks;
}

Json::Value parseJsonText( const std::string &text )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( text, root, false );
    return root;
}

Json::Value manifestWithOperators( const std::vector<std::string> &operatorIds )
{
    Json::Value manifest( Json::objectValue );
    manifest["version"] = 1;
    Json::Value tasks( Json::arrayValue );
    int index = 0;
    for ( const std::string &id : operatorIds )
    {
        Json::Value task( Json::objectValue );
        task["id"] = "t" + std::to_string( ++index );
        task["operator"] = id;
        tasks.append( task );
    }
    manifest["tasks"] = tasks;
    return manifest;
}

} // namespace

TEST_CASE( "Manifest parsing accepts the documented forms", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    Json::Value normalized;
    std::string error;

    // Object form.
    const Json::Value objectForm = parseJsonText( R"( {
        "version": 1,
        "variables": { "scene": "A", "year": 2024, "flag": true },
        "policy": { "on_error": "fail-fast" },
        "tasks": [ { "id": "a", "operator": "rs:x", "params": { "p": 1 } },
                   { "id": "b", "operator": "rs:y", "enabled": false } ] } )" );
    REQUIRE( parseManifestDocument( objectForm, normalized, &error ) );
    REQUIRE( normalized["policy"]["on_error"].asString() == "fail-fast" );
    REQUIRE( normalized["variables"]["scene"].asString() == "A" );
    REQUIRE( normalized["variables"]["year"].asString() == "2024" );
    REQUIRE( normalized["variables"]["flag"].asString() == "true" );
    REQUIRE( normalized["tasks"].size() == 2 );

    // Default policy is continue.
    const Json::Value noPolicy = manifestWithOperators( { "rs:x" } );
    REQUIRE( parseManifestDocument( noPolicy, normalized, &error ) );
    REQUIRE( normalized["policy"]["on_error"].asString() == "continue" );

    // Array form (JSONL result).
    Json::Value arrayForm( Json::arrayValue );
    arrayForm.append( parseJsonText( R"( {"id":"a","operator":"rs:x"} )" ) );
    arrayForm.append( parseJsonText( R"( {"id":"b","operator":"rs:y"} )" ) );
    REQUIRE( parseManifestDocument( arrayForm, normalized, &error ) );
    REQUIRE( normalized["tasks"].size() == 2 );
}

TEST_CASE( "Manifest parsing rejects contract violations", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    Json::Value normalized;
    std::string error;

    struct Case
    {
        const char *json;
        const char *expectedIn;
    };
    const std::vector<Case> cases = {
        { R"( {"tasks": "nope"} )", "tasks" },
        { R"( {"version": 2, "tasks": []} )", "version" },
        { R"( {"policy": {"on_error": "explode"}, "tasks": []} )", "on_error" },
        { R"( {"policy": {"typo": true}, "tasks": []} )", "policy key" },
        { R"( {"policy": {"on_error": {"x":1}}, "tasks": []} )", "must be a string" },
        { R"( {"polcy": {"on_error": "fail-fast"}, "tasks": [ {"id":"a","operator":"rs:x"} ]} )", "unknown manifest key" },
        { R"( {"varibles": {"a": 1}, "tasks": [ {"id":"a","operator":"rs:x"} ]} )", "unknown manifest key" },
        { R"( {"variables": "x", "tasks": []} )", "variables" },
        { R"( {"tasks": [ {"id":"a","operator":""} ]} )", "operator" },
        { R"( {"tasks": [ {"operator":"rs:x"} ]} )", "id" },
        { R"( {"tasks": [ {"id":"bad id!","operator":"rs:x"} ]} )", "id" },
        { R"( {"tasks": [ {"id":"a","operator":"rs:x"}, {"id":"a","operator":"rs:y"} ]} )", "duplicate" },
        { R"( {"tasks": [ {"id":"a","operator":"rs:x","params":[1]} ]} )", "params" },
        { R"( {"tasks": [ {"id":"a","operator":"rs:x","mystery":1} ]} )", "unknown key" },
        { R"( {"tasks": [ {"id":"a","operator":"rs:x","enabled":"yes"} ]} )", "enabled" },
    };
    for ( const Case &testCase : cases )
    {
        INFO( testCase.json );
        REQUIRE_FALSE( parseManifestDocument( parseJsonText( testCase.json ), normalized, &error ) );
        REQUIRE( error.find( testCase.expectedIn ) != std::string::npos );
    }
}

TEST_CASE( "JSONL manifests: header, comments, blank lines", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    const std::string path = "/tmp/surface11_batch_test.jsonl";
    {
        std::ofstream out( path, std::ios::binary );
        out << "# leading comment\n"
            << "\n"
            << R"( {"variables": {"scene": "B"}, "policy": {"on_error": "fail-fast"}} )" << "\n"
            << R"( {"id":"t1","operator":"rs:one"} )" << "\n"
            << "\n"
            << R"( {"id":"t2","operator":"rs:two","params":{"in":"${scene}.tif"}} )" << "\n";
    }
    Json::Value normalized;
    std::string error;
    REQUIRE( loadManifestFile( path, normalized, &error ) );
    REQUIRE( normalized["variables"]["scene"].asString() == "B" );
    REQUIRE( normalized["policy"]["on_error"].asString() == "fail-fast" );
    REQUIRE( normalized["tasks"].size() == 2 );
    std::remove( path.c_str() );

    // Malformed line is a hard error naming the file.
    {
        std::ofstream out( path, std::ios::binary );
        out << "{not json}\n";
    }
    REQUIRE_FALSE( loadManifestFile( path, normalized, &error ) );
    REQUIRE_THAT( error, Catch::Matchers::ContainsSubstring( "invalid JSONL" ) );
    std::remove( path.c_str() );

    // Missing file.
    REQUIRE_FALSE( loadManifestFile( "/tmp/surface11_does_not_exist.json", normalized, &error ) );
    REQUIRE_THAT( error, Catch::Matchers::ContainsSubstring( "cannot open" ) );
}

TEST_CASE( "Run: interpolation, unknown operators, exit aggregation (continue)", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    Options options;
    options.extraVariables = { { "scene", "S2A" } };

    const Json::Value manifest = parseJsonText( R"( {
        "version": 1,
        "tasks": [
            { "id": "interp", "operator": "rs:__nope1__",
              "params": { "input": "${scene}.tif", "nested": { "deep": [ "${scene}", 1, null ] } } },
            { "id": "missing_var", "operator": "rs:__nope2__",
              "params": { "input": "${nope}" } },
            { "id": "unknown_op", "operator": "rs:__nope3__" }
        ] } )" );
    Json::Value normalized;
    std::string error;
    REQUIRE( parseManifestDocument( manifest, normalized, &error ) );

    const Outcome outcome = runManifest( normalized, options, silentCallbacks() );
    REQUIRE( outcome.records.size() == 3 );
    // interp: ${scene} RESOLVES via --var, so it reaches the adapter lookup
    // and fails with 5 (unknown operator); missing_var fails on validation
    // (6) before the adapter; unknown_op fails on the adapter (5). Worst
    // exit surfaces.
    REQUIRE( outcome.records[0].status == "failed" );
    REQUIRE( outcome.records[0].exitCode == 5 );
    REQUIRE( outcome.records[1].status == "failed" );
    REQUIRE( outcome.records[1].exitCode == 6 );
    REQUIRE( outcome.records[1].error == "unknown variable ${nope}" );
    REQUIRE( outcome.records[2].status == "failed" );
    REQUIRE( outcome.records[2].exitCode == 5 );
    REQUIRE( outcome.records[2].error == "unknown operator: rs:__nope3__" );
    REQUIRE_FALSE( outcome.cancelled );
    REQUIRE( outcome.exitCode == 6 );
}

TEST_CASE( "Run: fail-fast stops and marks the tail skipped", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    Options options;
    options.failFast = true;
    const Outcome outcome = runManifest(
        manifestWithOperators( { "rs:__nope1__", "rs:__nope2__", "rs:__nope3__" } ),
        options, silentCallbacks() );
    REQUIRE( outcome.records.size() == 3 );
    REQUIRE( outcome.records[0].status == "failed" );
    REQUIRE( outcome.records[0].exitCode == 5 );
    REQUIRE( outcome.records[1].status == "skipped" );
    REQUIRE( outcome.records[1].error == "skipped: fail-fast after failed task" );
    REQUIRE( outcome.records[2].status == "skipped" );
    REQUIRE( outcome.exitCode == 5 );

    // Manifest-level policy reaches the same state without the override.
    Json::Value normalized;
    std::string error;
    REQUIRE( parseManifestDocument(
        parseJsonText( R"( {"policy":{"on_error":"fail-fast"},"tasks":[
            {"id":"a","operator":"rs:__nope__"},{"id":"b","operator":"rs:__nope__"} ]} )" ),
        normalized, &error ) );
    const Outcome byPolicy = runManifest( normalized, Options{}, silentCallbacks() );
    REQUIRE( byPolicy.records.size() == 2 );
    REQUIRE( byPolicy.records[1].status == "skipped" );
}

TEST_CASE( "Run: cancellation yields terminal cancelled/skipped and exit 4", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    Options options;
    Callbacks callbacks = silentCallbacks();
    callbacks.isCancelled = []() { return true; };

    const Outcome outcome = runManifest(
        manifestWithOperators( { "rs:__a__", "rs:__b__" } ), options, callbacks );
    REQUIRE( outcome.cancelled );
    REQUIRE( outcome.records.size() == 2 );
    REQUIRE( outcome.records[0].status == "cancelled" );
    REQUIRE( outcome.records[1].status == "skipped" );
    REQUIRE( outcome.records[1].error == "skipped: cancelled run" );
    REQUIRE( outcome.exitCode == 4 );
}

TEST_CASE( "Run: disabled tasks are skipped without execution", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    const Json::Value manifest = parseJsonText( R"( {
        "tasks": [ {"id":"off","operator":"rs:__nope__","enabled":false},
                   {"id":"on","operator":"rs:__nope2__"} ] } )" );
    Json::Value normalized;
    std::string error;
    REQUIRE( parseManifestDocument( manifest, normalized, &error ) );
    const Outcome outcome = runManifest( normalized, Options{}, silentCallbacks() );
    REQUIRE( outcome.records.size() == 2 );
    REQUIRE( outcome.records[0].status == "skipped" );
    REQUIRE( outcome.records[0].error == "disabled" );
    REQUIRE( outcome.records[1].status == "failed" );
    REQUIRE( outcome.exitCode == 5 );
}

TEST_CASE( "Run: dry-run validates adapters without executing", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    Options options;
    options.dryRun = true;
    const Outcome outcome = runManifest(
        manifestWithOperators( { "rs:__never_registered__" } ), options, silentCallbacks() );
    REQUIRE( outcome.records.size() == 1 );
    REQUIRE( outcome.records[0].status == "failed" );
    REQUIRE( outcome.records[0].exitCode == 5 );
}

TEST_CASE( "Result index: NDJSON written atomically, no tmp residue", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    const std::string index = "/tmp/surface11_result_index.ndjson";
    std::remove( index.c_str() );
    std::remove( ( index + ".tmp" ).c_str() );

    Options options;
    options.resultIndexPath = index;
    const Outcome outcome = runManifest(
        manifestWithOperators( { "rs:__nope1__", "rs:__nope2__" } ), options, silentCallbacks() );
    REQUIRE( outcome.records.size() == 2 );

    std::ifstream in( index, std::ios::binary );
    REQUIRE( in.good() );
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    // Two complete NDJSON lines.
    REQUIRE( std::count( text.begin(), text.end(), '\n' ) == 2 );
    const Json::Value line0 = parseJsonText( text.substr( 0, text.find( '\n' ) ) );
    REQUIRE( line0["id"].asString() == "t1" );
    REQUIRE( line0["status"].asString() == "failed" );
    REQUIRE( line0["exit_code"].asInt() == 5 );
    REQUIRE( line0["index"].asInt() == 0 );

    // No tmp residue (rename consumed it).
    std::ifstream tmp( index + ".tmp", std::ios::binary );
    REQUIRE_FALSE( tmp.good() );
    std::remove( index.c_str() );
}

TEST_CASE( "Errors in the index are redacted", "[cli_batch]" )
{
    using namespace sicnu::cli::batch;

    // The unknown-operator error echoes the operator id back into the
    // record; give the id a credential shape and require the secret to be
    // gone from the index (this FAILS if redaction is removed).
    const Json::Value manifest = parseJsonText( R"( {
        "tasks": [ {"id":"leak","operator":"rs:probe?password=hunter2"} ] } )" );
    Json::Value normalized;
    std::string error;
    REQUIRE( parseManifestDocument( manifest, normalized, &error ) );

    const std::string index = "/tmp/surface11_redact_index.ndjson";
    std::remove( index.c_str() );
    Options options;
    options.resultIndexPath = index;
    const Outcome outcome = runManifest( normalized, options, silentCallbacks() );
    REQUIRE( outcome.records[0].status == "failed" );
    REQUIRE( outcome.records[0].exitCode == 5 );

    std::ifstream in( index, std::ios::binary );
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    REQUIRE_THAT( text, Catch::Matchers::ContainsSubstring( "unknown operator" ) );
    REQUIRE_THAT( text, Catch::Matchers::ContainsSubstring( "[REDACTED]" ) );
    REQUIRE_FALSE( text.find( "hunter2" ) != std::string::npos );
    std::remove( index.c_str() );
}
