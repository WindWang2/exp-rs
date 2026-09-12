// tests/test_exprs_plugin_loader.cpp — native loading with a real fixture .so
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_host_runtime.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_registry.h"
#include "exprs/plugin_validator.h"
#include "exprs/version.h"

#include "operators/framework/rs_operator_context.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>

using namespace exprs;

#ifndef SICNU_TEST_HELLO_PLUGIN_DIR
#error "SICNU_TEST_HELLO_PLUGIN_DIR must point at the built fixture plugin dir"
#endif

namespace {
void writeManifest( const std::string &dir, const std::string &entrypoint, int abiVersion )
{
    std::ofstream output( dir + "/plugin.json", std::ios::trunc );
    output << R"({
        "manifest_version": 1,
        "id": "org.exprs.test.hello-plugin",
        "name": "Hello Fixture",
        "version": "1.0.0",
        "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
        "abi_version": )" << abiVersion << R"(,
        "entrypoint": ")" << entrypoint << R"(",
        "entrypoint_kind": "native",
        "capabilities": ["operator"],
        "operators": [{ "id": "test:hello", "display_name": "Test Hello", "group": "test" }]
    })";
}

/// Recording sink capturing registered factories.
class RecordingSink : public PluginContributionSink
{
public:
    void revokePlugin( const std::string &pluginId ) override
    {
        // Sink contract: drop plugin-originated callables BEFORE the library
        // is unloaded (std::function targets point into the plugin .so).
        (void)pluginId;
        operatorIds.clear();
        factories.clear();
    }
public:
    bool registerOperatorFactory(
        const std::string &pluginId, const std::string &operatorId,
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory ) override
    {
        operatorIds.push_back( operatorId );
        factories[operatorId] = std::move( factory );
        return true;
    }
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

    std::vector<std::string> operatorIds;
    std::map<std::string, std::function<std::unique_ptr<sicnu::operators::RSOperator>()>> factories;
};
} // namespace

TEST_CASE( "registry load of a native plugin registers a working factory",
           "[plugin][loader][regression]" )
{
    // Guards against the dead-code regression where the native load path was
    // unreachable: registry reported Loaded but the sink never received the
    // operator factory ("operator factory returned nullptr" at execute).
    writeManifest( SICNU_TEST_HELLO_PLUGIN_DIR, "libhello_plugin.so", pluginAbiVersion() );

    exprs::PluginRegistryOptions options;
    // Discovery scans subdirectories of each root — the root is the fixture
    // PARENT (scan() skips the root directory itself).
    options.roots = { std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/.." };
    options.policy.allowThirdPartyNative = true;
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    RecordingSink sink;
    registry.setContributionSink( &sink );
    registry.configure( options );
    registry.setEnabled( "org.exprs.test.hello-plugin", true );

    REQUIRE( registry.load( "org.exprs.test.hello-plugin" ) );
    REQUIRE( registry.isLoaded( "org.exprs.test.hello-plugin" ) );
    REQUIRE( sink.factories.count( "test:hello" ) == 1 );

    {
        // Operator objects live inside the plugin library: destroy them
        // BEFORE unloading the library (same contract as production code).
        auto instance = sink.factories.at( "test:hello" )();
        REQUIRE( instance != nullptr );
        sicnu::operators::RSOperatorContext context;
        Json::Value result = instance->run( Json::Value( Json::objectValue ), context );
        REQUIRE( result.get( "success", false ).asBool() );
    }

    REQUIRE( registry.unload( "org.exprs.test.hello-plugin" ) );
    REQUIRE( sink.factories.empty() ); // revoked before dlclose
    registry.setContributionSink( nullptr );
}

TEST_CASE( "manifest gate rejects ABI mismatch before dlopen", "[plugin][loader]" )
{
    writeManifest( SICNU_TEST_HELLO_PLUGIN_DIR, "libhello_plugin.so", 999 );
    PluginDiagnosticLog log;
    PluginRecord record = PluginDiscovery::inspectDirectory( SICNU_TEST_HELLO_PLUGIN_DIR, log );
    REQUIRE( record.state == PluginState::Incompatible );
    bool sawAbi = false;
    for ( const auto &item : log.items() )
        sawAbi = sawAbi || item.code == PluginDiagnosticCode::AbiVersionMismatch;
    REQUIRE( sawAbi );
}

TEST_CASE( "loader drives the full native plugin lifecycle", "[plugin][loader]" )
{
    writeManifest( SICNU_TEST_HELLO_PLUGIN_DIR, "libhello_plugin.so", pluginAbiVersion() );

    PluginDiagnosticLog log;
    PluginRecord record = PluginDiscovery::inspectDirectory( SICNU_TEST_HELLO_PLUGIN_DIR, log );
    REQUIRE( record.state == PluginState::Validated );

    // Entrypoint probe (plugin doctor surface) — no code executed.
    std::string probeError;
    REQUIRE( PluginLoader::probeEntrypoint( record.directory + "/" + record.manifest.entrypoint,
                                            probeError ) );

    // Full load: initialize + registerContributions into the sink.
    auto services = PluginLoader::createDefaultHostServices( "/tmp", {}, {} );
    RecordingSink sink;
    PluginLoader loader;
    REQUIRE( loader.load( record, *services, sink, log ) );
    REQUIRE( sink.operatorIds == std::vector<std::string>{ "test:hello" } );

    // The registered factory creates a working operator. The operator object
    // lives inside the plugin library — destroy it BEFORE unloading the
    // library.
    {
        auto instance = sink.factories.at( "test:hello" )();
        REQUIRE( instance != nullptr );
        sicnu::operators::RSOperatorContext context;
        Json::Value result = instance->run( Json::Value( Json::objectValue ), context );
        REQUIRE( result.get( "success", false ).asBool() );
    }

    LoadedPlugin taken = loader.take();
    REQUIRE( taken.instance != nullptr );

    // Unload path shuts down and releases the library.
    REQUIRE( loader.unload( taken, log ) );
    REQUIRE( taken.instance == nullptr );

    // Missing entrypoint symbol.
    writeManifest( SICNU_TEST_HELLO_PLUGIN_DIR, "libhello_plugin.so", pluginAbiVersion() );
    SECTION( "bogus library reports a load diagnostic" )
    {
        PluginDiagnosticLog failureLog;
        PluginRecord bogus = record;
        bogus.manifest.entrypoint = "libdoes_not_exist.so";
        PluginLoader failingLoader;
        RecordingSink failingSink;
        REQUIRE_FALSE( failingLoader.load( bogus, *services, failingSink, failureLog ) );
        REQUIRE( failureLog.hasErrors() );
    }
}

TEST_CASE( "loader re-checks entrypoint containment at load time (issue #756)",
           "[plugin][loader][containment]" )
{
    // Validation and load read the filesystem at different times; the loader
    // must refuse a record whose entrypoint escapes (or stopped being inside)
    // the plugin root, independently of the validator verdict.
    namespace fs = std::filesystem;
    const std::string root = "/tmp/exprs_test_loader_escape";
    fs::remove_all( root );
    fs::create_directories( root + "/org.test.escape" );
    const std::string pluginDir = root + "/org.test.escape";
    // A real file OUTSIDE the plugin dir (the escape target exists — the
    // refusal must not be a mere missing-file accident).
    { std::ofstream output( root + "/liboutside.so", std::ios::binary ); output << "outside"; }
    // And a legal file inside, later swapped for an escaping symlink.
    { std::ofstream output( pluginDir + "/liblegal.so", std::ios::binary ); output << "legal"; }

    auto makeRecord = []( const std::string &entrypoint ) {
        PluginRecord record;
        record.directory = "/tmp/exprs_test_loader_escape/org.test.escape";
        record.manifestPath = record.directory + "/plugin.json";
        record.manifest.manifestVersion = 1;
        record.manifest.id = "org.test.escape";
        record.manifest.name = "Escape";
        record.manifest.version = "1.0.0";
        record.manifest.apiVersion = std::string( EXP_RS_PLUGIN_API_VERSION );
        record.manifest.abiVersion = pluginAbiVersion();
        record.manifest.entrypoint = entrypoint;
        record.manifest.entrypointKind = PluginEntrypointKind::Native;
        record.state = PluginState::Validated;
        return record;
    };

    auto services = PluginLoader::createDefaultHostServices( "/tmp", {}, {} );

    SECTION( ".. entrypoint refused before dlopen" )
    {
        PluginRecord record = makeRecord( "../liboutside.so" );
        PluginLoader loader;
        RecordingSink sink;
        PluginDiagnosticLog log;
        REQUIRE_FALSE( loader.load( record, *services, sink, log ) );
        REQUIRE( log.hasErrors() );
        bool sawEscape = false;
        for ( const auto &item : log.items() )
            sawEscape = sawEscape || item.code == PluginDiagnosticCode::EntrypointOutsideRoot;
        REQUIRE( sawEscape );
        REQUIRE( sink.factories.empty() );
    }
    SECTION( "absolute entrypoint refused before dlopen" )
    {
        PluginRecord record =
            makeRecord( "/tmp/exprs_test_loader_escape/liboutside.so" );
        PluginLoader loader;
        RecordingSink sink;
        PluginDiagnosticLog log;
        REQUIRE_FALSE( loader.load( record, *services, sink, log ) );
        REQUIRE( log.hasErrors() );
    }
    SECTION( "file swapped to symlink escape after validation is refused" )
    {
        // Simulates the validation→load TOCTOU: the record was validated when
        // liblegal.so was a regular file inside the root; by load time it is
        // a symlink to a library outside.
        std::error_code linkError;
        fs::create_symlink( "/tmp/exprs_test_loader_escape/liboutside.so",
                            fs::path( pluginDir + "/liblegal.so" ), linkError );
        if ( linkError )
        {
            fs::remove_all( root );
            return;
        }
        PluginRecord record = makeRecord( "liblegal.so" );
        PluginLoader loader;
        RecordingSink sink;
        PluginDiagnosticLog log;
        REQUIRE_FALSE( loader.load( record, *services, sink, log ) );
        bool sawEscape = false;
        for ( const auto &item : log.items() )
            sawEscape = sawEscape || item.code == PluginDiagnosticCode::EntrypointOutsideRoot;
        REQUIRE( sawEscape );
    }
    fs::remove_all( root );
}

TEST_CASE( "registry load drops the lock across host-process spawn (issue #928)",
           "[plugin][registry][lockdrop]" )
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "exprs_test_lockdrop";
    fs::remove_all( root );
    const fs::path slowDir = root / "org.test.slow-load";
    const fs::path peerDir = root / "org.test.peer-load";
    fs::create_directories( slowDir );
    fs::create_directories( peerDir );

#ifdef _WIN32
    const char *entrypoint = "libslow_plugin.dll";
#else
    const char *entrypoint = "libslow_plugin.so";
#endif
    {
        std::ofstream lib( ( slowDir / entrypoint ).string(), std::ios::binary );
        lib << "dummy";
        std::ofstream manifest( ( slowDir / "plugin.json" ).string(), std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.slow-load",
            "name": "Slow",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "runtime": "host-process",
            "entrypoint": ")" << entrypoint << R"(",
            "entrypoint_kind": "native",
            "capabilities": ["operator"],
            "operators": [{ "id": "test:slow", "display_name": "Slow", "group": "test" }]
        })";
    }
    {
        std::ofstream manifest( ( peerDir / "plugin.json" ).string(), std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.peer-load",
            "name": "Peer",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": []
        })";
    }

    class SlowRuntime : public HostProcessRuntime
    {
    public:
        std::atomic<bool> entered{ false };
        bool loadPlugin( const PluginRecord &, HostServicesV1 &, PluginContributionSink &,
                         PluginDiagnosticLog & ) override
        {
            entered.store( true );
            std::this_thread::sleep_for( std::chrono::milliseconds( 400 ) );
            return true;
        }
        bool unloadPlugin( const std::string &, PluginDiagnosticLog & ) override { return true; }
        Json::Value diagnosticsSnapshot() const override { return Json::Value(); }
    };

    SlowRuntime runtime;
    RecordingSink sink;
    PluginRegistryOptions options;
    options.roots = { root.generic_string() };
    options.policy.allowThirdPartyNative = true;
    PluginRegistry &registry = PluginRegistry::instance();
    registry.setContributionSink( &sink );
    registry.setHostProcessRuntime( &runtime );
    registry.configure( options );
    registry.setEnabled( "org.test.slow-load", true );
    registry.setEnabled( "org.test.peer-load", true );

    PluginRecord copied;
    REQUIRE( registry.copyRecord( "org.test.peer-load", copied ) );
    REQUIRE( copied.id() == "org.test.peer-load" );

    std::thread loader( [ &registry ] { (void)registry.load( "org.test.slow-load" ); } );
    const auto waitStart = std::chrono::steady_clock::now();
    while ( !runtime.entered.load()
            && std::chrono::steady_clock::now() - waitStart < std::chrono::seconds( 2 ) )
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    REQUIRE( runtime.entered.load() );

    // record()/refresh() of a *different* plugin must not wait out the
    // 400 ms spawn. refresh() rebuilds mRecords (the in-flight load may
    // then abandon); the bound is the lock-drop proof.
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE( registry.record( "org.test.peer-load" ) != nullptr );
    registry.refresh();
    REQUIRE( registry.copyRecord( "org.test.peer-load", copied ) );
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE( elapsed < std::chrono::milliseconds( 150 ) );
    REQUIRE( copied.id() == "org.test.peer-load" );

    loader.join();
    registry.unloadAll();
    registry.setHostProcessRuntime( nullptr );
    registry.setContributionSink( nullptr );
    fs::remove_all( root );
}
