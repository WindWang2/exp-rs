// tests/test_exprs_plugin_loader.cpp — native loading with a real fixture .so
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_host_runtime.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_package.h"
#include "exprs/plugin_permissions.h"
#include "exprs/plugin_registry.h"
#include "exprs/plugin_snapshot.h"
#include "exprs/plugin_validator.h"
#include "exprs/version.h"

#include <QtCore/QByteArray>
#include <QtCore/QProcess>
#include <QtCore/QtGlobal>

#include "operators/framework/rs_operator_context.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <future>
#include <thread>

using namespace exprs;

#ifndef SICNU_TEST_HELLO_PLUGIN_DIR
#error "SICNU_TEST_HELLO_PLUGIN_DIR must point at the built fixture plugin dir"
#endif

namespace {
/// Platform-correct payload name of the hello fixture MODULE (the CMake
/// target builds libhello_plugin.so on POSIX and libhello_plugin.dll on
/// Windows; a manifest naming the wrong extension fails validation with
/// EntrypointMissing before any code runs).
#ifdef _WIN32
const char *kHelloEntrypoint = "libhello_plugin.dll";
#elif defined( __APPLE__ )
const char *kHelloEntrypoint = "libhello_plugin.dylib";
#else
const char *kHelloEntrypoint = "libhello_plugin.so";
#endif

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
    bool waitPluginIdle( const std::string &, int ) override
    {
        // Drain barrier override for the busy-plugin upgrade leg: when
        // neverIdle is set the plugin never quiesces, so unload() — and
        // every drain-gated lifecycle operation built on it — refuses.
        return !neverIdle;
    }

    std::vector<std::string> operatorIds;
    std::map<std::string, std::function<std::unique_ptr<sicnu::operators::RSOperator>()>> factories;
    bool neverIdle = false;
};
} // namespace

TEST_CASE( "registry load of a native plugin registers a working factory",
           "[plugin][loader][regression]" )
{
    // Guards against the dead-code regression where the native load path was
    // unreachable: registry reported Loaded but the sink never received the
    // operator factory ("operator factory returned nullptr" at execute).
    writeManifest( SICNU_TEST_HELLO_PLUGIN_DIR, kHelloEntrypoint, pluginAbiVersion() );

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

// WP6 (plugin-platform 12.0): package metadata (SBOM path, signature
// section) is CARRIED metadata with typed validation. Runs on every platform
// (the POSIX-only test_exprs_plugin_system lane covers install rollback).
TEST_CASE( "package signature and SBOM metadata validate typed", "[plugin][package][p12]" )
{
    const std::string root = "/tmp/exprs_test_pkgmeta_win";
    std::error_code ec;
    std::filesystem::remove_all( root, ec );
    std::filesystem::create_directories( root + "/org.test.pkgmeta", ec );
    const std::string dir = root + "/org.test.pkgmeta";

    // The package section is spliced INSIDE the root object (a trailing
    // append after the closing brace would not be JSON at all).
    auto writeManifest = [&dir]( const std::string &packageSection ) {
        std::ofstream manifest( dir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.pkgmeta",
            "name": "Package Meta",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint_kind": "manifest",
            "capabilities": ["operator"],
            "operators": [{
                "id": "pkgmeta:echo",
                "display_name": "Echo",
                "group": "test",
                "inputs": [{ "name": "text", "type": "string", "required": true }],
                "external": { "argv": ["/bin/echo", "-n", "${text}"], "timeout_seconds": 30 }
            }])"
                << ( packageSection.empty() ? std::string() : ",\n" + packageSection )
                << "\n}";
    };

    // A valid signature section installs cleanly and records the honesty
    // note (carried metadata, NOT authenticity - no trust root in v1).
    writeManifest( R"("package": {
            "sbom": { "format": "cyclonedx", "path": "sbom.json" },
            "signature": { "algorithm": "sha256", "value": "deadbeef", "keyId": "dev" }
        })" );
    {
        PluginDiagnosticLog log;
        std::string installed;
        REQUIRE( PluginPackage::install( dir, installed, log ) );
        REQUIRE_FALSE( installed.empty() );
        bool sawSignatureNote = false;
        for ( const PluginDiagnostic &item : log.items() )
        {
            if ( item.message.find( "signature metadata" ) != std::string::npos
                 && item.message.find( "NOT authenticity" ) != std::string::npos )
                sawSignatureNote = true;
        }
        REQUIRE( sawSignatureNote );
        REQUIRE( PluginPackage::uninstall( "org.test.pkgmeta", log ) );
    }

    // A traversal-typed SBOM path is a typed WARNING (never resolved), and
    // install still succeeds: the section is informational.
    writeManifest( R"("package": { "sbom": { "format": "spdx", "path": "../../etc/passwd" } })" );
    {
        PluginDiagnosticLog log;
        std::string installed;
        REQUIRE( PluginPackage::install( dir, installed, log ) );
        bool sawTraversalWarning = false;
        for ( const PluginDiagnostic &item : log.items() )
        {
            if ( item.code == PluginDiagnosticCode::EntrypointOutsideRoot
                 && item.field == "package.sbom.path" )
                sawTraversalWarning = true;
        }
        REQUIRE( sawTraversalWarning );
        REQUIRE( PluginPackage::uninstall( "org.test.pkgmeta", log ) );
    }

    // Wrong-typed signature fields are typed warnings, not aborts (#1038
    // totality rule: a malformed informational section must not kill the
    // install transaction).
    writeManifest( R"("package": { "signature": { "algorithm": 7, "value": true } })" );
    {
        PluginDiagnosticLog log;
        std::string installed;
        REQUIRE( PluginPackage::install( dir, installed, log ) );
        bool sawTypeWarning = false;
        for ( const PluginDiagnostic &item : log.items() )
        {
            if ( item.code == PluginDiagnosticCode::ManifestInvalidField
                 && item.field == "package.signature.algorithm" )
                sawTypeWarning = true;
        }
        REQUIRE( sawTypeWarning );
        REQUIRE( PluginPackage::uninstall( "org.test.pkgmeta", log ) );
    }

    // A non-object signature section is ignored with a typed warning.
    writeManifest( R"("package": { "signature": "not-an-object" })" );
    {
        PluginDiagnosticLog log;
        std::string installed;
        REQUIRE( PluginPackage::install( dir, installed, log ) );
        bool sawShapeWarning = false;
        for ( const PluginDiagnostic &item : log.items() )
        {
            if ( item.code == PluginDiagnosticCode::ManifestInvalidField
                 && item.field == "package.signature" )
                sawShapeWarning = true;
        }
        REQUIRE( sawShapeWarning );
        REQUIRE( PluginPackage::uninstall( "org.test.pkgmeta", log ) );
    }

    std::filesystem::remove_all( root, ec );
}

TEST_CASE( "manifest gate rejects ABI mismatch before dlopen", "[plugin][loader]" )
{
    writeManifest( SICNU_TEST_HELLO_PLUGIN_DIR, kHelloEntrypoint, 999 );
    PluginDiagnosticLog log;
    PluginRecord record = PluginDiscovery::inspectDirectory( SICNU_TEST_HELLO_PLUGIN_DIR, log );
    REQUIRE( record.state == PluginState::Incompatible );
    bool sawAbi = false;
    for ( const auto &item : log.items() )
        sawAbi = sawAbi || item.code == PluginDiagnosticCode::AbiVersionMismatch;
    REQUIRE( sawAbi );
}

// WP2 (plugin-platform 12.0): granted permissions carry the same structured
// audit evidence as denied ones. A manifest-kind plugin (no binary) is the
// smallest loadable unit, so this runs on every platform.
TEST_CASE( "granted permissions carry audit events in both policy modes",
           "[plugin][permissions][p12]" )
{
    const std::string root = "/tmp/exprs_test_audit_win";
    std::error_code ec;
    std::filesystem::remove_all( root, ec );
    std::filesystem::create_directories( root + "/org.test.audit", ec );
    {
        std::ofstream manifest( root + "/org.test.audit/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.audit",
            "name": "Audit",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint_kind": "manifest",
            "capabilities": ["operator", "external_tools"],
            "permissions": ["external_process", "filesystem_read"],
            "operators": [{
                "id": "audit:echo",
                "display_name": "Echo",
                "group": "test",
                "inputs": [{ "name": "text", "type": "string", "required": true }],
                "external": { "argv": ["/bin/echo", "-n", "${text}"], "timeout_seconds": 30 }
            }]
        })";
    }

    auto countGrants = []( const PluginDiagnosticLog &log, const std::string &pluginId ) {
        int grants = 0;
        for ( const PluginDiagnostic &item : log.items() )
        {
            if ( item.pluginId == pluginId
                 && item.code == PluginDiagnosticCode::PermissionGranted )
                ++grants;
        }
        return grants;
    };

    PluginRegistry &registry = PluginRegistry::instance();
    registry.setContributionSink( nullptr );

    // Default policy mode is Audit: the granted path still records evidence.
    // configure() rescans and clears the diagnostic log, so each block below
    // counts only its own events.
    {
        PluginRegistryOptions options;
        options.roots = { root };
        options.policy = PluginPolicy::fromEnvironment();
        options.policy.mode = PluginPolicyMode::Audit;
        registry.configure( options );
        const PluginRecord *record = registry.record( "org.test.audit" );
        REQUIRE( record != nullptr );
        REQUIRE( record->manifest.permissions.size() == 2 );
        REQUIRE( registry.load( "org.test.audit" ) );
        REQUIRE( countGrants( registry.diagnostics(), "org.test.audit" ) == 2 );
        for ( const PluginDiagnostic &item : registry.diagnostics().items() )
        {
            if ( item.code == PluginDiagnosticCode::PermissionGranted )
                REQUIRE( PluginDiagnostic::codeString( item.code ) == "E5006" );
        }
        REQUIRE( registry.unload( "org.test.audit" ) );

        // A refused load grants nothing (the disable persists across the
        // reconfigure that resets the log).
        registry.setEnabled( "org.test.audit", false );
        registry.configure( options );
        REQUIRE_FALSE( registry.load( "org.test.audit" ) );
        REQUIRE( countGrants( registry.diagnostics(), "org.test.audit" ) == 0 );
        registry.setEnabled( "org.test.audit", true );
    }

    // Enforce mode grants the same declared permissions (declared = allowed).
    {
        PluginRegistryOptions options;
        options.roots = { root };
        options.policy = PluginPolicy::fromEnvironment();
        options.policy.mode = PluginPolicyMode::Enforce;
        registry.configure( options );
        REQUIRE( registry.load( "org.test.audit" ) );
        REQUIRE( countGrants( registry.diagnostics(), "org.test.audit" ) == 2 );
        for ( const PluginDiagnostic &item : registry.diagnostics().items() )
        {
            if ( item.code == PluginDiagnosticCode::PermissionGranted )
                REQUIRE( item.message.find( "policy mode 'enforce'" ) != std::string::npos );
        }
        REQUIRE( registry.unload( "org.test.audit" ) );
    }
    registry.unloadAll();
    registry.setContributionSink( nullptr );
    std::filesystem::remove_all( root, ec );
}

// ---------------------------------------------------------------------------
// WP4: dev-mode hot reload (plugin-platform 12.0)
// ---------------------------------------------------------------------------
namespace {
/// Loads the hello fixture into the registry the same way the production
/// shell does, and restores a clean manifest for later tests on return.
struct ReloadFixture
{
    PluginRegistry &registry = PluginRegistry::instance();
    RecordingSink sink;
    const std::string id = "org.exprs.test.hello-plugin";
    std::string pluginDir{ std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) };

    ReloadFixture()
    {
        writeManifest( pluginDir, kHelloEntrypoint, pluginAbiVersion() );
        PluginRegistryOptions options;
        options.roots = { pluginDir + "/.." };
        options.policy.allowThirdPartyNative = true;
        options.policy.devMode = true;
        registry.setContributionSink( &sink );
        registry.configure( options );
        registry.setEnabled( id, true );
        REQUIRE( registry.load( id ) );
        REQUIRE( sink.factories.count( "test:hello" ) == 1 );
    }
    ~ReloadFixture()
    {
        registry.unloadAll();
        registry.setContributionSink( nullptr );
        writeManifest( pluginDir, kHelloEntrypoint, pluginAbiVersion() );
        std::error_code ec;
        std::filesystem::remove_all( pluginDir + "/libnotreally.so", ec );
        // The real rollback source lives in the registry snapshot root
        // (<temp>/sicnu-plugin-snapshots/last-good-<id>, track 13.0 layout);
        // remove the whole root so the next test starts clean and the
        // dev-mode temp tree does not accumulate. The pre-13.0 flat name
        // is dropped too in case an older run left one.
        const std::string temp =
            std::filesystem::temp_directory_path().generic_string();
        std::filesystem::remove_all( temp + "/sicnu-plugin-snapshots", ec );
        std::filesystem::remove_all( temp + "/plugin-last-good-" + id, ec );
    }

    PluginRegistry::ReloadOptions devOptions()
    {
        PluginRegistry::ReloadOptions options;
        options.devMode = true;
        return options;
    }
    bool sawCode( PluginDiagnosticCode code ) const
    {
        for ( const PluginDiagnostic &item : registry.diagnostics().items() )
        {
            if ( item.pluginId == id && item.code == code )
                return true;
        }
        return false;
    }
};
} // namespace

TEST_CASE( "hot reload is refused unless BOTH the caller and host policy allow dev mode",
           "[plugin][reload][p12]" )
{
    ReloadFixture fixture;
    PluginRegistry &registry = fixture.registry;

    // Caller asks, host policy says no (production default): refused.
    {
        PluginRegistryOptions options;
        options.roots = { fixture.pluginDir + "/.." };
        options.policy.allowThirdPartyNative = true;
        options.policy.devMode = false;
        registry.configure( options );
        registry.setEnabled( fixture.id, true );
        REQUIRE( registry.isLoaded( fixture.id ) );
        REQUIRE_FALSE( registry.reload( fixture.id, fixture.devOptions() ) );
        REQUIRE( fixture.sawCode( PluginDiagnosticCode::TrustRejected ) );
        REQUIRE( registry.isLoaded( fixture.id ) );          // old version intact
        REQUIRE( fixture.sink.factories.count( "test:hello" ) == 1 );
    }
    // Host policy allows, caller does not ask: refused (explicit opt-in).
    {
        PluginRegistryOptions options;
        options.roots = { fixture.pluginDir + "/.." };
        options.policy.allowThirdPartyNative = true;
        options.policy.devMode = true;
        registry.configure( options );
        registry.setEnabled( fixture.id, true );
        REQUIRE( registry.isLoaded( fixture.id ) );
        PluginRegistry::ReloadOptions silent;
        silent.devMode = false;
        REQUIRE_FALSE( registry.reload( fixture.id, silent ) );
        REQUIRE( fixture.sawCode( PluginDiagnosticCode::TrustRejected ) );
        REQUIRE( registry.isLoaded( fixture.id ) );
    }
    // Not loaded: refused typed.
    {
        REQUIRE( registry.unload( fixture.id ) );
        REQUIRE_FALSE( registry.reload( fixture.id, fixture.devOptions() ) );
        // The refusal is TYPED for THIS reason: an unloaded plugin is refused
        // with a TrustRejected record naming the state, not a silent false.
        bool sawNotLoadedRefusal = false;
        for ( const PluginDiagnostic &item : registry.diagnostics().items() )
        {
            if ( item.pluginId == fixture.id
                 && item.code == PluginDiagnosticCode::TrustRejected
                 && item.message.find( "not loaded" ) != std::string::npos )
                sawNotLoadedRefusal = true;
        }
        REQUIRE( sawNotLoadedRefusal );
    }
}

TEST_CASE( "hot reload swaps in a valid new manifest and refuses a broken one",
           "[plugin][reload][p12]" )
{
    ReloadFixture fixture;
    PluginRegistry &registry = fixture.registry;

    // A structurally broken manifest refuses the reload with the running
    // version untouched — no snapshot, no unload, no dlopen.
    {
        std::ofstream broken( fixture.pluginDir + "/plugin.json", std::ios::trunc );
        broken << "{ not json";
        broken.close();
        REQUIRE_FALSE( registry.reload( fixture.id, fixture.devOptions() ) );
        REQUIRE( registry.isLoaded( fixture.id ) );
        REQUIRE( fixture.sink.factories.count( "test:hello" ) == 1 );
        // The refusal is TYPED: the parse failure's own code, not a guess.
        REQUIRE( fixture.sawCode( PluginDiagnosticCode::ManifestInvalidJson ) );
    }

    // A manifest whose declared host API range excludes this host is refused
    // BEFORE execution (typed, same gate as fresh installs).
    {
        std::ofstream incompatible( fixture.pluginDir + "/plugin.json", std::ios::trunc );
        incompatible << R"({
            "manifest_version": 1,
            "id": "org.exprs.test.hello-plugin",
            "name": "Hello Fixture",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "min_host_api": ")" << ( pluginApiVersion().major ) << "."
                   << ( pluginApiVersion().minor + 50 ) << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint": ")" << kHelloEntrypoint << R"(",
            "entrypoint_kind": "native",
            "capabilities": ["operator"]
        })";
        incompatible.close();
        REQUIRE_FALSE( registry.reload( fixture.id, fixture.devOptions() ) );
        REQUIRE( registry.isLoaded( fixture.id ) );
        REQUIRE( fixture.sawCode( PluginDiagnosticCode::ApiVersionMismatch ) );
    }

    // A valid new manifest loads: the factory registry reflects the new one.
    {
        std::ofstream fresh( fixture.pluginDir + "/plugin.json", std::ios::trunc );
        fresh << R"({
            "manifest_version": 1,
            "id": "org.exprs.test.hello-plugin",
            "name": "Hello Fixture v2",
            "version": "2.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint": ")" << kHelloEntrypoint << R"(",
            "entrypoint_kind": "native",
            "capabilities": ["operator"],
            "operators": [{ "id": "test:hello_v2", "display_name": "Test Hello v2", "group": "test" }]
        })";
        fresh.close();
        const bool reloaded = registry.reload( fixture.id, fixture.devOptions() );
        REQUIRE( reloaded );
        REQUIRE( registry.isLoaded( fixture.id ) );
        // The registry now tracks the NEW manifest: v2 name/version and the
        // v2-declared operator id (the fixture binary itself registers the
        // hard-coded "test:hello" factory either way — that is the payload's
        // business, not the manifest's).
        const PluginRecord *after = registry.record( fixture.id );
        REQUIRE( after != nullptr );
        REQUIRE( after->manifest.version == "2.0.0" );
        REQUIRE( after->manifest.name == "Hello Fixture v2" );
        REQUIRE( after->manifest.operators.size() == 1 );
        REQUIRE( after->manifest.operators[0].id == "test:hello_v2" );
        // The reload re-registered the plugin's contributions into the sink
        // (the unload revoked the previous ones).
        REQUIRE( fixture.sink.factories.count( "test:hello" ) == 1 );
    }
}

TEST_CASE( "hot reload rolls back to the snapshot when the new code cannot load",
           "[plugin][reload][p12]" )
{
    ReloadFixture fixture;
    PluginRegistry &registry = fixture.registry;

    // The dev-mode last-good capture is ASYNC (13.0): let the worker finish
    // before the "dev's next edit" lands, mirroring real usage where the
    // capture completes during the editing gap. A capture that still races
    // an edit is caught by the manifest-identity gate and honestly refused.
    // (completion 13/15: the dev snapshot is pid-attributed — this process's
    // own, so snapshotOwnerPid() names the directory.)
    {
        const std::string marker =
            std::filesystem::temp_directory_path().generic_string()
            + "/sicnu-plugin-snapshots/last-good-" + fixture.id
            + "-" + std::to_string( snapshotOwnerPid() )
            + "/snapshot.marker.json";
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
        while ( !std::filesystem::exists( marker )
                && std::chrono::steady_clock::now() < deadline )
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        REQUIRE( std::filesystem::exists( marker ) );
    }

    // The new manifest passes validation (the entrypoint exists as a regular
    // file) but the payload is not a loadable library — this is the path that
    // reaches dlopen failure and must roll back to the working bytes.
    {
        std::ofstream bogus( fixture.pluginDir + "/libnotreally.so", std::ios::binary );
        bogus << "this is not a shared library";
        bogus.close();
        std::ofstream manifest( fixture.pluginDir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.exprs.test.hello-plugin",
            "name": "Hello Fixture broken",
            "version": "2.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint": "libnotreally.so",
            "entrypoint_kind": "native",
            "capabilities": ["operator"]
        })";
        manifest.close();

        const bool reloaded2 = registry.reload( fixture.id, fixture.devOptions() );
        REQUIRE_FALSE( reloaded2 );
        REQUIRE( fixture.sawCode( PluginDiagnosticCode::LibraryLoadFailed ) );
        REQUIRE( fixture.sawCode( PluginDiagnosticCode::PluginReloadRolledBack ) );
    }

    // The rollback restored the exact old bytes: the manifest on disk is the
    // working one again and the plugin is loaded from it.
    {
        PluginDiagnosticLog log;
        PluginRecord record = PluginDiscovery::inspectDirectory( fixture.pluginDir, log );
        REQUIRE( record.state == PluginState::Validated );
        REQUIRE( record.manifest.version == "1.0.0" );
        REQUIRE( record.manifest.entrypoint == kHelloEntrypoint );
        REQUIRE( registry.isLoaded( fixture.id ) );
        REQUIRE( fixture.sink.factories.count( "test:hello" ) == 1 );
        std::error_code ec;
        REQUIRE_FALSE( std::filesystem::exists( fixture.pluginDir + "/libnotreally.so", ec ) );
        REQUIRE_FALSE( std::filesystem::exists( fixture.pluginDir + "/.reload-snapshot", ec ) );
    }
}

TEST_CASE( "hot reload aborts on a failed state migration with the old version loaded",
           "[plugin][reload][p12]" )
{
    ReloadFixture fixture;
    PluginRegistry &registry = fixture.registry;
    PluginRegistry::ReloadOptions options = fixture.devOptions();
    bool migrationRan = false;
    options.migrateState = [&migrationRan]( const std::string &pluginDir ) {
        (void)pluginDir;
        migrationRan = true;
        return false; // old state cannot be migrated
    };
    REQUIRE_FALSE( registry.reload( fixture.id, options ) );
    REQUIRE( migrationRan );
    REQUIRE( registry.isLoaded( fixture.id ) );
    REQUIRE( fixture.sink.factories.count( "test:hello" ) == 1 );
    // The manifest was NOT touched: the migration runs before the unload.
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::InitializationFailed ) );
}

TEST_CASE( "loader drives the full native plugin lifecycle", "[plugin][loader]" )
{
    writeManifest( SICNU_TEST_HELLO_PLUGIN_DIR, kHelloEntrypoint, pluginAbiVersion() );

    PluginDiagnosticLog log;
    PluginRecord record = PluginDiscovery::inspectDirectory( SICNU_TEST_HELLO_PLUGIN_DIR, log );
    REQUIRE( record.state == PluginState::Validated );

    // Entrypoint probe (plugin doctor surface) — no code executed.
    std::string probeError;
    const bool probeOk =
        PluginLoader::probeEntrypoint( record.directory + "/" + record.manifest.entrypoint,
                                       probeError );
    REQUIRE( probeOk );

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
    writeManifest( SICNU_TEST_HELLO_PLUGIN_DIR, kHelloEntrypoint, pluginAbiVersion() );
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

TEST_CASE( "registry unload drops the lock across the sink revoke (issue #1156)",
           "[plugin][registry][lockdrop][issue1156]" )
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "exprs_test_lockdrop_revoke";
    fs::remove_all( root );
    const fs::path pluginDir = root / "org.test.gated";
    fs::create_directories( pluginDir );
    {
        std::ofstream manifest( ( pluginDir / "plugin.json" ).string(), std::ios::trunc );
        // Hardening 15/20: the fixture drifted behind the validator — the
        // missing capabilities block and the missing operator `external`
        // section made configure() mark the plugin Broken BEFORE the test
        // body ran, so the #1156 AB-BA regression oracle never executed
        // (load failed on the first REQUIRE).
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.gated",
            "name": "Gated",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "capabilities": ["operator", "external_tools"],
            "permissions": ["external_process", "filesystem_read"],
            "operators": [{ "id": "test:gated", "display_name": "Gated", "group": "test",
                "inputs": [{ "name": "text", "type": "string", "required": true }],
                "external": { "argv": ["/bin/echo", "-n", "ECHO:", "${text}"],
                              "timeout_seconds": 30 } }]
        })";
    }

    // Sink whose revokePlugin blocks until a "bootstrap finished" gate is
    // set — modelling PluginRuntimeHost::revokePluginContributions taking
    // the host mutex while bootstrap still holds it across configure().
    class GatedSink : public RecordingSink
    {
    public:
        std::atomic<bool> *gate = nullptr;
        void revokePlugin( const std::string &pluginId ) override
        {
            const auto deadline = std::chrono::steady_clock::now()
                                  + std::chrono::seconds( 5 );
            while ( gate && !gate->load()
                    && std::chrono::steady_clock::now() < deadline )
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            RecordingSink::revokePlugin( pluginId );
        }
    };

    std::atomic<bool> bootstrapDone{ false };
    GatedSink sink;
    sink.gate = &bootstrapDone;

    PluginRegistryOptions options;
    options.roots = { root.generic_string() };
    PluginRegistry &registry = PluginRegistry::instance();
    registry.setContributionSink( &sink );
    registry.configure( options );
    REQUIRE( registry.load( "org.test.gated" ) );

    // Thread B: unload reaches the sink revoke. Pre-#1156 it held the
    // registry mutex across the call, so the concurrent configure() below
    // could never finish and open the gate — the documented AB-BA.
    std::future<bool> unloadDone = std::async( std::launch::async,
        [ &registry ] { return registry.unload( "org.test.gated" ); } );
    // Let the unload reach the revoke (it may also win the race before it;
    // the configure leg is the one that must never be blocked behind it).
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

    // Thread A: bootstrap's configure() takes the registry mutex, then
    // opens the gate so the revoke can finish.
    std::future<bool> bootstrapDoneFuture = std::async( std::launch::async, [ & ] {
        registry.configure( options );
        bootstrapDone.store( true );
        return true;
    } );

    REQUIRE( bootstrapDoneFuture.wait_for( std::chrono::seconds( 5 ) )
             == std::future_status::ready );
    REQUIRE( bootstrapDoneFuture.get() );
    REQUIRE( unloadDone.wait_for( std::chrono::seconds( 5 ) )
             == std::future_status::ready );
    REQUIRE( unloadDone.get() );

    registry.unloadAll();
    registry.setContributionSink( nullptr );
    fs::remove_all( root );
}

// ---------------------------------------------------------------------------
// Track 13.0 WP1/WP2/WP3: atomic install-time upgrade, bounded snapshots, GC
// ---------------------------------------------------------------------------
namespace {
/// Registry + filesystem stage for upgrade tests: a private user plugin
/// root (SICNU_PLUGIN_USER_ROOT) and a private temp dir, so the snapshot
/// root (<temp>/sicnu-plugin-snapshots) is fully contained and GC claims
/// are checkable by listing one directory.
struct UpgradeFixture
{
    PluginRegistry &registry = PluginRegistry::instance();
    RecordingSink sink;
    const std::string id = "org.test.upgrade";
    const std::string root;
    const std::string userRoot;
    const std::string snapshotRoot;

    UpgradeFixture()
        : root( ( std::filesystem::temp_directory_path()
                  / "exprs_test_upgrade" )
                    .generic_string() )
        , userRoot( root + "/user-plugins" )
        , snapshotRoot( root + "/sicnu-plugin-snapshots" )
    {
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        std::filesystem::create_directories( userRoot, ec );
        qputenv( "SICNU_PLUGIN_USER_ROOT", QByteArray::fromStdString( userRoot ) );
        PluginRegistryOptions options;
        options.roots = { userRoot };
        options.tempDirectory = root; // snapshot root lands inside it
        options.policy.allowThirdPartyNative = true;
        registry.setContributionSink( &sink );
        registry.configure( options );
    }
    ~UpgradeFixture()
    {
        registry.unloadAll();
        registry.setContributionSink( nullptr );
        qunsetenv( "SICNU_PLUGIN_USER_ROOT" );
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
    }

    /// Writes a package source dir: manifest-kind (loads with no binary)
    /// when loadable, native + host-process runtime when not — the file
    /// exists so VALIDATION passes, but load() fails deterministically
    /// because the test process installs no host-process runtime.
    void writePackage( const std::string &name, const std::string &version,
                       bool loadable ) const
    {
        const std::string dir = root + "/" + name;
        std::error_code ec;
        std::filesystem::create_directories( dir, ec );
        std::ofstream manifest( dir + "/plugin.json", std::ios::trunc );
        if ( loadable )
        {
            manifest << R"({
                "manifest_version": 1,
                "id": ")" + id + R"(",
                "name": "Upgrade Fixture",
                "version": ")" + version + R"(",
                "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
                "abi_version": )" << pluginAbiVersion() << R"(,
                "entrypoint_kind": "manifest",
                "capabilities": ["operator", "external_tools"],
                "permissions": ["external_process", "filesystem_read"],
                "operators": [{
                    "id": "up:echo",
                    "display_name": "Echo",
                    "group": "test",
                    "inputs": [{ "name": "text", "type": "string", "required": true }],
                    "external": { "argv": ["/bin/echo", "-n", "${text}"], "timeout_seconds": 30 }
                }]
            })";
        }
        else
        {
            manifest << R"({
                "manifest_version": 1,
                "id": ")" + id + R"(",
                "name": "Upgrade Fixture unloadable",
                "version": ")" + version + R"(",
                "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
                "abi_version": )" << pluginAbiVersion() << R"(,
                "runtime": "host-process",
                "entrypoint": "libbroken.so",
                "entrypoint_kind": "native",
                "capabilities": ["operator"],
                "permissions": ["external_process"],
                "operators": [{ "id": "up:broken", "display_name": "Broken", "group": "test" }]
            })";
            std::ofstream lib( dir + "/libbroken.so", std::ios::binary );
            lib << "not a shared library";
        }
    }

    std::string targetDir() const { return userRoot + "/" + id; }

    std::string installedVersion() const
    {
        PluginDiagnosticLog log;
        const PluginRecord record =
            PluginDiscovery::inspectDirectory( targetDir(), log );
        return record.manifest.version;
    }
    std::vector<PluginPermission> installedPermissions() const
    {
        PluginDiagnosticLog log;
        const PluginRecord record =
            PluginDiscovery::inspectDirectory( targetDir(), log );
        return record.manifest.permissions;
    }
    /// Capability set as a sorted list (manifest order is not a contract —
    /// provenance equality compares as sets).
    std::vector<std::string> installedCapabilities() const
    {
        PluginDiagnosticLog log;
        const PluginRecord record =
            PluginDiscovery::inspectDirectory( targetDir(), log );
        std::vector<std::string> caps = record.manifest.capabilities;
        std::sort( caps.begin(), caps.end() );
        return caps;
    }

    bool sawCode( PluginDiagnosticCode code ) const
    {
        for ( const PluginDiagnostic &item : registry.diagnostics().items() )
        {
            if ( item.pluginId == id && item.code == code )
                return true;
        }
        return false;
    }
    /// Every entry directly inside the snapshot root (for residue claims).
    std::vector<std::string> snapshotRootEntries() const
    {
        std::vector<std::string> names;
        std::error_code ec;
        for ( const auto &entry :
              std::filesystem::directory_iterator( snapshotRoot, ec ) )
            names.push_back( entry.path().filename().generic_string() );
        return names;
    }
};
} // namespace

TEST_CASE( "installOrUpgrade installs a fresh package, then atomically upgrades it",
           "[plugin][upgrade][p13]" )
{
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;

    // Fresh install path: nothing at the target yet.
    fixture.writePackage( "src-v1", "1.0.0", true );
    const PluginRegistry::PluginUpgradeResult first =
        registry.installOrUpgrade( fixture.root + "/src-v1" );
    REQUIRE( first.status == PluginRegistry::PluginUpgradeStatus::Installed );
    REQUIRE( first.installedDir == fixture.targetDir() );
    REQUIRE( fixture.installedVersion() == "1.0.0" );
    REQUIRE( registry.record( fixture.id ) != nullptr );

    REQUIRE( registry.load( fixture.id ) );

    // The upgrade: drain the loaded v1, atomic swap, load v2, commit.
    fixture.writePackage( "src-v2", "2.0.0", true );
    const PluginRegistry::PluginUpgradeResult second =
        registry.installOrUpgrade( fixture.root + "/src-v2" );
    REQUIRE( second.status == PluginRegistry::PluginUpgradeStatus::Upgraded );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::PluginUpgraded ) );

    // The published generation is v2 — record AND on-disk bytes agree.
    const PluginRecord *record = registry.record( fixture.id );
    REQUIRE( record != nullptr );
    REQUIRE( record->manifest.version == "2.0.0" );
    REQUIRE( fixture.installedVersion() == "2.0.0" );
    REQUIRE( registry.isLoaded( fixture.id ) );

    // WP3: the per-upgrade snapshot is consumed — nothing but (possibly)
    // the dev last-good tree may remain under the snapshot root. With
    // devMode off there is no last-good either: the root is EMPTY.
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "installOrUpgrade refuses a bad manifest before touching the install",
           "[plugin][upgrade][p13]" )
{
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );
    REQUIRE( registry.load( fixture.id ) );

    const std::string dir = fixture.root + "/src-bad";
    std::error_code ec;
    std::filesystem::create_directories( dir, ec );
    { std::ofstream broken( dir + "/plugin.json", std::ios::trunc ); broken << "{ not json"; }

    const PluginRegistry::PluginUpgradeResult refused =
        registry.installOrUpgrade( dir );
    REQUIRE( refused.status == PluginRegistry::PluginUpgradeStatus::Refused );
    // Old version untouched: still loaded, still v1 bytes on disk.
    REQUIRE( registry.isLoaded( fixture.id ) );
    REQUIRE( fixture.installedVersion() == "1.0.0" );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "installOrUpgrade aborts on a failed state migration with v1 untouched",
           "[plugin][upgrade][p13]" )
{
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );
    REQUIRE( registry.load( fixture.id ) );
    fixture.writePackage( "src-v2", "2.0.0", true );

    PluginRegistry::PluginUpgradeOptions options;
    bool migrationRan = false;
    options.migrateState = [&migrationRan]( const std::string & ) {
        migrationRan = true;
        return false;
    };
    const PluginRegistry::PluginUpgradeResult refused =
        registry.installOrUpgrade( fixture.root + "/src-v2", options );
    REQUIRE( refused.status == PluginRegistry::PluginUpgradeStatus::Refused );
    REQUIRE( migrationRan );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::InitializationFailed ) );
    REQUIRE( registry.isLoaded( fixture.id ) );
    REQUIRE( fixture.installedVersion() == "1.0.0" );
    REQUIRE( fixture.snapshotRootEntries().empty() ); // snapshot GC'd on refusal
}

TEST_CASE( "installOrUpgrade rolls back when the new version cannot load",
           "[plugin][upgrade][p13]" )
{
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );
    REQUIRE( registry.load( fixture.id ) );

    // v2 validates (entrypoint file exists) but load() fails: the manifest
    // asks for the host-process runtime and none is installed here. This is
    // the deterministic stand-in for "new binary fails to load / worker
    // dies during the swap" — the transaction must restore v1 exactly.
    fixture.writePackage( "src-v2", "2.0.0", false );
    const PluginRegistry::PluginUpgradeResult rolled =
        registry.installOrUpgrade( fixture.root + "/src-v2" );
    REQUIRE( rolled.status == PluginRegistry::PluginUpgradeStatus::RolledBack );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::PluginUpgradeRolledBack ) );

    // The rollback restored the exact old bytes AND the old generation is
    // the running one — version, permissions and capabilities are v1's.
    REQUIRE( fixture.installedVersion() == "1.0.0" );
    REQUIRE( fixture.installedPermissions()
             == std::vector<PluginPermission>{ PluginPermission::ExternalProcess,
                                               PluginPermission::FilesystemRead } );
    REQUIRE( fixture.installedCapabilities()
             == std::vector<std::string>{ "external_tools", "operator" } );
    REQUIRE( registry.isLoaded( fixture.id ) );
    const PluginRecord *record = registry.record( fixture.id );
    REQUIRE( record != nullptr );
    REQUIRE( record->manifest.version == "1.0.0" );
    // Rollback consumed the snapshot; the upgrade backup is gone.
    for ( const std::string &name : fixture.snapshotRootEntries() )
        REQUIRE( name.find( "upgrade-" ) == std::string::npos );
}

TEST_CASE( "installOrUpgrade refuses the drain when the plugin is busy",
           "[plugin][upgrade][p13]" )
{
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );
    REQUIRE( registry.load( fixture.id ) );
    fixture.writePackage( "src-v2", "2.0.0", true );

    fixture.sink.neverIdle = true; // the drain barrier never goes idle
    const PluginRegistry::PluginUpgradeResult refused =
        registry.installOrUpgrade( fixture.root + "/src-v2" );
    fixture.sink.neverIdle = false;
    REQUIRE( refused.status == PluginRegistry::PluginUpgradeStatus::Refused );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::PluginInUse ) );
    REQUIRE( registry.isLoaded( fixture.id ) ); // old version keeps running
    REQUIRE( fixture.installedVersion() == "1.0.0" );
}

TEST_CASE( "uninstallPlugin drains, removes the package AND its snapshot",
           "[plugin][upgrade][p13]" )
{
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );
    REQUIRE( registry.load( fixture.id ) );

    // A dev-mode load leaves a pid-attributed last-good snapshot (completion
    // 13/15); emulate it deterministically (devMode is off here — drop a
    // marked snapshot in directly), plus the pre-attribution legacy layout
    // an uninstall must also clean.
    const std::string lastGood = fixture.snapshotRoot + "/last-good-" + fixture.id + "-"
                                 + std::to_string( snapshotOwnerPid() );
    const std::string legacyLastGood = fixture.snapshotRoot + "/last-good-" + fixture.id;
    {
        PluginSnapshotBudget budget;
        const PluginSnapshotResult snap = capturePluginSnapshot(
            fixture.targetDir(), lastGood, fixture.id, budget );
        REQUIRE( snap.ok() );
    }
    {
        std::error_code err;
        std::filesystem::create_directories( legacyLastGood, err );
        std::ofstream( legacyLastGood + "/x" ) << "x";
    }
    REQUIRE( std::filesystem::exists( lastGood ) );
    REQUIRE( std::filesystem::exists( legacyLastGood ) );

    REQUIRE( registry.uninstallPlugin( fixture.id ) );
    REQUIRE_FALSE( std::filesystem::exists( fixture.targetDir() ) );
    REQUIRE_FALSE( std::filesystem::exists( lastGood ) ); // no orphan
    REQUIRE_FALSE( std::filesystem::exists( legacyLastGood ) ); // legacy swept too
}

TEST_CASE( "snapshot capture is bounded, verified and fail-closed",
           "[plugin][snapshot][p13]" )
{
    namespace fs = std::filesystem;
    const std::string root =
        ( fs::temp_directory_path() / "exprs_test_snapshot" ).generic_string();
    std::error_code ec;
    fs::remove_all( root, ec );
    fs::create_directories( root + "/src/sub", ec );
    { std::ofstream f( root + "/src/a.txt" ); f << "alpha"; }
    { std::ofstream f( root + "/src/sub/b.txt" ); f << "beta-gamma"; }
    const std::string dest = root + "/snapshots/last-good-org.test.snap";
    fs::create_directories( root + "/snapshots", ec );

    // Happy path: capture publishes atomically with a marker that verifies.
    {
        PluginSnapshotBudget budget;
        const PluginSnapshotResult snap =
            capturePluginSnapshot( root + "/src", dest, "org.test.snap", budget );
        REQUIRE( snap.ok() );
        REQUIRE( snap.files == 2 );
        std::string error;
        REQUIRE( verifyPluginSnapshot( dest, "org.test.snap", error ) );
    }
    // Tampered payload: deleting a file makes the marker lie — verify fails.
    {
        fs::remove( dest + "/a.txt", ec );
        std::string error;
        REQUIRE_FALSE( verifyPluginSnapshot( dest, "org.test.snap", error ) );
    }
    // Wrong plugin id is rejected (a snapshot is never mis-attributed).
    {
        std::string error;
        REQUIRE_FALSE( verifyPluginSnapshot( root + "/src", "org.test.snap", error ) );
    }
    // Budget bounds are enforced BEFORE the dest is touched.
    {
        PluginSnapshotBudget tight;
        tight.maxFiles = 1;
        const PluginSnapshotResult over = capturePluginSnapshot(
            root + "/src", root + "/snapshots/over", "org.test.snap", tight );
        REQUIRE( over.status == PluginSnapshotStatus::BudgetExceeded );
        REQUIRE_FALSE( fs::exists( root + "/snapshots/over", ec ) );
        tight.maxFiles = 64;
        tight.maxBytes = 8;
        const PluginSnapshotResult bytes = capturePluginSnapshot(
            root + "/src", root + "/snapshots/over2", "org.test.snap", tight );
        REQUIRE( bytes.status == PluginSnapshotStatus::BudgetExceeded );
        tight.maxBytes = 1u << 20;
        tight.maxFileBytes = 4;
        const PluginSnapshotResult perFile = capturePluginSnapshot(
            root + "/src", root + "/snapshots/over3", "org.test.snap", tight );
        REQUIRE( perFile.status == PluginSnapshotStatus::BudgetExceeded );
    }
    // Cancel is observed between files and never publishes.
    {
        PluginSnapshotBudget budget;
        const PluginSnapshotResult cancelled = capturePluginSnapshot(
            root + "/src", root + "/snapshots/cancelled", "org.test.snap", budget,
            [] { return true; } );
        REQUIRE( cancelled.status == PluginSnapshotStatus::Cancelled );
        REQUIRE_FALSE( fs::exists( root + "/snapshots/cancelled", ec ) );
    }
    // A symlinked payload entry fails closed (never followed).
    {
        std::error_code linkError;
        fs::create_symlink( fs::path( root + "/src/a.txt" ),
                            fs::path( root + "/src/link.txt" ), linkError );
        if ( !linkError )
        {
            PluginSnapshotBudget budget;
            const PluginSnapshotResult unsafe = capturePluginSnapshot(
                root + "/src", root + "/snapshots/unsafe", "org.test.snap", budget );
            REQUIRE( unsafe.status == PluginSnapshotStatus::Unsafe );
            fs::remove( root + "/src/link.txt", ec );
        }
    }
    // Markerless snapshot: a dir that never got its completion marker is
    // never trusted — verify refuses without caring about payload content
    // (O2.4's literal case; tampered-payload above covers the marker-lies
    // branch, this covers the !is_regular_file(marker) branch).
    {
        const std::string bare = root + "/snapshots/no-marker";
        fs::create_directories( bare, ec );
        { std::ofstream f( bare + "/a.txt" ); f << "alpha"; }
        { std::ofstream f( bare + "/plugin.json" );
          f << "{\"id\":\"org.test.snap\"}"; }
        std::string error;
        REQUIRE_FALSE( verifyPluginSnapshot( bare, "org.test.snap", error ) );
    }
    // Env-driven budgets (O2.2/O2.3's plumbing): SICNU_PLUGIN_SNAPSHOT_MAX_*
    // feeds fromEnvironment — real values read through, signed input and
    // non-numeric input fall back to defaults, below-floor clamps.
    {
        qputenv( "SICNU_PLUGIN_SNAPSHOT_MAX_BYTES", "8192" );
        qputenv( "SICNU_PLUGIN_SNAPSHOT_MAX_FILES", "16" );
        PluginSnapshotBudget envBudget = PluginSnapshotBudget::fromEnvironment();
        REQUIRE( envBudget.maxBytes == 8192 );
        REQUIRE( envBudget.maxFiles == 16 );

        // Signed input is rejected outright (strtoull would wrap "-5" into
        // a huge value and MAX OUT the bound instead of failing closed).
        qputenv( "SICNU_PLUGIN_SNAPSHOT_MAX_BYTES", "-5" );
        envBudget = PluginSnapshotBudget::fromEnvironment();
        REQUIRE( envBudget.maxBytes == PluginSnapshotBudget{}.maxBytes );
        qputenv( "SICNU_PLUGIN_SNAPSHOT_MAX_BYTES", "bogus" );
        envBudget = PluginSnapshotBudget::fromEnvironment();
        REQUIRE( envBudget.maxBytes == PluginSnapshotBudget{}.maxBytes );
        // Below the floor clamps UP — a hostile env cannot turn the bound
        // into an effective no-snapshot.
        qputenv( "SICNU_PLUGIN_SNAPSHOT_MAX_BYTES", "8" );
        qputenv( "SICNU_PLUGIN_SNAPSHOT_MAX_FILES", "1" );
        envBudget = PluginSnapshotBudget::fromEnvironment();
        REQUIRE( envBudget.maxBytes == 4096 );
        REQUIRE( envBudget.maxFiles == 8 );
        qunsetenv( "SICNU_PLUGIN_SNAPSHOT_MAX_BYTES" );
        qunsetenv( "SICNU_PLUGIN_SNAPSHOT_MAX_FILES" );
    }
    fs::remove_all( root, ec );
}

TEST_CASE( "snapshot sweep reclaims residue but keeps live and own-pid artifacts",
           "[plugin][snapshot][p13]" )
{
    namespace fs = std::filesystem;
    const std::string root =
        ( fs::temp_directory_path() / "exprs_test_sweep" ).generic_string();
    std::error_code ec;
    fs::remove_all( root, ec );
    const std::string snapRoot = pluginSnapshotRoot( root );
    fs::create_directories( snapRoot, ec );
    const long pid = snapshotOwnerPid();
    // A pid guaranteed dead: INT_MAX exceeds every platform's pid_max, so
    // the sweep's liveness probe deterministically classifies it as a
    // crashed foreign process (a "pid+N" guess could hit a live process).
    const long otherPid = static_cast<long>( std::numeric_limits<int>::max() );

    const auto seed = [&snapRoot]( const std::string &name ) {
        std::error_code err;
        fs::create_directories( snapRoot + "/" + name, err );
        std::ofstream( snapRoot + "/" + name + "/x" ) << "x";
    };
    seed( "last-good-org.live" );
    seed( "last-good-org.orphan" );
    seed( "upgrade-org.a-" + std::to_string( otherPid ) );  // foreign crash residue
    seed( "upgrade-org.b-" + std::to_string( pid ) );        // own live upgrade
    // In-flight residue grammar: <dest>~staging-<pid>-<seq> and
    // <dest>~old-<pid>-<seq>. '~' cannot appear in a plugin id, so a real
    // snapshot named last-good-x.staging-N (id "x.staging-N" is legal) is
    // never conflated with residue — the misclassification class 12.x had.
    seed( "last-good-x~staging-" + std::to_string( otherPid ) + "-0" );
    seed( "last-good-y~old-" + std::to_string( otherPid ) + "-0" ); // dest missing -> restored
    seed( "last-good-x.staging-" + std::to_string( otherPid ) );    // VALID id, kept
    seed( "unrelated-dir" );                                        // foreign name: untouched

    const int removed = sweepPluginSnapshots(
        root, { "org.live", "org.b", "x.staging-" + std::to_string( otherPid ) } );
    REQUIRE( removed >= 3 );
    REQUIRE( fs::exists( snapRoot + "/last-good-org.live", ec ) );
    REQUIRE_FALSE( fs::exists( snapRoot + "/last-good-org.orphan", ec ) );
    REQUIRE_FALSE(
        fs::exists( snapRoot + "/upgrade-org.a-" + std::to_string( otherPid ), ec ) );
    REQUIRE( fs::exists( snapRoot + "/upgrade-org.b-" + std::to_string( pid ), ec ) );
    REQUIRE_FALSE( fs::exists(
        snapRoot + "/last-good-x~staging-" + std::to_string( otherPid ) + "-0", ec ) );
    // A real snapshot whose plugin id contains ".staging-<digits>" is NOT
    // residue — kept (the '~-separator makes the two classes disjoint).
    REQUIRE( fs::exists(
        snapRoot + "/last-good-x.staging-" + std::to_string( otherPid ), ec ) );
    // The parked dest was restored (crash between the two renames).
    REQUIRE( fs::exists( snapRoot + "/last-good-y", ec ) );
    REQUIRE( fs::exists( snapRoot + "/unrelated-dir", ec ) );
    fs::remove_all( root, ec );
}

TEST_CASE( "snapshot sweep keeps a live sibling's pid-attributed last-good (completion 13/15)",
           "[plugin][snapshot][completion13]" )
{
    namespace fs = std::filesystem;
    const std::string root =
        ( fs::temp_directory_path() / "exprs_test_sweep_cross" ).generic_string();
    std::error_code ec;
    fs::remove_all( root, ec );
    const std::string snapRoot = pluginSnapshotRoot( root );
    fs::create_directories( snapRoot, ec );

    // A guaranteed-LIVE foreign pid: a short-lived sleeper child. While it
    // runs, its pid-attributed last-good snapshot belongs to a live process —
    // another instance's sweep (which does not have the plugin in ITS live
    // set) must never collect it. Master keyed last-good purely on the
    // sweeping process's live ids and deleted exactly this directory.
    long siblingPid = 0;
    {
#ifdef _WIN32
        QProcess sleeper;
        sleeper.setProgram( "cmd.exe" );
        sleeper.setArguments( { "/c", "timeout /t 8 /nobreak >nul" } );
        sleeper.start();
        REQUIRE( sleeper.waitForStarted( 5000 ) );
        siblingPid = static_cast<long>( sleeper.processId() );
#else
        QProcess sleeper;
        sleeper.start( "sleep", { "8" } );
        REQUIRE( sleeper.waitForStarted( 5000 ) );
        siblingPid = static_cast<long>( sleeper.processId() );
#endif
        REQUIRE( siblingPid > 0 );

        const std::string name = "last-good-org.sibling-" + std::to_string( siblingPid );
        {
            std::error_code err;
            fs::create_directories( snapRoot + "/" + name, err );
            std::ofstream( snapRoot + "/" + name + "/x" ) << "x";
        }
        // The sweeping process's live set does NOT contain org.sibling — the
        // exact cross-process shape this attribution fixes.
        const int removed = sweepPluginSnapshots( root, {} );
        ( void ) removed;
        REQUIRE( fs::exists( snapRoot + "/" + name, ec ) );

        // The pid-suffixed name must still parse as a SAFE name (digits and
        // dashes are); a malformed owner suffix is residue, not a sibling.
        const long deadPid = static_cast<long>( std::numeric_limits<int>::max() );
        const std::string deadName = "last-good-org.crashed-" + std::to_string( deadPid );
        {
            std::error_code err;
            fs::create_directories( snapRoot + "/" + deadName, err );
            std::ofstream( snapRoot + "/" + deadName + "/x" ) << "x";
        }
        sweepPluginSnapshots( root, {} );
        REQUIRE_FALSE( fs::exists( snapRoot + "/" + deadName, ec ) );
        // The sibling is STILL alive at this point (its QProcess is in scope)
        // and its snapshot must have survived both sweeps.
        REQUIRE( fs::exists( snapRoot + "/" + name, ec ) );
    }
    fs::remove_all( root, ec );
}

TEST_CASE( "snapshot sweep keeps the legacy last-good liveness rule unchanged (completion 13/15)",
           "[plugin][snapshot][completion13]" )
{
    namespace fs = std::filesystem;
    const std::string root =
        ( fs::temp_directory_path() / "exprs_test_sweep_legacy" ).generic_string();
    std::error_code ec;
    fs::remove_all( root, ec );
    const std::string snapRoot = pluginSnapshotRoot( root );
    fs::create_directories( snapRoot, ec );
    const long ownPid = snapshotOwnerPid();

    const auto seed = [&snapRoot]( const std::string &name ) {
        std::error_code err;
        fs::create_directories( snapRoot + "/" + name, err );
        std::ofstream( snapRoot + "/" + name + "/x" ) << "x";
    };
    // Legacy grammar (pre-attribution layout): liveIds rule unchanged —
    // live here, orphan collected.
    seed( "last-good-org.legacy.live" );
    seed( "last-good-org.legacy.orphan" );
    // Own-pid snapshot: the same-pid rule keeps it even though the id is not
    // in the live set passed to the sweep (an in-flight capture's dir).
    seed( "last-good-org.own-" + std::to_string( ownPid ) );

    const int removed = sweepPluginSnapshots( root, { "org.legacy.live" } );
    ( void ) removed;
    REQUIRE( fs::exists( snapRoot + "/last-good-org.legacy.live", ec ) );
    REQUIRE_FALSE( fs::exists( snapRoot + "/last-good-org.legacy.orphan", ec ) );
    REQUIRE( fs::exists( snapRoot + "/last-good-org.own-" + std::to_string( ownPid ), ec ) );
    fs::remove_all( root, ec );
}

TEST_CASE( "snapshot sweep keeps a dead-owner last-good while the plugin id is live "
           "(completion 13/15)",
           "[plugin][snapshot][completion13]" )
{
    namespace fs = std::filesystem;
    const std::string root =
        ( fs::temp_directory_path() / "exprs_test_sweep_deadowner" ).generic_string();
    std::error_code ec;
    fs::remove_all( root, ec );
    const std::string snapRoot = pluginSnapshotRoot( root );
    fs::create_directories( snapRoot, ec );
    // A pid guaranteed dead (see the sweep harness above).
    const long deadPid = static_cast<long>( std::numeric_limits<int>::max() );

    const auto seed = [&snapRoot]( const std::string &name ) {
        std::error_code err;
        fs::create_directories( snapRoot + "/" + name, err );
        std::ofstream( snapRoot + "/" + name + "/x" ) << "x";
    };
    // Owner dead in BOTH readings, but the plugin id is live for the
    // sweeping registry — the keep must hold through the pid-attributed
    // reading (a regression reducing the rule to owner-liveness only would
    // re-delete a live plugin's rollback source).
    seed( "last-good-org.kept-" + std::to_string( deadPid ) );
    // Same shape, id unknown to the sweeper — collectible residue.
    seed( "last-good-org.dropped-" + std::to_string( deadPid ) );

    const int removed = sweepPluginSnapshots( root, { "org.kept" } );
    ( void ) removed;
    REQUIRE( fs::exists( snapRoot + "/last-good-org.kept-" + std::to_string( deadPid ), ec ) );
    REQUIRE_FALSE(
        fs::exists( snapRoot + "/last-good-org.dropped-" + std::to_string( deadPid ), ec ) );
    fs::remove_all( root, ec );
}

TEST_CASE( "snapshot sweep restores a dead-owner upgrade snapshot into a partial install (#1157)",
           "[plugin][snapshot][p13][issue1157]" )
{
    namespace fs = std::filesystem;
    const std::string root =
        ( fs::temp_directory_path() / "exprs_test_sweep_restore" ).generic_string();
    std::error_code ec;
    fs::remove_all( root, ec );
    const std::string pluginRoot = root + "/plugins";
    const std::string pluginDir = pluginRoot + "/org.test.crashed";
    fs::create_directories( pluginDir, ec );

    // A healthy install: manifest + payload.
    {
        std::ofstream manifest( pluginDir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.crashed",
            "name": "Crashed",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": []
        })";
        std::ofstream payload( pluginDir + "/payload.txt", std::ios::trunc );
        payload << "good-version-bytes";
    }

    // A VERIFIED upgrade snapshot owned by a certainly-dead pid (INT_MAX is
    // beyond every pid_max): the process died mid-rollback.
    const long deadPid = static_cast<long>( std::numeric_limits<int>::max() );
    const std::string snapshotDir =
        pluginSnapshotRoot( root ) + "/upgrade-org.test.crashed-" + std::to_string( deadPid );
    const PluginSnapshotResult snap = capturePluginSnapshot(
        pluginDir, snapshotDir, "org.test.crashed", PluginSnapshotBudget::fromEnvironment() );
    REQUIRE( snap.ok() );

    // Crash mid-rollback: restorePluginSnapshot had cleared part of the
    // live directory when the process died — plugin.json is gone, a
    // leftover half-written new file remains.
    fs::remove( pluginDir + "/plugin.json", ec );
    {
        std::ofstream partial( pluginDir + "/partial-new.txt", std::ios::trunc );
        partial << "half-written";
    }

    const int removed =
        sweepPluginSnapshots( root, { "org.test.crashed" }, { pluginRoot } );
    REQUIRE( removed >= 1 );
    // The snapshot was consumed by the RESTORE, not deleted as residue: the
    // install is whole again (verified bytes, marker excluded), the
    // half-written file is gone, and no snapshot is left behind.
    REQUIRE( fs::exists( pluginDir + "/plugin.json", ec ) );
    REQUIRE_FALSE( fs::exists( pluginDir + "/partial-new.txt", ec ) );
    REQUIRE_FALSE( fs::exists( snapshotDir, ec ) );
    {
        std::ifstream restored( pluginDir + "/payload.txt" );
        std::string bytes( ( std::istreambuf_iterator<char>( restored ) ),
                           std::istreambuf_iterator<char>() );
        REQUIRE( bytes == "good-version-bytes" );
    }
    fs::remove_all( root, ec );
}

TEST_CASE( "installOrUpgrade refuses a policy-gated manifest before draining v1",
           "[plugin][upgrade][p13]" )
{
    // O1.4: the host policy gate runs BEFORE the old generation drains —
    // an enforce-mode permission gap refuses the upgrade with v1 untouched.
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );
    REQUIRE( registry.load( fixture.id ) );

    // Re-gate under Enforce: v1 still passes (it declares both permissions
    // external_tools implies); v2 drops filesystem_read — validated, but
    // policy-blocked before the drain.
    {
        PluginRegistryOptions options;
        options.roots = { fixture.userRoot };
        options.tempDirectory = fixture.root;
        options.policy.allowThirdPartyNative = true;
        options.policy.mode = PluginPolicyMode::Enforce;
        registry.configure( options );
    }
    REQUIRE( registry.isLoaded( fixture.id ) );

    const std::string dir = fixture.root + "/src-v2";
    std::error_code ec;
    std::filesystem::create_directories( dir, ec );
    {
        std::ofstream manifest( dir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": ")" + fixture.id + R"(",
            "name": "Upgrade Fixture",
            "version": "2.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint_kind": "manifest",
            "capabilities": ["operator", "external_tools"],
            "permissions": ["external_process"],
            "operators": []
        })";
    }

    const PluginRegistry::PluginUpgradeResult refused =
        registry.installOrUpgrade( dir );
    REQUIRE( refused.status == PluginRegistry::PluginUpgradeStatus::Refused );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::TrustRejected ) );
    REQUIRE( registry.isLoaded( fixture.id ) ); // never drained
    REQUIRE( fixture.installedVersion() == "1.0.0" );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "concurrent installOrUpgrade on the same id refuses the second",
           "[plugin][upgrade][p13]" )
{
    // O1.11: one lifecycle operation per plugin — a second upgrade racing
    // the owner's transaction is refused typed, never interleaved.
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );
    fixture.writePackage( "src-v2", "2.0.0", true );
    fixture.writePackage( "src-v3", "3.0.0", true );

    // The winner parks inside its migrateState hook — lifecycle ownership
    // is held across it — so the loser provably overlaps the transaction.
    std::atomic<bool> inMigration{ false };
    std::atomic<bool> releaseMigration{ false };
    PluginRegistry::PluginUpgradeOptions slow;
    slow.migrateState = [&]( const std::string & ) {
        inMigration.store( true );
        while ( !releaseMigration.load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        return true;
    };

    PluginRegistry::PluginUpgradeResult winner;
    std::thread first( [&] {
        winner = registry.installOrUpgrade( fixture.root + "/src-v2", slow );
    } );
    const auto waitStart = std::chrono::steady_clock::now();
    while ( !inMigration.load()
            && std::chrono::steady_clock::now() - waitStart < std::chrono::seconds( 5 ) )
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    if ( !inMigration.load() )
    {
        // Never leave a joinable thread parked when the test aborts —
        // ~thread() on a live joinable thread is std::terminate.
        releaseMigration.store( true );
        first.join();
        FAIL( "the winner never reached the migration hook" );
    }

    const PluginRegistry::PluginUpgradeStatus loserStatus =
        registry.installOrUpgrade( fixture.root + "/src-v3" ).status;
    // Capture outcomes first; only assert once no thread can be left
    // joinable-and-blocked behind a failed REQUIRE.
    releaseMigration.store( true );
    first.join();
    REQUIRE( loserStatus == PluginRegistry::PluginUpgradeStatus::Refused );
    // NOTE: no sawCode() assertion for the loser's diagnostic — the shared
    // mDiagnostics log is last-refresh-wins, and the winner's refresh()
    // legitimately overwrites a refusal recorded mid-transaction. The
    // durable typed contract is the Refused status itself.
    REQUIRE( winner.status == PluginRegistry::PluginUpgradeStatus::Upgraded );
    REQUIRE( fixture.installedVersion() == "2.0.0" );
}

TEST_CASE( "installOrUpgrade refuses a plugin shadowed by an earlier root",
           "[plugin][upgrade][p13]" )
{
    // Discovery is first-root-wins: a same-id plugin in an earlier root
    // shadows the user-root copy. Installing here would write bytes
    // discovery never surfaces; on the upgrade path it would also drain
    // the SHADOWING generation and then report a false Upgraded. The
    // shadow guard refuses both branches typed.
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    const std::string bundledRoot = fixture.root + "/bundled-plugins";
    std::error_code ec;
    std::filesystem::create_directories( bundledRoot, ec );
    fixture.writePackage( "src-shadow", "1.0.0", true );
    std::filesystem::rename( fixture.root + "/src-shadow",
                             bundledRoot + "/" + fixture.id, ec );
    REQUIRE( !ec );

    PluginRegistryOptions options;
    options.roots = { bundledRoot, fixture.userRoot };
    options.tempDirectory = fixture.root;
    options.policy.allowThirdPartyNative = true;
    registry.configure( options );

    // Fresh-install shadow: the id already resolves to the bundled copy —
    // a user-root install would sit permanently invisible.
    fixture.writePackage( "src-v2", "2.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v2" ).status
             == PluginRegistry::PluginUpgradeStatus::Refused );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::TrustRejected ) );
    REQUIRE_FALSE( std::filesystem::exists( fixture.targetDir() ) );

    // Upgrade shadow: plant a user-root copy directly (what a prior install
    // would have left), keep the bundled shadow — the upgrade must refuse
    // rather than drain the bundled generation and report a false Upgraded.
    std::filesystem::copy( bundledRoot + "/" + fixture.id, fixture.targetDir(),
                           std::filesystem::copy_options::recursive, ec );
    REQUIRE( !ec );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v2" ).status
             == PluginRegistry::PluginUpgradeStatus::Refused );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::TrustRejected ) );
    // The shadowed user copy is untouched: still the planted v1 bytes.
    REQUIRE( fixture.installedVersion() == "1.0.0" );
}

TEST_CASE( "installOrUpgrade rolls back when the package fails checksum verification",
           "[plugin][upgrade][p13]" )
{
    // A package.checksums digest that can never match the staged payload
    // fails install() AFTER the drain — the post-drain failure leg: the
    // old bytes are still on disk (install aborted before the swap), the
    // old generation reloads, and the status is RolledBack (never Failed —
    // the previous install was fully intact).
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );
    REQUIRE( registry.load( fixture.id ) );
    REQUIRE( registry.isLoaded( fixture.id ) );

    const std::string dir = fixture.root + "/src-v2";
    std::filesystem::create_directories( dir );
    {
        std::ofstream manifest( dir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": ")" + fixture.id + R"(",
            "name": "Upgrade Fixture",
            "version": "2.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint_kind": "manifest",
            "capabilities": ["operator", "external_tools"],
            "permissions": ["external_process", "filesystem_read"],
            "package": { "checksums": { "plugin.json": "0000000000000000000000000000000000000000000000000000000000000000" } }
        })";
    }

    const PluginRegistry::PluginUpgradeResult result =
        registry.installOrUpgrade( dir );
    REQUIRE( result.status == PluginRegistry::PluginUpgradeStatus::RolledBack );
    REQUIRE( fixture.installedVersion() == "1.0.0" );
    REQUIRE( registry.isLoaded( fixture.id ) );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::PluginUpgradeRolledBack ) );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "registry teardown joins an in-flight snapshot capture",
           "[plugin][snapshot][p13]" )
{
    // O2.5: unloadAll() must cancel+join a capture worker — no leak, no
    // hang, no publish racing teardown. A dev-mode load() is what arms the
    // async last-good capture in production code.
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    {
        PluginRegistryOptions options;
        options.roots = { fixture.userRoot };
        options.tempDirectory = fixture.root;
        options.policy.allowThirdPartyNative = true;
        options.policy.devMode = true;
        registry.configure( options );
    }
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );

    // Make the capture span several file-boundary cancel checks so the
    // job is realistically still in flight when teardown lands.
    for ( int i = 0; i < 64; ++i )
    {
        std::ofstream f( fixture.targetDir() + "/payload_" + std::to_string( i )
                             + ".bin",
                         std::ios::binary );
        f << std::string( 8192, 'x' );
    }
    REQUIRE( registry.load( fixture.id ) ); // starts the async capture
    registry.unloadAll();                   // must return: cancel + join

    // Whatever raced, no in-flight staging dir may remain afterwards.
    for ( const std::string &name : fixture.snapshotRootEntries() )
        REQUIRE( name.find( "~staging-" ) == std::string::npos );

    // The same contract at the job level: dropping the last reference
    // cancels and joins instead of leaking a worker.
    const std::string big = fixture.root + "/big-src";
    std::filesystem::create_directories( big );
    for ( int i = 0; i < 64; ++i )
    {
        std::ofstream f( big + "/f" + std::to_string( i ) + ".bin",
                         std::ios::binary );
        f << std::string( 8192, 'y' );
    }
    {
        auto job = PluginSnapshotJob::start(
            big, fixture.snapshotRoot + "/last-good-teardown", "teardown",
            PluginSnapshotBudget() );
        job->cancel();
    } // shared_ptr drop -> dtor cancels + joins; reaching this line is the proof
    REQUIRE( true );
}

TEST_CASE( "configure sweeps orphaned last-good snapshots and refuses symlinks",
           "[plugin][snapshot][p13]" )
{
    // O3.3 + O3.5: a last-good tree whose plugin is no longer discovered is
    // collected on (re)configure; a symlink planted in a collectible name
    // slot is unlinked without ever traversing its target.
    UpgradeFixture fixture;
    PluginRegistry &registry = fixture.registry;
    fixture.writePackage( "src-v1", "1.0.0", true );
    REQUIRE( registry.installOrUpgrade( fixture.root + "/src-v1" ).status
             == PluginRegistry::PluginUpgradeStatus::Installed );

    const std::string lastGood = fixture.snapshotRoot + "/last-good-" + fixture.id;
    {
        const PluginSnapshotResult snap = capturePluginSnapshot(
            fixture.targetDir(), lastGood, fixture.id, PluginSnapshotBudget() );
        REQUIRE( snap.ok() );
    }
    REQUIRE( std::filesystem::exists( lastGood ) );

    // Orphan it: the package vanishes, so the id is no longer discovered.
    std::error_code ec;
    std::filesystem::remove_all( fixture.targetDir(), ec );
    REQUIRE( !ec );

    PluginRegistryOptions options;
    options.roots = { fixture.userRoot };
    options.tempDirectory = fixture.root;
    options.policy.allowThirdPartyNative = true;
    registry.configure( options );
    REQUIRE( !std::filesystem::exists( lastGood ) ); // swept

#ifndef _WIN32
    // A forged link in a collectible slot: the sweep drops the LINK only —
    // the outside tree it points at is never entered (fail closed).
    const std::filesystem::path outside =
        std::filesystem::path( fixture.root ) / "outside-target";
    std::filesystem::create_directories( outside );
    {
        std::ofstream f( ( outside / "keep.txt" ).string(), std::ios::trunc );
        f << "must survive";
    }
    std::filesystem::create_symlink(
        outside, std::filesystem::path( fixture.snapshotRoot )
                     / "last-good-org.forged" );
    registry.configure( options );
    REQUIRE( std::filesystem::exists( outside / "keep.txt" ) );
    REQUIRE( !std::filesystem::exists(
        std::filesystem::path( fixture.snapshotRoot ) / "last-good-org.forged" ) );
#endif
}
