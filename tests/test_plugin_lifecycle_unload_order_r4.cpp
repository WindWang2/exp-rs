// tests/test_plugin_lifecycle_unload_order_r4.cpp — unload ORDER contracts
// (track R4 WP-C). The registry documents its sequence (plugin_registry.cpp,
// "Unload sequence (issue #747), in order"):
//
//   1. arm the drain (beginPluginDrain) and mark the record Quiescing;
//   2. wait bounded for in-flight executions (waitPluginIdle) — refusing
//      (state restored, drain cancelled) on timeout;
//   3. revoke host-side contributions (revokePlugin) while the code is
//      still mapped;
//   4. shutdown + delete the plugin instance, then dlclose LAST.
//
// Every step is observable through a recording sink; the tests pin the
// STRICT order (markers strictly increasing), the boundedness of every wait
// (no infinite join may ever hang ctest), and the "contributions never
// outlive the mapping" side (factories empty after unload; reload works).
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_loader.h"
#include "exprs/plugin_registry.h"
#include "exprs/version.h"

#include <fstream>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace exprs;

#ifndef SICNU_TEST_HELLO_PLUGIN_DIR
#error "SICNU_TEST_HELLO_PLUGIN_DIR must point at the built hello fixture plugin dir"
#endif

namespace {

const char *kHelloId = "org.exprs.test.hello-plugin";
#ifdef _WIN32
const char *kHelloEntrypoint = "libhello_plugin.dll";
#elif defined( __APPLE__ )
const char *kHelloEntrypoint = "libhello_plugin.dylib";
#else
const char *kHelloEntrypoint = "libhello_plugin.so";
#endif

/// Sink that records every lifecycle event in arrival order and can stall
/// the drain barrier (neverIdle) the way a busy plugin would.
class OrderSink : public PluginContributionSink
{
public:
    void revokePlugin( const std::string & ) override
    {
        events.push_back( "revoke" );
        operatorIds.clear();
        factories.clear();
    }
    bool registerOperatorFactory(
        const std::string &, const std::string &operatorId,
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory ) override
    {
        operatorIds.push_back( operatorId );
        factories[ operatorId ] = std::move( factory );
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
    void beginPluginDrain( const std::string & ) override { events.push_back( "drain:begin" ); }
    bool waitPluginIdle( const std::string &pluginId, int timeoutMs ) override
    {
        (void)pluginId;
        events.push_back( "drain:wait" );
        lastWaitTimeoutMs = timeoutMs;
        return !neverIdle;
    }
    void cancelPluginDrain( const std::string & ) override { events.push_back( "drain:cancel" ); }
    void pluginLoaded( const std::string & ) override { events.push_back( "loaded" ); }

    std::vector<std::string> events;
    std::vector<std::string> operatorIds;
    std::map<std::string, std::function<std::unique_ptr<sicnu::operators::RSOperator>()>> factories;
    bool neverIdle = false;
    int lastWaitTimeoutMs = -1;
};

/// Registry stage with the hello fixture loaded under a recording sink.
struct UnloadOrderFixture
{
    PluginRegistry &registry = PluginRegistry::instance();
    OrderSink sink;
    const std::string root;
    const std::string pluginDir{ std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) };

    UnloadOrderFixture()
        : root( ( std::filesystem::temp_directory_path() / "exprs_test_unload_order_r4" )
                    .generic_string() )
    {
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        // The fixture dir ships no manifest — the tests own it (same deal
        // as ReloadFixture in test_exprs_plugin_loader.cpp).
        {
            std::ofstream manifest( pluginDir + "/plugin.json", std::ios::trunc );
            manifest << R"({
            "manifest_version": 1,
            "id": ")" << kHelloId << R"(",
            "name": "Hello Fixture",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint": ")" << kHelloEntrypoint << R"(",
            "entrypoint_kind": "native",
            "capabilities": ["operator"],
            "operators": [{ "id": "test:hello", "display_name": "Test Hello", "group": "test" }]
        })";
        }
        PluginRegistryOptions options;
        options.roots = { pluginDir + "/.." };
        options.tempDirectory = root;
        options.policy.allowThirdPartyNative = true;
        options.policy.devMode = true; // last-good captures (async) like dev usage
        registry.setContributionSink( &sink );
        registry.configure( options );
        registry.setEnabled( kHelloId, true );
    }
    ~UnloadOrderFixture()
    {
        registry.unloadAll();
        registry.setContributionSink( nullptr );
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        // The dev-mode temp tree must not leak into the next test.
        std::filesystem::remove_all(
            std::filesystem::temp_directory_path().generic_string()
                + "/sicnu-plugin-snapshots",
            ec );
    }

    bool sawEvent( const std::string &event ) const
    {
        for ( const std::string &item : sink.events )
            if ( item == event )
                return true;
        return false;
    }

    /// Index of the LAST occurrence of @p event (events may repeat across
    /// load/unload cycles within one case).
    int lastIndexOf( const std::string &event ) const
    {
        for ( int i = static_cast<int>( sink.events.size() ) - 1; i >= 0; --i )
            if ( sink.events[ static_cast<size_t>( i ) ] == event )
                return i;
        return -1;
    }
};

} // namespace

TEST_CASE( "unload drains first, revokes while mapped, then tears down",
           "[plugin][unloadorder][r4]" )
{
    UnloadOrderFixture fixture;
    REQUIRE( fixture.registry.load( kHelloId ) );
    REQUIRE( fixture.sink.factories.count( "test:hello" ) == 1 );
    fixture.sink.events.clear();

    const bool unloaded = fixture.registry.unload( kHelloId );
    REQUIRE( unloaded );

    // Strict order: drain armed BEFORE the bounded wait, revoke AFTER the
    // record left the loaded set (the barrier guards the revoke).
    const int begin = fixture.lastIndexOf( "drain:begin" );
    const int wait = fixture.lastIndexOf( "drain:wait" );
    const int revoke = fixture.lastIndexOf( "revoke" );
    REQUIRE( begin >= 0 );
    REQUIRE( wait > begin );
    REQUIRE( revoke > wait );
    // The drain got the registry's bounded budget (never a 0/-1 accident).
    REQUIRE( fixture.sink.lastWaitTimeoutMs > 0 );

    // Contribution contract: nothing of the plugin survives the unload.
    REQUIRE( fixture.sink.factories.empty() );
    REQUIRE_FALSE( fixture.registry.isLoaded( kHelloId ) );
    REQUIRE( fixture.registry.record( kHelloId ) != nullptr );
    REQUIRE( fixture.registry.record( kHelloId )->state == PluginState::Unloaded );

    // dlclose-last side: the library is unmappable and loadable again.
    REQUIRE( fixture.registry.load( kHelloId ) );
    REQUIRE( fixture.sink.factories.count( "test:hello" ) == 1 );
}

TEST_CASE( "a busy plugin refuses the unload and stays fully usable",
           "[plugin][unloadorder][r4]" )
{
    UnloadOrderFixture fixture;
    REQUIRE( fixture.registry.load( kHelloId ) );
    fixture.sink.events.clear();
    fixture.sink.neverIdle = true;

    const bool unloaded = fixture.registry.unload( kHelloId );
    fixture.sink.neverIdle = false;
    REQUIRE_FALSE( unloaded );

    // Refusal path order: drain armed, wait failed, drain CANCELLED, and
    // NO revoke (contributions of a running plugin are never dropped).
    REQUIRE( fixture.sawEvent( "drain:begin" ) );
    REQUIRE( fixture.sawEvent( "drain:wait" ) );
    REQUIRE( fixture.sawEvent( "drain:cancel" ) );
    REQUIRE_FALSE( fixture.sawEvent( "revoke" ) );

    // The plugin keeps running: state restored, factory intact, and a
    // follow-up unload (plugin idle by then) succeeds.
    REQUIRE( fixture.registry.isLoaded( kHelloId ) );
    REQUIRE( fixture.sink.factories.count( "test:hello" ) == 1 );
    bool sawInUse = false;
    for ( const PluginDiagnostic &item : fixture.registry.diagnostics().items() )
        if ( item.code == PluginDiagnosticCode::PluginInUse )
            sawInUse = true;
    REQUIRE( sawInUse );
    REQUIRE( fixture.registry.unload( kHelloId ) );
}

TEST_CASE( "the drain wait is bounded — a refusing barrier cannot hang unload",
           "[plugin][unloadorder][r4]" )
{
    UnloadOrderFixture fixture;
    REQUIRE( fixture.registry.load( kHelloId ) );

    fixture.sink.neverIdle = true;
    const auto started = std::chrono::steady_clock::now();
    const bool unloaded = fixture.registry.unload( kHelloId, 250 );
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started )
                               .count();
    fixture.sink.neverIdle = false;
    REQUIRE_FALSE( unloaded );
    // The explicit budget was honored and the call RETURNED (the contract
    // under test: no unbounded join may ever hang the caller).
    REQUIRE( fixture.sink.lastWaitTimeoutMs == 250 );
    REQUIRE( elapsedMs < 10000 );
}

TEST_CASE( "unloading while the last-good capture is in flight stays bounded and clean",
           "[plugin][unloadorder][r4]" )
{
    // Sibling contract of "registry teardown joins an in-flight snapshot
    // capture" (test_exprs_plugin_loader): a single unload racing the async
    // last-good capture must complete, stay bounded, and leave the registry
    // consistent — whichever way the race resolves.
    UnloadOrderFixture fixture;
    REQUIRE( fixture.registry.load( kHelloId ) );

    const auto started = std::chrono::steady_clock::now();
    const bool unloaded = fixture.registry.unload( kHelloId );
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started )
                               .count();
    REQUIRE( unloaded );
    REQUIRE( elapsedMs < 30000 ); // snapshotWaitMs/teardown bounds, generous

    REQUIRE_FALSE( fixture.registry.isLoaded( kHelloId ) );
    REQUIRE( fixture.sink.factories.empty() );
    // The registry is immediately reusable: reload + unload again both work.
    REQUIRE( fixture.registry.load( kHelloId ) );
    REQUIRE( fixture.registry.unload( kHelloId ) );
}
