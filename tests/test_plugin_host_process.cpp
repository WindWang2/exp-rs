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
    manifest["capabilities"].append( "operator" );
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
    // Dedicated budgets: 2.5 s deadline ceiling, 400 ms kill grace.
    Stack stack( 2500, 400 );
    auto &registry = PluginRegistry::instance();
    REQUIRE( loadOrExplain( kPluginId ) );

    // Sequence (deterministic, margins >= 1 s):
    //   t=0.0  A starts iso-slow 8 s  -> times out at t=2.5 (ceiling);
    //          per-id cancel goes out, grace runs t=2.5..2.9.
    //   t=2.0  B starts iso-slow 2 s  -> would finish at t=4.0, i.e. B is
    //          IN FLIGHT at A's escalation decision (t=2.9).
    //   => A's timeout must NOT kill the worker (B is a peer in flight);
    //      the session is poisoned and the worker dies at drain.
    Json::Value aResult;
    Json::Value bResult;
    std::thread threadA( [&] {
        Json::Value params( Json::objectValue );
        params["seconds"] = 8;
        aResult = runOperator( stack, "test:iso-slow", params );
    } );
    std::thread threadB( [&] {
        std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );
        Json::Value params( Json::objectValue );
        params["seconds"] = 2;
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
    auto grandchildAlive = []( pid_t pid ) { return pid > 0 && ::kill( pid, 0 ) == 0; };
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
