// tests/test_plugin_host_process.cpp — out-of-process native plugin host
// (isolation runtime 5.0, M4/M5). Drives the SDK registry seam against the
// REAL exprs_plugin_host_worker binary and the isolation_plugin fixture:
// launch + handshake, execute over proxies, crash → typed failure → bounded
// recovery, hang → kill ladder, flood → frame cap, unload round-trip.
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_host_runtime.h"
#include "exprs/plugin_interface.h"
#include "exprs/plugin_registry.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include "plugins/host/plugin_host_process_runtime.h"

#include <atomic>
#include <chrono>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <filesystem>
#include <fstream>
#include <json/json.h>
#include <string>
#include <thread>

using namespace exprs;

namespace {

const char *kFixtureDir = SICNU_TEST_ISOLATION_PLUGIN_DIR;
const char *kWorkerPath = SICNU_TEST_PLUGIN_HOST_WORKER;
const char *kPluginId = "org.exprs.test.isolation-plugin";

#ifdef _WIN32
const char *kEntrypoint = "libisolation_plugin.dll";
#elif defined( __APPLE__ )
const char *kEntrypoint = "libisolation_plugin.dylib";
#else
const char *kEntrypoint = "libisolation_plugin.so";
#endif

/// Minimal sink: collects operator proxy factories (all the test needs);
/// provider/tool/model registrations are accepted no-op.
class TestSink : public PluginContributionSink
{
public:
    std::map<std::string, std::function<std::unique_ptr<sicnu::operators::RSOperator>()>>
        operators;

    bool registerOperatorFactory( const std::string &, const std::string &operatorId,
                                  std::function<std::unique_ptr<sicnu::operators::RSOperator>()>
                                      factory ) override
    {
        operators[operatorId] = std::move( factory );
        return true;
    }
    void revokePlugin( const std::string & ) override { operators.clear(); }
    bool registerDataProvider( const std::string &, const std::string &,
                               std::shared_ptr<IPluginDataProviderV1> ) override
    {
        return true;
    }
    bool registerModelRuntime( const std::string &, const std::string &,
                               PluginModelRuntimeFactoryV1 ) override
    {
        return true;
    }
    bool registerAgentTool( const std::string &, const std::string &,
                            std::shared_ptr<IPluginAgentToolV1> ) override
    {
        return true;
    }
};

/// Writes plugin.json (runtime: host-process) next to the built fixture.
void writeManifest()
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
    manifest["capabilities"].append( "operator" );
    Json::Value operators( Json::arrayValue );
    for ( const char *id : { "test:iso-echo", "test:iso-crash", "test:iso-hang",
                             "test:iso-flood", "test:iso-slow" } )
    {
        Json::Value op( Json::objectValue );
        op["id"] = id;
        op["display_name"] = id;
        op["group"] = "test";
        op["description"] = "isolation fixture operator";
        operators.append( op );
    }
    manifest["operators"] = operators;

    std::ofstream output( std::string( kFixtureDir ) + "/plugin.json", std::ios::trunc );
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
    writer->write( manifest, &output );
    output << "\n";
}

/// One registry + runtime + sink stack wired the way the framework does.
struct Stack
{
    TestSink sink;
    std::unique_ptr<sicnu::plugins::PluginHostProcessRuntime> runtime;
    std::string tempDir;

    Stack()
    {
        const int pid =
#ifdef _WIN32
            ::_getpid();
#else
            static_cast<int>( ::getpid() );
#endif
        tempDir = ( std::filesystem::temp_directory_path()
                    / ( "sicnu-iso-" + std::to_string( pid ) ) )
                      .generic_string();
        std::filesystem::create_directories( tempDir );
        writeManifest();

        sicnu::plugins::PluginHostProcessRuntime::Options options;
        options.workerPath = kWorkerPath;
        options.handshakeTimeoutMs = 15000;
        options.quotaCeilings = PluginQuota::fromEnvironment();
        options.quotaCeilings.requestDeadlineMs = 6000; // bounded test budgets
        runtime = std::make_unique<sicnu::plugins::PluginHostProcessRuntime>( options );

        auto &registry = PluginRegistry::instance();
        PluginRegistryOptions registryOptions;
        registryOptions.roots = { std::filesystem::path( kFixtureDir ).parent_path().generic_string() };
        registryOptions.tempDirectory = tempDir;
        registryOptions.hostProcessWorkerPath = kWorkerPath;
        registry.setContributionSink( &sink );
        registry.setHostProcessRuntime( runtime.get() );
        registry.configure( registryOptions );
        // The enable/disable index PERSISTS across runs (plugins.index.json);
        // a previous run's cleanup may have disabled the fixture — clear it
        // for this process and restore at teardown (conformance-kit rule).
        mSavedDisabled = registry.userDisabledIds();
        registry.setUserDisabledIds( {} );
    }

    std::vector<std::string> mSavedDisabled;

    ~Stack()
    {
        // The registry is a process-wide singleton: detach THIS test's
        // runtime/sink (and unload hosted workers) so the next Stack starts
        // from clean state with no dangling pointers.
        auto &registry = PluginRegistry::instance();
        registry.unloadAll();
        registry.setHostProcessRuntime( nullptr );
        registry.setContributionSink( nullptr );
        registry.setUserDisabledIds( mSavedDisabled );
        std::error_code ec;
        std::filesystem::remove_all( tempDir, ec );
    }
};

/// Registry.load with diagnostics on failure (test visibility).
bool loadOrExplain( const char *pluginId )
{
    const bool ok = PluginRegistry::instance().load( pluginId );
    if ( !ok )
    {
        for ( const auto &d : PluginRegistry::instance().diagnostics().items() )
            WARN( PluginDiagnostic::codeString( d.code ) << ": " << d.message );
    }
    return ok;
}

Json::Value runOperator( Stack &stack, const char *operatorId, const Json::Value &params,
                         sicnu::operators::RSOperatorContext *customContext = nullptr )
{
    auto factoryIterator = stack.sink.operators.find( operatorId );
    if ( factoryIterator == stack.sink.operators.end() )
    {
        Json::Value failure( Json::objectValue );
        failure["__testError"] = "operator not registered: " + std::string( operatorId );
        return failure;
    }
    auto instance = factoryIterator->second();
    sicnu::operators::RSOperatorContext local;
    sicnu::operators::RSOperatorContext &context = customContext ? *customContext : local;
    Json::Value result;
    try
    {
        result = instance->run( params, context );
    }
    catch ( const sicnu::operators::RSOperatorError &error )
    {
        Json::Value failure( Json::objectValue );
        failure["__operatorError"] = true;
        failure["code"] = std::to_string( static_cast<int>( error.code() ) );
        failure["message"] = error.message();
        failure["details"] = error.details();
        return failure;
    }
    return result;
}

} // namespace

TEST_CASE( "host-process plugin loads and executes over the worker", "[hostprocess]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();

    REQUIRE( loadOrExplain( kPluginId ) );
    REQUIRE( registry.isLoaded( kPluginId ) );
    REQUIRE( stack.runtime->isWorkerAlive( kPluginId ) );

    // Healthy round-trip: params over the channel, result back.
    Json::Value params( Json::objectValue );
    params["message"] = "hello worker";
    Json::Value result = runOperator( stack, "test:iso-echo", params );
    REQUIRE( result["success"].asBool() );
    REQUIRE( result["echo"]["message"].asString() == "hello worker" );
    REQUIRE( result["worker"].asString() == "isolation_plugin" );

    // Unregistered method refusal.
    Json::Value missing = runOperator( stack, "test:no-such-operator", params );
    REQUIRE( missing.isMember( "__testError" ) );

    REQUIRE( registry.unload( kPluginId ) );
    REQUIRE_FALSE( registry.isLoaded( kPluginId ) );
    REQUIRE_FALSE( stack.runtime->isWorkerAlive( kPluginId ) );
}

TEST_CASE( "plugin crash leaves the host alive and recovery is bounded",
           "[hostprocess][crash]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    // First crash: the proxy respawns the worker once and retries — the
    // fixture crashes AGAIN, so the second attempt fails typed.
    Json::Value result = runOperator( stack, "test:iso-crash", Json::Value() );
    INFO( "crash result: "
          << Json::writeString( Json::StreamWriterBuilder(), result ) );
    REQUIRE( result["__operatorError"].asBool() );
    // Typed failure after ONE bounded recovery attempt (the fixture crashes
    // again on retry): ErrorCode::NotInitialized (4002) — the E6005 path.
    REQUIRE( result["code"].asString() == "4002" );

    // The host registry is fully functional; a reload brings a fresh worker.
    REQUIRE( registry.unload( kPluginId ) );
    REQUIRE( loadOrExplain( kPluginId ) );
    REQUIRE( stack.runtime->isWorkerAlive( kPluginId ) );
    Json::Value params( Json::objectValue );
    params["after"] = "crash";
    Json::Value echo = runOperator( stack, "test:iso-echo", params );
    REQUIRE( echo["success"].asBool() );
    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "hung request hits the deadline and the kill ladder", "[hostprocess][hang]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    const auto start = std::chrono::steady_clock::now();
    Json::Value result = runOperator( stack, "test:iso-hang", Json::Value() );
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE( result["__operatorError"].asBool() );
    REQUIRE( result["code"].asString() == "4100" ); // ExternalProcessTimeout (E6004 path)
    // Deadline (quota ceiling 6 s) + kill-ladder grace — bounded.
    REQUIRE( elapsed < std::chrono::seconds( 12 ) );

    // Recovery: next call respawns and works.
    Json::Value echo = runOperator( stack, "test:iso-echo", Json::Value() );
    REQUIRE( echo["success"].asBool() );
    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "oversized response is refused by the frame cap", "[hostprocess][quota]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    Json::Value result = runOperator( stack, "test:iso-flood", Json::Value() );
    REQUIRE( result["__operatorError"].asBool() );
    // The 64 MiB payload exceeds the negotiated frame cap: the channel dies
    // as a protocol violation, recovery applies once, then the call fails
    // typed (NotInitialized, 4002) — host unharmed throughout.
    REQUIRE( result["code"].asString() == "4002" );

    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "enable/disable round-trip works with the host-process runtime",
           "[hostprocess][lifecycle]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );
    REQUIRE( registry.unload( kPluginId ) );
    // Reload after unload re-launches a fresh worker (generation bump).
    REQUIRE( loadOrExplain( kPluginId ) );
    Json::Value echo = runOperator( stack, "test:iso-echo", Json::Value() );
    REQUIRE( echo["success"].asBool() );
    REQUIRE( registry.unload( kPluginId ) );

    // Disabled plugins refuse to load with the typed disabled diagnostic.
    REQUIRE( registry.setEnabled( kPluginId, false ) );
    REQUIRE_FALSE( registry.load( kPluginId ) );
    REQUIRE( registry.setEnabled( kPluginId, true ) );
}
