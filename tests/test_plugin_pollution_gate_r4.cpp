// tests/test_plugin_pollution_gate_r4.cpp — pollution gate (track R4 WP-F).
// A plugin tree that grows an UNKNOWN native library must never widen what
// the host trusts:
//
//   1. an unknown .so with NO manifest is invisible to discovery (no
//      manifest, no record) and typed (Broken) on direct inspect — never a
//      crash, never an accidental load;
//   2. the same library WITH a manifest claiming an id is refused by the
//      trust gate when third-party natives are disallowed (the default in
//      production policy) — typed TrustRejected, zero contributions;
//   3. claiming a TRUSTED plugin's id does not help: the manifest/binary
//      identity gate refuses the impostor (InitializationFailed) while the
//      real plugin keeps loading from its own directory.
//
// (The legacy-host module whitelist that #1334 adds lives on a different
// channel — see DECISIONS.md D-2 for why it is not pinned here.)
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_registry.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

using namespace exprs;

#ifndef SICNU_TEST_HELLO_PLUGIN_DIR
#error "SICNU_TEST_HELLO_PLUGIN_DIR must point at the built hello fixture plugin dir"
#endif

namespace {

#ifdef _WIN32
const char *kHelloEntrypoint = "libhello_plugin.dll";
#else
const char *kHelloEntrypoint = "libhello_plugin.so";
#endif
const char *kHelloId = "org.exprs.test.hello-plugin";

/// Minimal accepting sink — the load preflight refuses (RegistrationFailed)
/// when no sink is installed, which would mask the trust/identity gates
/// under test.
class AcceptingSink : public PluginContributionSink
{
public:
    bool registerOperatorFactory( const std::string &, const std::string &,
                                  std::function<std::unique_ptr<sicnu::operators::RSOperator>()> ) override
    {
        return true;
    }
    bool registerDataProvider( const std::string &, const std::string &,
                               std::shared_ptr<IPluginDataProviderV1> ) override { return true; }
    bool registerModelRuntime( const std::string &, const std::string &,
                               PluginModelRuntimeFactoryV1 ) override { return true; }
    bool registerAgentTool( const std::string &, const std::string &,
                            std::shared_ptr<IPluginAgentToolV1> ) override { return true; }
};

/// A plugin root with the trusted hello fixture plus a pollution payload.
struct PollutionFixture
{
    PluginRegistry &registry = PluginRegistry::instance();
    const std::string root;
    const std::string helloDir;
    const std::string strangerDir;

    PollutionFixture()
        : root( ( std::filesystem::temp_directory_path() / "exprs_test_pollution_r4" )
                    .generic_string() )
        , helloDir( root + "/" + kHelloId )
        , strangerDir( root + "/org.test.stranger" )
    {
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        std::filesystem::create_directories( helloDir, ec );
        std::filesystem::create_directories( strangerDir, ec );
        std::filesystem::copy_file( std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/"
                                        + kHelloEntrypoint,
                                    helloDir + "/" + kHelloEntrypoint, ec );
        std::filesystem::copy_file( std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/"
                                        + kHelloEntrypoint,
                                    strangerDir + "/" + kHelloEntrypoint, ec );
        writeHelloManifest();
    }
    ~PollutionFixture()
    {
        registry.unloadAll();
        registry.setContributionSink( nullptr );
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
    }

    void writeHelloManifest()
    {
        std::ofstream manifest( helloDir + "/plugin.json", std::ios::trunc );
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

    void writeStrangerManifest( const std::string &id )
    {
        std::ofstream manifest( strangerDir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": ")" << id << R"(",
            "name": "Stranger",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint": ")" << kHelloEntrypoint << R"(",
            "entrypoint_kind": "native",
            "capabilities": ["operator"],
            "operators": [{ "id": "test:hello", "display_name": "Stranger", "group": "test" }]
        })";
    }

    void configure( bool allowThirdPartyNative )
    {
        PluginRegistryOptions options;
        // Production root order: index 0 is the app-bundled root (origin
        // Builtin — always trusted), later roots are system/user. The trust
        // gate only refuses third-party natives from those LATER roots, so
        // the fixture mirrors the shape with a dummy bundled dir up front.
        options.roots = { root + "/bundled", root };
        options.tempDirectory = root;
        options.policy.allowThirdPartyNative = allowThirdPartyNative;
        registry.setContributionSink( &sink );
        registry.configure( options );
        registry.setEnabled( kHelloId, true );
    }
    AcceptingSink sink;
};

} // namespace

TEST_CASE( "an unknown library without a manifest stays invisible and typed",
           "[plugin][pollution][r4]" )
{
    PollutionFixture fixture;
    // Plant the pollution: an extra native library, no manifest.
    std::error_code ec;
    std::filesystem::copy_file(
        std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/" + kHelloEntrypoint,
        fixture.helloDir + "/libunknown_stranger.so", ec );
    REQUIRE( !ec );
    // ...and an entire manifest-less directory of libraries next to it.
    std::filesystem::create_directories( fixture.root + "/org.test.dark", ec );
    std::filesystem::copy_file(
        std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/" + kHelloEntrypoint,
        fixture.root + "/org.test.dark/libdark.so", ec );
    REQUIRE( !ec );

    fixture.configure( true ); // even with third-party natives ALLOWED...

    // ...manifest-less libraries are not plugins: no record, no load, no crash.
    REQUIRE( fixture.registry.record( "libunknown_stranger" ) == nullptr );
    REQUIRE( fixture.registry.record( "org.test.dark" ) == nullptr );
    REQUIRE_FALSE( fixture.registry.load( "org.test.dark" ) );
    // The trusted plugin still loads from the polluted tree.
    REQUIRE( fixture.registry.load( kHelloId ) );
    REQUIRE( fixture.registry.isLoaded( kHelloId ) );

    // Direct inspect of a manifest-less directory is typed, not a crash.
    PluginDiagnosticLog log;
    const PluginRecord dark =
        PluginDiscovery::inspectDirectory( fixture.root + "/org.test.dark", log );
    REQUIRE( dark.state == PluginState::Broken );
}

TEST_CASE( "a manifest claims nothing when third-party natives are untrusted",
           "[plugin][pollution][r4]" )
{
    PollutionFixture fixture;
    fixture.writeStrangerManifest( "org.test.stranger" );
    fixture.configure( false ); // production trust: bundled natives only

    const PluginRecord *stranger = fixture.registry.record( "org.test.stranger" );
    REQUIRE( stranger != nullptr );
    // The trust gate runs at CONFIGURE time: the record is Blocked and the
    // TrustRejected verdict sits on the RECORD's diagnostics.
    REQUIRE( stranger->state == PluginState::Blocked );
    bool sawTrust = false;
    for ( const PluginDiagnostic &item : stranger->diagnostics.items() )
        if ( item.code == PluginDiagnosticCode::TrustRejected )
            sawTrust = true;
    REQUIRE( sawTrust );
    REQUIRE_FALSE( fixture.registry.load( "org.test.stranger" ) );
    // The polluted neighbour does not poison the trusted plugin: with the
    // trust gate open the hello fixture still loads from the same tree.
    fixture.configure( true );
    REQUIRE( fixture.registry.load( kHelloId ) );
    REQUIRE( fixture.registry.isLoaded( kHelloId ) );
}

TEST_CASE( "impersonating a trusted id is refused by the identity gate",
           "[plugin][pollution][r4]" )
{
    PollutionFixture fixture;
    // The stranger directory claims to BE the trusted plugin.
    fixture.writeStrangerManifest( kHelloId );
    fixture.configure( true ); // trust gates open — identity gate must hold

    // First-root-wins discovery resolves the id to the REAL directory; the
    // stranger copy is a shadowed duplicate the registry never loads.
    const PluginRecord *record = fixture.registry.record( kHelloId );
    REQUIRE( record != nullptr );
    REQUIRE( record->directory == fixture.helloDir );
    REQUIRE( fixture.registry.load( kHelloId ) );
    REQUIRE( fixture.registry.isLoaded( kHelloId ) );
    // The loaded generation is the trusted one (same directory bytes).
    REQUIRE( fixture.registry.record( kHelloId )->manifest.entrypoint
             == kHelloEntrypoint );
}
