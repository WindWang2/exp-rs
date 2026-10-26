// tests/test_r23_e_plugin_typed_gates.cpp — R23-E WP1 regression guards for
// the #1662 sweep typed-gate fixes (#1551/#1592, commits 77d70df9 area).
//
// The out-of-process worker answers hostile-typed frames (plugin.load with a
// non-object "services", a non-string services member, a non-string
// pluginDirectory, a non-object manifest; execution requests with non-string
// operatorId/toolId/providerId/uri/model fields) with structured E1002/E6002
// refusals. Before those gates a Json::LogicError escaped the load handler
// into std::terminate (#1551) or was swallowed by the pool loop leaving the
// request unanswered — the host could only time out and burn a restart
// (#1592). These cases drive the REAL exprs_plugin_host_worker binary over
// the REAL session channel and pin all three contract axes:
//   1. the refusal is typed (Status::Error + code, not Timeout/ChannelClosed)
//   2. the worker SURVIVES the hostile frame (isAlive, no restart burned)
//   3. the same worker then serves a valid load/execute (nothing wedged)
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_host_runtime.h"
#include "exprs/plugin_interface.h"
#include "exprs/plugin_quotas.h"
#include "exprs/version.h"

#include "plugins/host/plugin_host_process_runtime.h"
#include "plugins/host/plugin_host_protocol.h"
#include "plugins/host/plugin_host_session.h"
#include "support/exprs_test_env.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <json/json.h>
#include <memory>
#include <string>
#include <vector>

using namespace exprs;
using namespace sicnu::plugins::hostprotocol;
using sicnu::plugins::PluginHostProcessSession;

namespace {

/// Per-process copy of the built isolation fixture (same discipline as
/// test_plugin_host_process.cpp: rewriting plugin.json in the shared
/// build-tree dir races parallel ctest processes, issue #1364).
const exprs_test::ScratchGuard kFixtureScratch{ "r23_e_typed_gates_fixture" };

const char *kWorkerPath = SICNU_TEST_PLUGIN_HOST_WORKER;
const char *kPluginId = "org.exprs.test.isolation-plugin";

#ifdef _WIN32
const char *kEntrypoint = "libisolation_plugin.dll";
#elif defined( __APPLE__ )
const char *kEntrypoint = "libisolation_plugin.dylib";
#else
const char *kEntrypoint = "libisolation_plugin.so";
#endif

std::string fixtureDir()
{
    return kFixtureScratch.path + "/isolation_plugin";
}

/// Manifest declaring exactly the fixture surfaces these cases drive: the
/// echo operator (valid round-trip), the isodb:// store (provider uri gate)
/// and the identity model runtime (model request gates). Same field shape
/// the sibling suite writes (proven against PluginManifest::fromJson).
Json::Value manifestJson()
{
    Json::Value manifest( Json::objectValue );
    manifest["manifest_version"] = 1;
    manifest["id"] = kPluginId;
    manifest["name"] = "Isolation Test Plugin";
    manifest["version"] = "1.0.0";
    manifest["api_version"] = EXP_RS_PLUGIN_API_VERSION;
    manifest["abi_version"] = pluginAbiVersion();
    manifest["runtime"] = "host-process";
    manifest["entrypoint"] = kEntrypoint;
    manifest["entrypoint_kind"] = "native";
    manifest["capabilities"] = Json::Value( Json::arrayValue );
    for ( const char *kind : { "operator", "data_provider", "model_runtime", "agent_tool" } )
        manifest["capabilities"].append( kind );

    Json::Value operators( Json::arrayValue );
    Json::Value echoOperator( Json::objectValue );
    echoOperator["id"] = "test:iso-echo";
    echoOperator["display_name"] = "test:iso-echo";
    echoOperator["group"] = "test";
    echoOperator["description"] = "isolation fixture operator";
    operators.append( echoOperator );
    manifest["operators"] = operators;

    Json::Value providers( Json::arrayValue );
    Json::Value provider( Json::objectValue );
    provider["id"] = "test:iso-store";
    provider["display_name"] = "Isolation Store";
    provider["description"] = "in-memory fixture store";
    Json::Value schemes( Json::arrayValue );
    schemes.append( "isodb://" );
    provider["schemes"] = schemes;
    providers.append( provider );
    manifest["data_providers"] = providers;

    Json::Value runtimes( Json::arrayValue );
    Json::Value runtime( Json::objectValue );
    runtime["framework"] = "iso-identity";
    runtime["display_name"] = "Isolation Identity";
    runtime["description"] = "identity tensor backend (known-answer)";
    runtime["gpu"] = false;
    runtimes.append( runtime );
    manifest["model_runtimes"] = runtimes;

    Json::Value tools( Json::arrayValue );
    Json::Value tool( Json::objectValue );
    tool["id"] = "test:iso-tool";
    tool["display_name"] = "Isolation Tool";
    tool["category"] = "test";
    tool["description"] = "echo fixture tool";
    Json::Value inputSchema( Json::objectValue );
    inputSchema["type"] = "object";
    tool["input_schema"] = inputSchema;
    tools.append( tool );
    manifest["agent_tools"] = tools;
    return manifest;
}

/// Copies the built fixture and materializes plugin.json next to the binary
/// (the worker-side loader resolves entrypoint + manifest from this tree).
void prepareFixture()
{
    const std::string dir = fixtureDir();
    std::error_code ec;
    std::filesystem::create_directories( dir );
    std::filesystem::copy( SICNU_TEST_ISOLATION_PLUGIN_DIR, dir,
                           std::filesystem::copy_options::recursive
                             | std::filesystem::copy_options::overwrite_existing,
                           ec );
    if ( ec )
    {
        std::fprintf( stderr, "cannot copy the isolation fixture into %s: %s\n", dir.c_str(),
                      ec.message().c_str() );
        std::abort();
    }
    std::ofstream output( dir + "/plugin.json", std::ios::trunc );
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
    writer->write( manifestJson(), &output );
    output << "\n";
}

/// A valid plugin.load frame (same shape PluginHostProcessRuntime builds).
Json::Value validLoadParams()
{
    Json::Value params( Json::objectValue );
    params["entrypoint"] = kEntrypoint;
    params["pluginDirectory"] = fixtureDir();
    params["manifest"] = manifestJson();
    Json::Value serviceValues( Json::objectValue );
    serviceValues["tempDirectory"] = kFixtureScratch.path;
    serviceValues["workspaceRoot"] = kFixtureScratch.path;
    serviceValues["dataDirectory"] = kFixtureScratch.path;
    serviceValues["pluginDirectory"] = fixtureDir();
    params["services"] = serviceValues;
    return params;
}

/// Spawns a session against the real worker binary with diagnostics on
/// failure (test visibility, same pattern as loadOrExplain).
std::shared_ptr<PluginHostProcessSession> spawnSession()
{
    PluginHostProcessSession::SpawnOptions options;
    options.workerPath = kWorkerPath;
    options.pluginId = kPluginId;
    options.pluginDirectory = fixtureDir();
    options.quota = PluginQuota::fromEnvironment();
    options.handshakeTimeoutMs = 15000;
    options.killGraceMs = 3000;
    PluginDiagnosticLog diagnostics;
    auto session = PluginHostProcessSession::spawn( options, diagnostics );
    if ( !session )
        for ( const auto &d : diagnostics.items() )
            WARN( PluginDiagnostic::codeString( d.code ) << ": " << d.message );
    return session;
}

/// Healthy echo round-trip over the data plane; REQUIREs inside so hostile
/// cases can call it directly as their "still alive" proof.
void requireEchoWorks( PluginHostProcessSession &session, const std::string &message )
{
    Json::Value params( Json::objectValue );
    params["operatorId"] = "test:iso-echo";
    params["params"] = Json::Value( Json::objectValue );
    params["params"]["message"] = message;
    const IpcChannel::Outcome outcome = session.request( kExecuteOperator, params, 8000 );
    INFO( "echo outcome: " << outcome.statusCode() );
    REQUIRE( outcome.status == IpcChannel::Outcome::Status::Ok );
    REQUIRE( outcome.result["success"].asBool() );
    REQUIRE( outcome.result["result"]["echo"]["message"].asString() == message );
}

} // namespace

TEST_CASE( "hostile plugin.load payload types are refused typed and the worker survives",
           "[r23e][hostprocess][typedgates]" )
{
    prepareFixture();

    struct HostileVariant
    {
        const char *name;
        std::function<void( Json::Value & )> mutate;
    };
    const std::vector<HostileVariant> variants = {
        { "services is an array",
          []( Json::Value &p ) {
              p["services"] = Json::Value( Json::arrayValue );
              p["services"].append( "boom" );
          } },
        { "services is a string", []( Json::Value &p ) { p["services"] = "boom"; } },
        { "services member is an int",
          []( Json::Value &p ) { p["services"]["tempDirectory"] = 42; } },
        { "services member is an array",
          []( Json::Value &p ) { p["services"]["workspaceRoot"] = Json::Value( Json::arrayValue ); } },
        { "pluginDirectory is a number", []( Json::Value &p ) { p["pluginDirectory"] = 7; } },
        { "manifest is a string", []( Json::Value &p ) { p["manifest"] = "not-a-manifest"; } },
        { "manifest is an array", []( Json::Value &p ) {
             p["manifest"] = Json::Value( Json::arrayValue );
             p["manifest"].append( 1 );
         } },
    };

    for ( const HostileVariant &variant : variants )
    {
        INFO( "variant: " << variant.name );
        auto session = spawnSession();
        REQUIRE( session );
        REQUIRE( session->isAlive() );

        Json::Value hostile = validLoadParams();
        variant.mutate( hostile );
        const IpcChannel::Outcome refused =
            session->requestControlRaw( kLoadPlugin, hostile, 10000 );
        INFO( "refusal: " << refused.statusCode() << " "
                          << refused.error.code << " " << refused.error.message );
        // Axis 1 — typed refusal, not a timeout (unanswered request) and not
        // a channel close (worker terminated): exactly the #1551/#1592
        // failure modes the gates retired.
        REQUIRE( refused.status == IpcChannel::Outcome::Status::Error );
        REQUIRE( refused.error.code == "E1002" );
        REQUIRE_FALSE( refused.error.message.empty() );
        // Axis 2 — the worker survived the hostile frame.
        REQUIRE( session->isAlive() );
        // Axis 3 — the same worker then loads the valid frame and serves the
        // data plane: the refusal wedged nothing.
        const IpcChannel::Outcome validLoad =
            session->requestControlRaw( kLoadPlugin, validLoadParams(), 15000 );
        INFO( "valid load after refusal: " << validLoad.statusCode() );
        REQUIRE( validLoad.status == IpcChannel::Outcome::Status::Ok );
        REQUIRE( validLoad.result["registered"]["operators"].size() >= 1 );
        requireEchoWorks( *session, "after-hostile-load" );

        PluginDiagnosticLog shutdownDiagnostics;
        REQUIRE( session->shutdown( 5000, shutdownDiagnostics ) );
    }
}

TEST_CASE( "hostile execution-plane param types are refused typed",
           "[r23e][hostprocess][typedgates]" )
{
    prepareFixture();

    auto session = spawnSession();
    REQUIRE( session );
    const IpcChannel::Outcome load =
        session->requestControlRaw( kLoadPlugin, validLoadParams(), 15000 );
    REQUIRE( load.status == IpcChannel::Outcome::Status::Ok );

    // Each hostile frame must answer Error/E6002 and leave the worker alive;
    // a missing gate would surface as Timeout (swallowed Json::LogicError on
    // the pool thread, request never answered) or ChannelClosed (dead
    // worker) — the #1592 failure mode.
    struct HostileExecution
    {
        const char *name;
        const char *method;
        Json::Value params;
    };
    std::vector<HostileExecution> frames;
    {
        Json::Value operatorIdTyped( Json::objectValue );
        operatorIdTyped["operatorId"] = 17;
        frames.push_back( { "operatorId is a number", kExecuteOperator, operatorIdTyped } );
    }
    {
        Json::Value workDirTyped( Json::objectValue );
        workDirTyped["operatorId"] = "test:iso-echo";
        workDirTyped["workDir"] = Json::Value( Json::arrayValue );
        frames.push_back( { "workDir is an array", kExecuteOperator, workDirTyped } );
    }
    {
        Json::Value toolIdTyped( Json::objectValue );
        toolIdTyped["toolId"] = Json::Value( Json::arrayValue );
        frames.push_back( { "toolId is an array", kExecuteAgentTool, toolIdTyped } );
    }
    {
        Json::Value providerUriTyped( Json::objectValue );
        providerUriTyped["providerId"] = "test:iso-store";
        providerUriTyped["uri"] = 5;
        frames.push_back( { "provider uri is a number", kInspectData, providerUriTyped } );
    }
    {
        Json::Value modelRequestTyped( Json::objectValue );
        modelRequestTyped["framework"] = "iso-identity";
        modelRequestTyped["request"] = "not-an-object";
        frames.push_back( { "model request is a string", kLoadModel, modelRequestTyped } );
    }
    {
        Json::Value modelNameTyped( Json::objectValue );
        modelNameTyped["framework"] = "iso-identity";
        modelNameTyped["request"] = Json::Value( Json::objectValue );
        modelNameTyped["request"]["modelName"] = 9;
        frames.push_back( { "model request modelName is a number", kLoadModel, modelNameTyped } );
    }

    for ( const HostileExecution &frame : frames )
    {
        INFO( "frame: " << frame.name );
        const IpcChannel::Outcome refused = session->request( frame.method, frame.params, 8000 );
        INFO( "refusal: " << refused.statusCode() << " "
                          << refused.error.code << " " << refused.error.message );
        REQUIRE( refused.status == IpcChannel::Outcome::Status::Error );
        REQUIRE( refused.error.code == "E6002" );
        REQUIRE_FALSE( refused.error.message.empty() );
        REQUIRE( session->isAlive() );
    }

    // A malformed inference tensor answers a TYPED RESULT envelope (Ok on
    // the channel with success=false) rather than killing either side — the
    // parseTensor try/catch contract.
    {
        Json::Value malformedTensor( Json::objectValue );
        malformedTensor["framework"] = "iso-identity";
        Json::Value input( Json::objectValue );
        input["batch"] = 1;
        input["channels"] = 1;
        input["rows"] = 1;
        input["cols"] = 1;
        input["data"] = Json::Value( Json::arrayValue );
        input["data"].append( Json::Value( Json::objectValue ) );
        malformedTensor["input"] = input;
        const IpcChannel::Outcome outcome =
            session->request( kInferModel, malformedTensor, 8000 );
        INFO( "malformed tensor outcome: " << outcome.statusCode() );
        REQUIRE( outcome.status == IpcChannel::Outcome::Status::Ok );
        REQUIRE_FALSE( outcome.result["success"].asBool() );
        REQUIRE( outcome.result["error"].asString().find( "malformed inference request" )
                 != std::string::npos );
        REQUIRE( session->isAlive() );
    }

    // The data plane still serves valid traffic after the whole barrage.
    requireEchoWorks( *session, "after-execution-barrage" );

    PluginDiagnosticLog shutdownDiagnostics;
    REQUIRE( session->shutdown( 5000, shutdownDiagnostics ) );
}
