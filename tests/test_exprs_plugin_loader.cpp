// tests/test_exprs_plugin_loader.cpp — native loading with a real fixture .so
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_host_runtime.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_package.h"
#include "exprs/plugin_permissions.h"
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
        // The real rollback source lives in the registry temp directory
        // (<temp>/plugin-last-good-<id>); remove it so the next test starts
        // from a clean slate and the dev-mode temp tree does not accumulate.
        const std::string snapshot =
            std::filesystem::temp_directory_path().generic_string()
            + "/plugin-last-good-" + id;
        std::filesystem::remove_all( snapshot, ec );
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
