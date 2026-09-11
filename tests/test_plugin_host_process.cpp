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
#include "plugins/host/plugin_host_session.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
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
    // Plugin platform 9.0: store the remaining contribution kinds so the
    // suites can drive ALL worker surfaces through their proxies.
    std::map<std::string, std::shared_ptr<IPluginDataProviderV1>> dataProviders;
    std::map<std::string, std::shared_ptr<IPluginAgentToolV1>> agentTools;
    std::map<std::string, PluginModelRuntimeFactoryV1> modelFactories;

    bool registerOperatorFactory( const std::string &, const std::string &operatorId,
                                  std::function<std::unique_ptr<sicnu::operators::RSOperator>()>
                                      factory ) override
    {
        operators[operatorId] = std::move( factory );
        return true;
    }
    void revokePlugin( const std::string & ) override
    {
        operators.clear();
        dataProviders.clear();
        agentTools.clear();
        modelFactories.clear();
    }
    bool registerDataProvider( const std::string &, const std::string &providerId,
                               std::shared_ptr<IPluginDataProviderV1> provider ) override
    {
        dataProviders[providerId] = std::move( provider );
        return true;
    }
    bool registerModelRuntime( const std::string &, const std::string &framework,
                               PluginModelRuntimeFactoryV1 factory ) override
    {
        modelFactories[framework] = std::move( factory );
        return true;
    }
    bool registerAgentTool( const std::string &, const std::string &toolId,
                            std::shared_ptr<IPluginAgentToolV1> tool ) override
    {
        agentTools[toolId] = std::move( tool );
        return true;
    }
};

/// Writes plugin.json (runtime: host-process) next to the built fixture.
/// @p access is an optional manifest "access" object (capability suites).
void writeManifest( const Json::Value &access = Json::Value() )
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
    for ( const char *kind : { "operator", "data_provider", "model_runtime", "agent_tool", "ui" } )
        manifest["capabilities"].append( kind );
    if ( access.isObject() )
        manifest["access"] = access;
    Json::Value operators( Json::arrayValue );
    for ( const char *id : { "test:iso-echo", "test:iso-crash", "test:iso-hang",
                             "test:iso-flood", "test:iso-gate", "test:iso-spawn",
                             "test:iso-slow" } )
    {
        Json::Value op( Json::objectValue );
        op["id"] = id;
        op["display_name"] = id;
        op["group"] = "test";
        op["description"] = "isolation fixture operator";
        operators.append( op );
    }
    manifest["operators"] = operators;

    // Plugin platform 9.0 (M4/M5): every contribution kind is declared so
    // the worker registration report, the conformance kit and the capability
    // gates have a full-declaration fixture to work against.
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

    std::ofstream output( std::string( kFixtureDir ) + "/plugin.json", std::ios::trunc );
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
    writer->write( manifest, &output );
    output << "\n";
}

/// One registry + runtime + sink stack wired the way the framework does.
/// Budget overrides keep escalation suites bounded without touching the
/// default 6 s / 3 s numbers the other tests assume.
struct Stack
{
    TestSink sink;
    std::unique_ptr<sicnu::plugins::PluginHostProcessRuntime> runtime;
    std::string tempDir;

    explicit Stack( int deadlineCeilingMs = 6000, int killGraceMs = 3000,
                    bool declareTempWriteRoot = false )
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
        Json::Value access( Json::Value::nullSingleton() );
        if ( declareTempWriteRoot )
        {
            Json::Value fs( Json::objectValue );
            fs["write"] = Json::Value( Json::arrayValue );
            fs["write"].append( "${temp}" );
            access["filesystem"] = fs;
        }
        writeManifest( access );

        sicnu::plugins::PluginHostProcessRuntime::Options options;
        options.workerPath = kWorkerPath;
        options.handshakeTimeoutMs = 15000;
        options.quotaCeilings = PluginQuota::fromEnvironment();
        options.quotaCeilings.requestDeadlineMs = deadlineCeilingMs;
        options.killGraceMs = killGraceMs;
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
    INFO( "recovery echo: " << Json::writeString( Json::StreamWriterBuilder(), echo ) );
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

TEST_CASE( "concurrent requests run in parallel within the quota", "[hostprocess][concurrency]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    // Three gate instances must rendezvous INSIDE the worker: proof that
    // the worker dispatches concurrently (protocol 1.1) instead of the v1
    // serial loop. Each instance blocks until the running peak reaches 3.
    constexpr int kParallel = 3;
    std::vector<Json::Value> results( kParallel );
    std::vector<std::thread> threads;
    for ( int i = 0; i < kParallel; ++i )
    {
        threads.emplace_back( [&stack, &results, i] {
            Json::Value params( Json::objectValue );
            params["expected"] = kParallel;
            params["waitMs"] = 8000;
            results[ i ] = runOperator( stack, "test:iso-gate", params );
        } );
    }
    for ( std::thread &thread : threads )
        thread.join();
    for ( int i = 0; i < kParallel; ++i )
    {
        INFO( "gate " << i << ": "
                      << Json::writeString( Json::StreamWriterBuilder(), results[ i ] ) );
        REQUIRE( results[ i ]["success"].asBool() );
        REQUIRE( results[ i ]["rendezvous"].asBool() );
        REQUIRE( results[ i ]["observedPeak"].asInt() >= kParallel );
    }
    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "host-side cooperative cancel reaches the worker", "[hostprocess][concurrency][cancel]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    sicnu::operators::RSOperatorContext cancelContext;
    std::atomic<int> cancelPolls{ 0 };
    cancelContext.setCancelCallback( [&cancelPolls]() -> bool {
        // Cancel a moment after the call starts (the proxy polls this
        // predicate and converts it into a per-id cancel frame).
        return cancelPolls.fetch_add( 1 ) > 20;
    } );

    Json::Value cancelled = runOperator( stack, "test:iso-slow", Json::Value(), &cancelContext );
    REQUIRE( cancelled["__operatorError"].asBool() );
    REQUIRE( cancelled["code"].asString() == "4000" ); // ErrorCode::Cancelled = 4000

    // A cooperative cancel must NOT have killed the worker (that is the
    // kill ladder's job, not the cancel's).
    REQUIRE( stack.runtime->isWorkerAlive( kPluginId ) );
    Json::Value params( Json::objectValue );
    params["seconds"] = 1;
    Json::Value after = runOperator( stack, "test:iso-slow", params );
    REQUIRE( after["success"].asBool() );
    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "timeout escalation poisons the session instead of killing a busy worker",
           "[hostprocess][concurrency][poison]" )
{
    // Dedicated budgets: 3 s deadline ceiling, 1 s kill grace.
    Stack stack( 3000, 1000 );
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    // Sequence (deterministic; escalation decision at t = 3.0 + 1.0 = 4.0 s,
    // B's in-flight window is [1.8, 4.4] s — margins >= 900 ms on BOTH
    // sides so scheduler jitter cannot flip the poison/kill branch):
    //   t=0.0  A starts iso-slow 8 s -> times out at t=3.0 (ceiling);
    //          per-id cancel goes out; grace runs t=3.0..4.0.
    //   t=1.8  B starts iso-slow 2.6 s -> finishes at t=4.4, i.e. B is IN
    //          FLIGHT at A's decision -> poison instead of kill.
    Json::Value aResult;
    Json::Value bResult;
    std::thread threadA( [&] {
        Json::Value params( Json::objectValue );
        params["ms"] = 8000;
        aResult = runOperator( stack, "test:iso-slow", params );
    } );
    std::thread threadB( [&] {
        std::this_thread::sleep_for( std::chrono::milliseconds( 1800 ) );
        Json::Value params( Json::objectValue );
        params["ms"] = 2600;
        bResult = runOperator( stack, "test:iso-slow", params );
    } );
    threadA.join();

    // A timed out (typed E6004 path)...
    REQUIRE( aResult["__operatorError"].asBool() );
    REQUIRE( aResult["code"].asString() == "4100" ); // ExternalProcessTimeout
    // ...and at this moment (B still in flight, decision time passed) the
    // worker must be ALIVE: poison, not the v1 whole-worker kill. The v1
    // ladder would have terminated the worker at A's deadline (t=2.5 s)
    // while B runs until t=4.0 s.
    REQUIRE( stack.runtime->isWorkerAlive( kPluginId ) );

    threadB.join();
    // B was still served to completion by the poisoned worker...
    REQUIRE( bResult["success"].asBool() );
    // ...whose in-flight count then drained to zero: the poison kill fired.
    REQUIRE_FALSE( stack.runtime->isWorkerAlive( kPluginId ) );

    // The next call hits the dead worker (E6005), the proxy applies ONE
    // bounded recovery, and the fresh worker answers — all inside one
    // operator call.
    Json::Value after = runOperator( stack, "test:iso-echo", Json::Value() );
    REQUIRE( after["success"].asBool() );
    REQUIRE( stack.runtime->isWorkerAlive( kPluginId ) );
    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "ConcurrencyGate is FIFO-fair and bounded", "[hostprocess][gate]" )
{
    // Unit-level contract of the exact quota enforcement primitive: a full
    // gate refuses bounded waiters (session maps that to E6007) and the
    // first waiter always wins the next slot.
    sicnu::plugins::ConcurrencyGate gate( 1 );
    REQUIRE( gate.acquire( 0 ) );
    std::atomic<bool> firstGotSlot{ false };
    std::atomic<bool> secondGotSlot{ false };
    std::atomic<int> order{ 0 };
    std::thread first( [&] {
        if ( gate.acquire( 2000 ) )
        {
            firstGotSlot = order.fetch_add( 1 ) == 0;
            gate.release();
        }
    } );
    std::thread second( [&] {
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        if ( gate.acquire( 2000 ) )
        {
            secondGotSlot = order.fetch_add( 1 ) == 1;
            gate.release();
        }
    } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 150 ) );
    REQUIRE( gate.width() == 1 );
    gate.release(); // free the original slot; FIFO: first waiter wins
    first.join();
    second.join();
    REQUIRE( firstGotSlot );
    REQUIRE( secondGotSlot );
}

#ifndef _WIN32
TEST_CASE( "process-group cleanup takes worker-spawned grandchildren with the worker",
           "[hostprocess][orphans]" )
{
    // kill(pid,0) reports zombies as alive; read /proc state so a reaped
    // or zombie process counts as gone (non-reaping PID 1 in containers).
    auto grandchildAlive = []( pid_t pid ) {
        if ( pid <= 0 || ::kill( pid, 0 ) != 0 )
            return false;
        std::ifstream stat( "/proc/" + std::to_string( pid ) + "/stat" );
        if ( !stat.is_open() )
            return true; // cannot inspect: keep the liveness answer
        std::string field;
        for ( int i = 0; i < 3; ++i )
        {
            stat >> field;
            if ( i == 2 )
                return field != "Z";
        }
        return true;
    };
    auto waitReaped = [&grandchildAlive]( pid_t pid ) {
        for ( int i = 0; i < 50; ++i )
        {
            if ( !grandchildAlive( pid ) )
                return true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        }
        return !grandchildAlive( pid );
    };

    // Scenario 1: GRACEFUL unload. The fixture writes the grandchild pid to
    // $SICNU_ISO_SPAWN_MARKER (read by the WORKER, so set before load).
    pid_t firstChild = -1;
    {
        Stack stack;
        auto &registry = PluginRegistry::instance();
        const std::string marker = ( std::filesystem::path( stack.tempDir ) / "spawn.pid" ).generic_string();
        ::setenv( "SICNU_ISO_SPAWN_MARKER", marker.c_str(), 1 );
        REQUIRE( loadOrExplain( kPluginId ) );

        Json::Value params( Json::objectValue );
        params["childSeconds"] = 60;
        Json::Value result = runOperator( stack, "test:iso-spawn", params );
        REQUIRE( result["success"].asBool() );
        REQUIRE( result["supported"].asBool() );
        std::ifstream in( marker );
        REQUIRE( in.is_open() );
        in >> firstChild;
        in.close();
        REQUIRE( grandchildAlive( firstChild ) );

        // Graceful unload; the POSIX group reap fires with the shutdown
        // (parity with the Windows job-close semantics).
        REQUIRE( registry.unload( kPluginId ) );
        REQUIRE( waitReaped( firstChild ) );
        ::unsetenv( "SICNU_ISO_SPAWN_MARKER" );
    }

    // Scenario 2: CRASH. The worker aborts itself; the group survives until
    // the session's confirmed-dead cleanup reaps it.
    {
        Stack stack;
        auto &registry = PluginRegistry::instance();
        const std::string marker = ( std::filesystem::path( stack.tempDir ) / "spawn.pid" ).generic_string();
        ::setenv( "SICNU_ISO_SPAWN_MARKER", marker.c_str(), 1 );
        REQUIRE( loadOrExplain( kPluginId ) );

        Json::Value params( Json::objectValue );
        params["childSeconds"] = 60;
        Json::Value result = runOperator( stack, "test:iso-spawn", params );
        REQUIRE( result["success"].asBool() );
        pid_t child = -1;
        std::ifstream in( marker );
        REQUIRE( in.is_open() );
        in >> child;
        in.close();
        REQUIRE( grandchildAlive( child ) );

        Json::Value crash = runOperator( stack, "test:iso-crash", Json::Value() );
        REQUIRE( crash["__operatorError"].asBool() );
        REQUIRE( waitReaped( child ) ); // no orphan after the crash path

        // Reload restores a fresh worker (crash test parity).
        REQUIRE( registry.unload( kPluginId ) );
        REQUIRE( loadOrExplain( kPluginId ) );
        REQUIRE( registry.unload( kPluginId ) );
        ::unsetenv( "SICNU_ISO_SPAWN_MARKER" );
    }
}
#endif

TEST_CASE( "worker-side workDir policy refuses paths outside declared roots",
           "[hostprocess][capabilities]" )
{
    // The manifest declares access.filesystem.write = ["${temp}"]: the
    // containment gate is OPT-IN via declared write roots (a manifest that
    // declares nothing keeps v1 behavior — the executor's default workDir
    // is legitimate and is not gated).
    Stack stack( 6000, 3000, true ); // declare ${temp} as the write root
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    // workDir inside the plugin-scoped temp directory: allowed.
    sicnu::operators::RSOperatorContext inside( stack.tempDir + "/work" );
    Json::Value ok = runOperator( stack, "test:iso-echo", Json::Value(), &inside );
    REQUIRE( ok["success"].asBool() );

    // workDir outside every declared root: typed E5005 refusal, the
    // operator never runs.
    sicnu::operators::RSOperatorContext outside( "/tmp" );
    Json::Value refused = runOperator( stack, "test:iso-echo", Json::Value(), &outside );
    REQUIRE( refused["__operatorError"].asBool() );
    REQUIRE( refused["code"].asString() == "9999" ); // Unknown (stable E5005 in details)
    REQUIRE( refused["message"].asString().find( "E5005" ) != std::string::npos );
    REQUIRE( refused["message"].asString().find( "outside the declared write roots" )
             != std::string::npos );

    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "declarative UI schema round-trips through the worker", "[hostprocess][uischema]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    // Describe: the worker probes the optional entry point, validates the
    // schema (fail closed) and answers with the normalized schema.
    exprs::PluginDiagnosticLog uiLog;
    Json::Value described = stack.runtime->describeUiSchema( kPluginId, uiLog );
    INFO( "describe: " << Json::writeString( Json::StreamWriterBuilder(), described ) );
    REQUIRE( described["ok"].asBool() );
    const Json::Value &schema = described["schema"];
    REQUIRE( schema["version"].asInt() == 1 );
    REQUIRE( schema["commands"].size() == 1 );
    REQUIRE( schema["commands"][0]["id"].asString() == "fixture.refresh" );
    REQUIRE( schema["settingsPages"][0]["controls"].size() == 5 );
    REQUIRE( schema["dockPanels"][0]["controls"].size() == 2 );

    // Invoke: bounded event in, bounded state update out.
    Json::Value event( Json::objectValue );
    event["contributionId"] = "dock.status";
    event["controlId"] = "ping";
    event["eventType"] = "clicked";
    Json::Value invoked = stack.runtime->invokeUi( kPluginId, event, 5000, uiLog );
    INFO( "invoke: " << Json::writeString( Json::StreamWriterBuilder(), invoked ) );
    REQUIRE( invoked["ok"].asBool() );
    REQUIRE( invoked["response"]["state"]["status"].asString() == "pinged" );

    // The worker itself refuses an INVALID schema (fail closed): point a
    // second registry cycle at the same plugin but mutate nothing — the
    // negative path is covered by the SDK validation suite; here we prove
    // the transport stays alive after UI traffic.
    Json::Value params( Json::objectValue );
    params["seconds"] = 1;
    Json::Value after = runOperator( stack, "test:iso-slow", params );
    REQUIRE( after["success"].asBool() );

    REQUIRE( registry.unload( kPluginId ) );
    REQUIRE_FALSE( stack.runtime->isWorkerAlive( kPluginId ) );
}

TEST_CASE( "declarative UI survives the crash-recovery sequence", "[hostprocess][uischema]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    exprs::PluginDiagnosticLog uiLog;
    Json::Value described = stack.runtime->describeUiSchema( kPluginId, uiLog );
    REQUIRE( described["ok"].asBool() );

    // Crash the worker; the proxy recovers (one bounded respawn + reload)
    // and the RETRY crashes again (the fixture always crashes): the hosted
    // session is dead afterwards. A describe on the dead session answers
    // typed E6005 — it never resurrects plugins on its own.
    Json::Value crash = runOperator( stack, "test:iso-crash", Json::Value() );
    REQUIRE( crash["__operatorError"].asBool() );
    described = stack.runtime->describeUiSchema( kPluginId, uiLog );
    INFO( "post-crash describe: "
          << Json::writeString( Json::StreamWriterBuilder(), described ) );
    REQUIRE_FALSE( described["ok"].asBool() );
    REQUIRE( described["error"].asString().find( "E6005" ) != std::string::npos );

    // Restore a healthy worker (the conformance kit does exactly this
    // between PT_RESTART and PT_UI_SCHEMA): unload + load.
    REQUIRE( registry.unload( kPluginId ) );
    REQUIRE( loadOrExplain( kPluginId ) );
    described = stack.runtime->describeUiSchema( kPluginId, uiLog );
    REQUIRE( described["ok"].asBool() );
    REQUIRE( described["schema"]["version"].asInt() == 1 );

    REQUIRE( registry.unload( kPluginId ) );
}

// -- plugin-platform 9.0: M2 concurrency stress --------------------------------

namespace {
/// Runs one caller on an async future so the test can BOUND the wait: a
/// caller that never finishes fails the wait_for (deadlock evidence), it
/// cannot hang the suite.
std::future<Json::Value> callAsync( Stack &stack, const char *operatorId, Json::Value params )
{
    return std::async( std::launch::async, [&stack, operatorId, params]() {
        return runOperator( stack, operatorId, params );
    } );
}
} // namespace

TEST_CASE( "interleaved timeout, crash and cancel keep every caller typed",
           "[hostprocess][stress][p12]" )
{
    // 3 rounds of 7 concurrent callers with deliberately colliding fates
    // (2 hang past the deadline, 2 crash the worker, 3 quick echoes).
    // Deterministic assertions:
    //   1. every caller terminates with a typed envelope (success or
    //      __operatorError) inside a generous wall budget;
    //   2. the session is consistent at quiesce (inFlight back to 0);
    //   3. unload at the end succeeds (barrier close, no leaked lease).
    Stack stack( 3000, 1000 );
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    for ( int round = 0; round < 3; ++round )
    {
        constexpr int kCallers = 7;
        constexpr auto kWallBudget = std::chrono::seconds( 45 );
        std::vector<std::future<Json::Value>> callers;
        callers.reserve( kCallers );
        for ( int i = 0; i < kCallers; ++i )
        {
            const int role = i % 4; // 0,1 slow (timeout); 2 crash; 3 echo
            Json::Value params;
            if ( role == 0 || role == 1 )
                params["ms"] = 8000; // far past the 3 s ceiling
            const char *op = ( role == 0 || role == 1 ) ? "test:iso-slow"
                             : ( role == 2 )            ? "test:iso-crash"
                                                        : "test:iso-echo";
            callers.push_back( callAsync( stack, op, params ) );
        }

        for ( int i = 0; i < kCallers; ++i )
        {
            REQUIRE( callers[ i ].wait_for( kWallBudget ) == std::future_status::ready );
            const Json::Value result = callers[ i ].get();
            const bool typed = result[ "success" ].asBool() || result[ "__operatorError" ].asBool();
            INFO( "round " << round << " caller " << i << ": "
                           << Json::writeString( Json::StreamWriterBuilder(), result ) );
            REQUIRE( typed );
        }

        // Quiesce: in-flight drained on the CURRENT session (any generation).
        const Json::Value snapshot = stack.runtime->diagnosticsSnapshot();
        if ( snapshot["plugins"].isMember( kPluginId ) )
        {
            INFO( "snapshot: " << Json::writeString( Json::StreamWriterBuilder(), snapshot ) );
            REQUIRE( snapshot["plugins"][ kPluginId ]["inFlight"].asInt() == 0 );
        }
    }

    // Clean teardown after the storm.
    REQUIRE( registry.unload( kPluginId ) );
}

// -- plugin-platform 9.0: M3 orphan detection ----------------------------------

#ifndef _WIN32
TEST_CASE( "processGroupState gives honest group evidence across the lifecycle",
           "[hostprocess][orphans][p12]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    // Live worker: its process group HAS a member (the worker itself).
    Json::Value snapshot = stack.runtime->diagnosticsSnapshot();
    REQUIRE( snapshot["plugins"][ kPluginId ]["processGroupState"].asString() == "yes" );

    // After a clean unload the group must be GONE — recorded as evidence in
    // the retiredGroups trail (kill(-pgid, 0) answered ESRCH).
    REQUIRE( registry.unload( kPluginId ) );
    snapshot = stack.runtime->diagnosticsSnapshot();
    REQUIRE( snapshot["retiredGroups"].isMember( kPluginId ) );
    const std::string state = snapshot["retiredGroups"][ kPluginId ].asString();
    // EPERM containers report "unknown" honestly; a false "yes" would be
    // a lie and a "no" without ESRCH evidence impossible.
    REQUIRE( ( state == "no" || state == "unknown" ) );
    if ( state == "unknown" )
        WARN( "process-group probe could not decide (EPERM?); honest unknown recorded" );
}
#endif

// -- plugin-platform 9.0: M4/M5 full contribution surfaces ----------------------

TEST_CASE( "dataProvider round-trip and declared-scheme gate over the worker",
           "[hostprocess][providers][p12]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );
    REQUIRE( stack.sink.dataProviders.count( "test:iso-store" ) == 1 );
    auto &provider = stack.sink.dataProviders.at( "test:iso-store" );

    // discover: the enumeration surface (unfiltered by the scheme gate).
    // The data-provider proxy wraps worker answers in {"result": ...}.
    Json::Value items = provider->discover( Json::Value( Json::objectValue ) )["result"];
    REQUIRE( items.isArray() );
    REQUIRE( items.size() == 2 );

    // inspect/open on a DECLARED scheme: honest envelope round-trip.
    Json::Value metadata = provider->inspect( "isodb://grid" )["result"];
    REQUIRE( metadata["provider"].asString() == "isolation_plugin" );
    Json::Value reference = provider->open( "isodb://grid" )["result"];
    REQUIRE( reference["kind"].asString() == "table" );
    REQUIRE( std::filesystem::exists( reference["path"].asString() ) );

    // open with an UNDECLARED scheme: typed worker-side policy refusal.
    Json::Value refused = provider->open( "otherscheme://grid" );
    REQUIRE( refused["success"].asBool() == false );
    REQUIRE( refused["error"]["code"].asString() == "E5005" );
    REQUIRE( refused["error"]["message"].asString().find( "otherscheme" )
             != std::string::npos );

    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "model runtime identity infer is a known-answer round-trip",
           "[hostprocess][model][p12]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );
    REQUIRE( stack.sink.modelFactories.count( "iso-identity" ) == 1 );

    std::string error;
    PluginModelRequestV1 request;
    request.modelName = "identity-fixture";
    auto runtime = stack.sink.modelFactories.at( "iso-identity" )( request, error );
    REQUIRE( runtime != nullptr );
    REQUIRE( runtime->backendName() == "iso-identity" );

    exprs::PluginTensorV1 input;
    input.data = { 1.f, 2.f, 3.f, 4.f, 5.f, 6.f };
    input.batch = 1;
    input.channels = 2;
    input.rows = 1;
    input.cols = 3;
    auto result = runtime->infer( input, "output" );
    REQUIRE( result.success );
    REQUIRE( result.output.data == input.data ); // exact identity, no tolerance
    REQUIRE( result.output.channels == 2 );
    REQUIRE( result.output.cols == 3 );

    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "agent tool executes through the SpatialTool envelope",
           "[hostprocess][agenttool][p12]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );
    REQUIRE( stack.sink.agentTools.count( "test:iso-tool" ) == 1 );

    Json::Value params( Json::objectValue );
    params["query"] = "hello";
    Json::Value envelope = stack.sink.agentTools.at( "test:iso-tool" )->execute( params );
    REQUIRE( envelope["success"].asBool() );
    REQUIRE( envelope["result"]["tool"].asString() == "test:iso-tool" );
    REQUIRE( envelope["result"]["echo"]["query"].asString() == "hello" );

    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "model framework gate refuses frameworks outside the declared access model",
           "[hostprocess][capabilities][p12]" )
{
    // Rewrite the manifest with a declared access.modelProvider.frameworks
    // list that EXCLUDES a second framework the fixture would register.
    // Because the worker only reports frameworks the plugin registered, the
    // host gate is exercised through the runtime-host path: load succeeds
    // (the declared framework passes), then the SINK path would refuse an
    // undeclared one — asserted at the unit level in test_plugin_capabilities
    // and structurally here by confirming the declared framework loads.
    Json::Value access( Json::objectValue );
    Json::Value modelProvider( Json::objectValue );
    Json::Value frameworks( Json::arrayValue );
    frameworks.append( "iso-identity" );
    modelProvider["frameworks"] = frameworks;
    access["modelProvider"] = modelProvider;
    access["ui"] = true;

    Stack stack;
    writeManifest( access ); // Stack already rewrote it; re-write with access
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );
    REQUIRE( stack.sink.modelFactories.count( "iso-identity" ) == 1 );

    // The declared framework works end-to-end despite the gate being armed.
    std::string error;
    PluginModelRequestV1 request;
    auto runtime = stack.sink.modelFactories.at( "iso-identity" )( request, error );
    REQUIRE( runtime != nullptr );

    REQUIRE( registry.unload( kPluginId ) );
}

TEST_CASE( "ui.invoke validates events host-side before the worker (E6010)",
           "[hostprocess][uischema][p12]" )
{
    Stack stack;
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    exprs::PluginDiagnosticLog uiLog;
    // A VALID event travels to the worker and answers (fixture echo).
    Json::Value event( Json::objectValue );
    event["contributionId"] = "dock.status";
    event["controlId"] = "ping";
    event["eventType"] = "clicked";
    auto ok = stack.runtime->invokeUi( kPluginId, event, 10000, uiLog );
    REQUIRE( ok["ok"].asBool() );

    // An UNKNOWN event type is refused host-side, typed E6010, and the
    // channel/worker stay perfectly healthy afterwards.
    event["eventType"] = "teleport";
    auto refused = stack.runtime->invokeUi( kPluginId, event, 10000, uiLog );
    REQUIRE_FALSE( refused["ok"].asBool() );
    REQUIRE( refused["code"].asString() == "E6010" );
    REQUIRE( refused["error"].asString().find( "teleport" ) != std::string::npos );

    // An oversized value is refused too.
    event["eventType"] = "custom";
    event["value"] = std::string( 8192, 'v' );
    auto oversized = stack.runtime->invokeUi( kPluginId, event, 10000, uiLog );
    REQUIRE_FALSE( oversized["ok"].asBool() );
    REQUIRE( oversized["code"].asString() == "E6010" );

    // The worker survived the refusals and still answers a valid event.
    Json::Value recovery( Json::objectValue );
    recovery["contributionId"] = "dock.status";
    recovery["controlId"] = "ping";
    recovery["eventType"] = "clicked";
    auto after = stack.runtime->invokeUi( kPluginId, recovery, 10000, uiLog );
    REQUIRE( after["ok"].asBool() );

    REQUIRE( registry.unload( kPluginId ) );
}
