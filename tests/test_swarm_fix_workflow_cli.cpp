// tests/test_swarm_fix_workflow_cli.cpp
// swarm-review WFC regression tests (2026-11-04) — workflow engine + CLI lane:
//   WFC-1  workflowDefinitionFromJson refuses a present, non-null params/
//          arguments member that is not an object (fail-closed like the
//          gates/inputs gates) instead of silently substituting an empty
//          object and running the step on DEFAULT parameters — the MCP
//          run_workflow face has no type gate in front of that parser.
//   WFC-2  ONE pipeline timeout policy (workflow/pipeline_timeout.h):
//          SICNU_PIPELINE_TIMEOUT_SEC unset/invalid -> 30 min, "0" ->
//          unlimited, N -> N seconds; the CLI runner's two deadline loops
//          and the runtime's ExecutionPlane await all resolve it there.
//   WFC-6  a dotless ${name} is an environment variable ONLY when the name is
//          all uppercase/digits/underscores; ${env.NAME} is an env var for
//          any name; a lowercase name is a STEP reference. The CLI pipeline
//          runner's pre-expansion applies exactly this predicate, so one
//          document resolves identically on both paths.
//   WFC-3  every `data` branch refuses a `--`-shaped leftover (the
//          rejectUnknownCliFlag gate pipeline/workflow/catalog already ran),
//          and a trailing --hash-budget is a typed error, not a silent drop.
//   WFC-4  lab's batch-summary flag is --summary-json; the boolean --json is
//          the global envelope again (no more same-name collision).
//   WFC-5  dataset/experiment/reproduce value flags never swallow a global
//          flag as their value and never vanish in last position.
//   WFC-7  --no-execution-cache is stripped before routing, so CLI 3.0
//          subcommands honor it (it used to be legacy-parser-only).
//   WFC-8  workflow run accepts the ENGINE input shape
//          ({fromStepId, fromPort, toPort}) that validate reads, mapping it
//          to the params placeholder the runner resolves.
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "workflow/pipeline_timeout.h"
#include "workflow/placeholder_grammar.h"
#include "workflow/workflow_definition.h"

#include <chrono>
#include <cstdlib>
#include <string>

// Portable env mutation for the WFC-2 policy test (same pattern as
// test_placeholder_grammar.cpp).
#ifdef _WIN32
static void portableSetenv( const char *key, const char *value )
{
    _putenv( ( std::string( key ) + "=" + value ).c_str() );
}
static void portableUnsetenv( const char *key )
{
    _putenv( ( std::string( key ) + "=" ).c_str() );
}
#else
static void portableSetenv( const char *key, const char *value )
{
    ::setenv( key, value, 1 );
}
static void portableUnsetenv( const char *key )
{
    ::unsetenv( key );
}
#endif

using sicnu::workflow::WorkflowDefinition;

namespace {

// ---------------------------------------------------------------------------
// In-process: WFC-1 — non-object params/arguments are refused, not defaulted.
// ---------------------------------------------------------------------------

Json::Value engineDoc( const Json::Value &member, const char *key )
{
    Json::Value step( Json::objectValue );
    step["id"] = "s1";
    step["operator"] = "rs:band_math";
    if ( !member.isNull() )
        step[key] = member;
    Json::Value steps( Json::arrayValue );
    steps.append( step );
    Json::Value doc( Json::objectValue );
    doc["id"] = "wfc1";
    doc["steps"] = steps;
    return doc;
}

bool parses( const Json::Value &doc, std::string &error )
{
    WorkflowDefinition def;
    error.clear();
    return sicnu::workflow::workflowDefinitionFromJson( doc, def, error );
}

// ---------------------------------------------------------------------------
// In-process: WFC-2 — the one timeout policy.
// ---------------------------------------------------------------------------

class ScopedEnv
{
public:
    explicit ScopedEnv( const char *name, const char *value )
        : m_name( name ), m_had( std::getenv( name ) != nullptr )
    {
        if ( m_had )
            m_old = std::getenv( name );
        if ( value )
            portableSetenv( name, value );
        else
            portableUnsetenv( name );
    }
    ~ScopedEnv()
    {
        if ( m_had )
            portableSetenv( m_name.c_str(), m_old.c_str() );
        else
            portableUnsetenv( m_name.c_str() );
    }
private:
    std::string m_name;
    std::string m_old;
    bool m_had;
};

const auto kDefaultTimeout = std::chrono::minutes( 30 );
const auto kUnlimitedFloor = std::chrono::hours( 24 * 365 );

} // namespace

TEST_CASE( "WFC-1: non-object params/arguments are refused, not defaulted",
           "[swarm][wfc1][workflow]" )
{
    const struct { const char *what; Json::Value value; const char *key; } bad[] = {
        { "params array", Json::Value( Json::arrayValue ), "params" },
        { "params string", Json::Value( "x" ), "params" },
        { "params number", Json::Value( 5 ), "params" },
        { "params bool", Json::Value( true ), "params" },
        { "arguments array", Json::Value( Json::arrayValue ), "arguments" },
        { "arguments string", Json::Value( "x" ), "arguments" },
    };
    for ( const auto &c : bad )
    {
        INFO( c.what );
        std::string error;
        REQUIRE_FALSE( parses( engineDoc( c.value, c.key ), error ) );
        // The message names the step AND the offending member.
        REQUIRE( error.find( c.key ) != std::string::npos );
        REQUIRE( error.find( "s1" ) != std::string::npos );
    }

    // Positive controls: absent / null / well-formed keep parsing.
    for ( const Json::Value ok : { engineDoc( Json::Value( Json::nullValue ), "params" ),
                                   engineDoc( Json::Value( Json::objectValue ), "params" ),
                                   engineDoc( Json::Value( Json::objectValue ), "arguments" ) } )
    {
        std::string error;
        REQUIRE( parses( ok, error ) );
    }
    {
        // null params with a well-formed arguments fallback still parses.
        Json::Value doc = engineDoc( Json::Value( Json::objectValue ), "arguments" );
        doc["steps"][0]["params"] = Json::Value( Json::nullValue );
        std::string error;
        REQUIRE( parses( doc, error ) );
    }
}

TEST_CASE( "WFC-2: SICNU_PIPELINE_TIMEOUT_SEC resolves the one timeout policy",
           "[swarm][wfc2][workflow]" )
{
    {
        const ScopedEnv unset( "SICNU_PIPELINE_TIMEOUT_SEC", nullptr );
        CHECK( sicnu::workflow::pipelineTimeoutMs() == kDefaultTimeout );
    }
    for ( const char *invalid : { "", "abc", "-5", "30x" } )
    {
        const ScopedEnv env( "SICNU_PIPELINE_TIMEOUT_SEC", invalid );
        INFO( invalid );
        CHECK( sicnu::workflow::pipelineTimeoutMs() == kDefaultTimeout );
    }
    {
        const ScopedEnv env( "SICNU_PIPELINE_TIMEOUT_SEC", "0" );
        CHECK( sicnu::workflow::pipelineTimeoutMs() >= kUnlimitedFloor );
    }
    {
        // At/above one year is "unlimited" too — and a digit bomb must not
        // overflow the seconds→milliseconds widening.
        const ScopedEnv env( "SICNU_PIPELINE_TIMEOUT_SEC", "99999999999999999999" );
        CHECK( sicnu::workflow::pipelineTimeoutMs() >= kUnlimitedFloor );
    }
    {
        const ScopedEnv env( "SICNU_PIPELINE_TIMEOUT_SEC", "7" );
        CHECK( sicnu::workflow::pipelineTimeoutMs() == std::chrono::seconds( 7 ) );
    }
    {
        const ScopedEnv env( "SICNU_PIPELINE_TIMEOUT_SEC", "1800" );
        CHECK( sicnu::workflow::pipelineTimeoutMs() == kDefaultTimeout );
    }
}

TEST_CASE( "WFC-6: the grammar classifies dotless ${...} by case, like the CLI",
           "[swarm][wfc6][workflow]" )
{
    {
        const auto refs = sicnu::workflow::parsePlaceholders( "${WORK}/out.tif" );
        REQUIRE( refs.size() == 1 );
        CHECK( refs[0].isEnvVar );
        CHECK( refs[0].envVarName == "WORK" );
        CHECK( refs[0].stepId.empty() );
    }
    {
        // A lowercase dotless name is a STEP reference on BOTH paths — the
        // CLI pre-expansion no longer treats it as an env var either.
        const auto refs = sicnu::workflow::parsePlaceholders( "${work}/out.tif" );
        REQUIRE( refs.size() == 1 );
        CHECK_FALSE( refs[0].isEnvVar );
        CHECK( refs[0].stepId == "work" );
        CHECK( refs[0].portName == "output" );
    }
    {
        // ${env.NAME} is an env var for ANY name.
        const auto refs = sicnu::workflow::parsePlaceholders( "${env.work}" );
        REQUIRE( refs.size() == 1 );
        CHECK( refs[0].isEnvVar );
        CHECK( refs[0].envVarName == "work" );
    }
    {
        // inferStepConnections keeps the same vocabulary: an env var is never
        // a DAG edge, a step reference always is.
        CHECK( sicnu::workflow::inferStepConnections( "input", "${work}" ).size() == 1 );
        CHECK( sicnu::workflow::inferStepConnections( "input", "${WORK}" ).empty() );
    }
}

// ---------------------------------------------------------------------------
// CLI-spawn lanes (POSIX popen, same fixture as test_cli_commands_json).
// ---------------------------------------------------------------------------
#if !defined( _WIN32 ) && defined( SICNU_TEST_CLI )

#include "support/exprs_test_env.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

struct RunResult
{
    int exitCode = -1;
    std::string output;
};

RunResult runCli( const std::string &args )
{
    // stderr is merged in: the failure lanes print their four-tuple to
    // stderr when no envelope is requested.
    std::string command = std::string( SICNU_TEST_CLI ) + " " + args + " 2>&1";
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

/// The envelope is the LAST single-line JSON object on the merged stream
/// (indentation is disabled by CliIO::finish, so it never wraps).
Json::Value parseEnvelope( const std::string &text )
{
    std::istringstream stream( text );
    std::string line;
    std::string lastJson;
    while ( std::getline( stream, line ) )
    {
        const std::size_t first = line.find_first_not_of( " \t\r\n" );
        if ( first != std::string::npos && line[first] == '{' )
            lastJson = line;
    }
    Json::Value root;
    if ( !lastJson.empty() )
    {
        Json::Reader reader;
        reader.parse( lastJson, root, false );
    }
    return root;
}

/// Owns a pid-unique scratch DIRECTORY (remove_all also takes the SQLite
/// -journal/-wal sidecars a dataset store leaves behind).
class TempDir
{
public:
    explicit TempDir( const std::string &name )
        : m_path( std::filesystem::temp_directory_path()
                  / ( "swarm_wfc_" + std::to_string( exprs_test::localPid() ) + "_" + name ) )
    {
        std::error_code ec;
        std::filesystem::remove_all( m_path, ec );
        std::filesystem::create_directories( m_path, ec );
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all( m_path, ec ); }
    std::string file( const std::string &name ) const { return ( m_path / name ).string(); }
private:
    std::filesystem::path m_path;
};

void writeText( const std::string &path, const std::string &content )
{
    std::ofstream out( path, std::ios::trunc | std::ios::binary );
    out << content;
}

} // namespace

TEST_CASE( "WFC-3: the data family refuses unknown flags and a trailing value flag",
           "[swarm][wfc3][cli]" )
{
    SECTION( "data inspect refuses a typo'd flag" )
    {
        const auto result = runCli( "data inspect /nonexistent/scene.tif --typo-flag --json" );
        INFO( result.output );
        CHECK( result.exitCode == 6 ); // exprs::ExitCode::InvalidInput
        CHECK( result.output.find( "unknown flag: --typo-flag" ) != std::string::npos );
    }
    SECTION( "data probe refuses a typo'd flag" )
    {
        const auto result = runCli( "data probe /nonexistent/scene.tif --statz --json" );
        INFO( result.output );
        CHECK( result.exitCode == 6 );
        CHECK( result.output.find( "unknown flag: --statz" ) != std::string::npos );
    }
    SECTION( "data cache: a trailing --bytes is a typed error" )
    {
        // The old `i + 1 < size` loop never visited a last-position flag, so
        // this typed refusal was unreachable and the flag vanished.
        const auto result = runCli( "data cache /nonexistent/source.tif --bytes --json" );
        INFO( result.output );
        CHECK( result.exitCode == 6 );
        CHECK( result.output.find( "--bytes" ) != std::string::npos );
    }
    SECTION( "data cache prefetch: a trailing --max-bytes is a typed error" )
    {
        const auto result = runCli( "data cache prefetch /nonexistent/spec.json --max-bytes --json" );
        INFO( result.output );
        CHECK( result.exitCode == 6 );
        CHECK( result.output.find( "--max-bytes needs a value" ) != std::string::npos );
    }
    SECTION( "data product plan: a trailing --hash-budget is a typed error" )
    {
        // Grammar note: the product branch consumes the PATH first and the
        // verb second (data product <path> plan) — the opposite of the order
        // docs/products/cn-satellites.md documents; that mismatch is a
        // separate pre-existing defect, reported to the mainline.
        const auto result =
            runCli( "data product /nonexistent/product plan --hash-budget --json" );
        INFO( result.output );
        CHECK( result.exitCode == 6 );
        CHECK( result.output.find( "--hash-budget" ) != std::string::npos );
    }
    SECTION( "positive control: the documented flags still run" )
    {
        const auto status = runCli( "data cache status --json" );
        INFO( status.output );
        REQUIRE( status.exitCode == 0 );
        CHECK( parseEnvelope( status.output ).get( "command", "" ).asString() == "data" );
    }
}

TEST_CASE( "WFC-4: lab --json is the envelope again; the summary flag is renamed",
           "[swarm][wfc4][cli]" )
{
    SECTION( "the boolean --json no longer eats the next token" )
    {
        // Before the rename --json was lab's summary VALUE flag, so this
        // invocation failed with "requires a value".
        const auto result = runCli( "lab --json --help" );
        INFO( result.output );
        CHECK( result.exitCode == 0 );
    }
    SECTION( "the renamed summary flag still requires a value" )
    {
        const auto result = runCli( "lab --summary-json" );
        INFO( result.output );
        CHECK( result.exitCode == 2 ); // exprs::ExitCode::ValidationFailure
        CHECK( result.output.find( "--summary-json requires a value" ) != std::string::npos );
    }
    SECTION( "the old flag name is now an unknown option" )
    {
        const auto result = runCli( "lab --json summary.json" );
        INFO( result.output );
        CHECK( result.exitCode == 2 );
    }
}

TEST_CASE( "WFC-5: dataset value flags never swallow a global flag",
           "[swarm][wfc5][cli]" )
{
    const TempDir dir( "dataset" );
    const std::string db = dir.file( "store.db" );
    SECTION( "--limit followed by --json is a missing value, not limit=0" )
    {
        const auto result = runCli( "dataset inspect --dataset-db " + db + " --limit --json" );
        INFO( result.output );
        CHECK( result.exitCode == 6 );
        CHECK( result.output.find( "--limit needs a value" ) != std::string::npos );
    }
    SECTION( "a trailing value flag is refused, not dropped" )
    {
        const auto result = runCli( "dataset inspect --dataset-db " + db + " --limit" );
        INFO( result.output );
        CHECK( result.exitCode == 6 );
        CHECK( result.output.find( "--limit needs a value" ) != std::string::npos );
    }
    SECTION( "the experiment family shares the same parser" )
    {
        const auto result = runCli( "experiment inspect --experiment-db " + db + " --run --json" );
        INFO( result.output );
        CHECK( result.exitCode == 6 );
        CHECK( result.output.find( "--run needs a value" ) != std::string::npos );
    }
    SECTION( "positive control: a well-formed value flag still runs" )
    {
        const auto created = runCli( "dataset create --dataset-db " + db + " --name seed --json" );
        INFO( created.output );
        REQUIRE( created.exitCode == 0 );
        const auto listed = runCli( "dataset inspect --dataset-db " + db + " --limit 5 --json" );
        INFO( listed.output );
        REQUIRE( listed.exitCode == 0 );
        CHECK( parseEnvelope( listed.output ).get( "command", "" ).asString() == "dataset" );
    }
}

TEST_CASE( "WFC-7: --no-execution-cache works under CLI 3.0 subcommands",
           "[swarm][wfc7][cli]" )
{
    // The flag used to be parsed only by the legacy QCommandLineParser, so
    // `--no-execution-cache data cache status` never reached the subcommand
    // (it fell through to the legacy "No pipeline specified" help, exit 1).
    const auto result = runCli( "--no-execution-cache data cache status --json" );
    INFO( result.output );
    CHECK( result.exitCode == 0 );
    CHECK( parseEnvelope( result.output ).get( "command", "" ).asString() == "data" );
    // A trailing position works too (stripping happens before routing).
    const auto trailing = runCli( "data cache status --json --no-execution-cache" );
    INFO( trailing.output );
    CHECK( trailing.exitCode == 0 );
}

TEST_CASE( "WFC-8: workflow run reads the engine input shape validate reads",
           "[swarm][wfc8][cli]" )
{
    const TempDir dir( "wfc8" );
    const std::string path = dir.file( "engine_inputs.json" );
    const std::string output = dir.file( "wfc8_out.tif" );
    writeText( path,
               R"({"schema_version": 1, "id": "wfc8", "title": "wfc8", "steps": [)"
               R"({"id": "consumer", "operator": "rs:band_math",)"
               R"( "params": {"output": ")" + output + R"(", "expression": "b1"},)"
               R"( "inputs": [{"fromStepId": "ghost", "fromPort": "output", "toPort": "input"}]}]})" );

    SECTION( "validate accepts the engine shape" )
    {
        const auto result = runCli( "workflow validate " + path );
        INFO( result.output );
        CHECK( result.exitCode == 0 );
    }
    SECTION( "run maps the engine wiring into params instead of dropping it" )
    {
        const auto result = runCli( "workflow run " + path );
        INFO( result.output );
        // The consumer cannot resolve a producer that does not exist, so the
        // run fails — but the failure must name the placeholder the wiring
        // produced. Before the fix the wiring was dropped and the operator
        // failed with "Missing required string parameter: input".
        CHECK( result.exitCode == 3 ); // exprs::ExitCode::ExecutionFailure
        CHECK( result.output.find( "${ghost.output}" ) != std::string::npos );
        CHECK( result.output.find( "Missing required string parameter: input" )
               == std::string::npos );
    }
}

#elif !defined( _WIN32 )

TEST_CASE( "WFC-3/4/5/7/8 CLI lanes need SICNU_TEST_CLI", "[swarm][cli]" )
{
    SKIP( "SICNU_TEST_CLI is not defined for this target — register "
          "test_swarm_fix_workflow_cli with "
          "target_compile_definitions(... SICNU_TEST_CLI=\"$<TARGET_FILE:sicnu_geo_rs_cli>\") "
          "(POSIX only, like test_cli_commands_json)" );
}

#endif // !_WIN32 && SICNU_TEST_CLI
