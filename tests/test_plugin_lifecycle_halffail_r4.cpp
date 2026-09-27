// tests/test_plugin_lifecycle_halffail_r4.cpp — registry-level contracts for
// mid-load failures (track R4 WP-B): ANY failure between dlopen and publish
// must leave the registry exactly as it was before the attempt — no
// half-registered state, no orphaned contributions, no last-good snapshot of
// a version that never loaded, no residue in the snapshot root.
//
// Failure matrix (each case = one injectable mid-load failure):
//   1. dlopen failure        entrypoint is a regular file that is not a library
//   2. entrypoint nullptr    EXPRS_createPluginV1 returns nullptr
//   3. entrypoint throws     EXPRS_createPluginV1 throws
//   4. id mismatch           binary pluginId() != manifest id (the native
//                            analog of #1334's Qt-metadata IID pre-check)
//   5. initialize() false    plugin refuses initialization
//   6. initialize() throws   plugin throws during initialization
//   7. invalid dependency    manifest dependency spec fails validation
//   8. unloadAll after       teardown after a failed load cleans every trace
//
// Known seam NOT exercised here (documented in DECISIONS.md, D-3): master's
// validator checks dependency SPEC FORMAT only (plugin_validator.cpp:560);
// there is no load-time dependency-resolution gate, so "declared dependency
// missing" cannot be injected as a mid-load failure. Adding one would be new
// functionality — outside this hardening track's iron law.
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_record.h"
#include "exprs/plugin_permissions.h"
#include "exprs/plugin_registry.h"
#include "exprs/plugin_snapshot.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
static void halffailSetEnv( const char *key, const char *value )
{
    const std::string kv = std::string( key ) + "=" + ( value ? value : "" );
    _putenv( kv.c_str() );
}
static void halffailUnsetEnv( const char *key )
{
    const std::string kv = std::string( key ) + "=";
    _putenv( kv.c_str() );
}
#else
#include <cstdlib>
static void halffailSetEnv( const char *key, const char *value ) { setenv( key, value, 1 ); }
static void halffailUnsetEnv( const char *key ) { unsetenv( key ); }
#endif

using namespace exprs;

#ifndef SICNU_TEST_BROKEN_PLUGIN_DIR
#error "SICNU_TEST_BROKEN_PLUGIN_DIR must point at the built broken fixture plugin dir"
#endif

namespace {

const char *kBrokenId = "org.exprs.test.broken-plugin";
#ifdef _WIN32
const char *kBrokenEntrypoint = "libbroken_plugin.dll";
#else
const char *kBrokenEntrypoint = "libbroken_plugin.so";
#endif

/// RAII env selector for the fixture's failure mode.
class BrokenMode
{
public:
    explicit BrokenMode( const char *mode ) { halffailSetEnv( "SICNU_BROKEN_PLUGIN_MODE", mode ); }
    ~BrokenMode() { halffailUnsetEnv( "SICNU_BROKEN_PLUGIN_MODE" ); }
    BrokenMode( const BrokenMode & ) = delete;
    BrokenMode &operator=( const BrokenMode & ) = delete;
};

/// Recording sink that mirrors the production contract: contributions are
/// revoked before the library is unmapped, and counts are observable.
class CountingSink : public PluginContributionSink
{
public:
    void revokePlugin( const std::string & ) override
    {
        operatorIds.clear();
        factories = 0;
    }
    bool registerOperatorFactory( const std::string &, const std::string &operatorId,
                                  std::function<std::unique_ptr<sicnu::operators::RSOperator>()> ) override
    {
        operatorIds.push_back( operatorId );
        ++factories;
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
    bool waitPluginIdle( const std::string &, int ) override { return true; }

    std::vector<std::string> operatorIds;
    int factories = 0;
};

/// One private registry stage per TEST_CASE: private user root and temp dir
/// so snapshot-root residue claims are checkable by listing one directory.
/// The constructor writes a DEFAULT manifest for the broken fixture id and
/// configures discovery over the fixture dir's parent — tests that need a
/// different manifest overwrite it and call reconfigure().
struct HalfFailFixture
{
    PluginRegistry &registry = PluginRegistry::instance();
    CountingSink sink;
    const std::string root;
    const std::string userRoot;
    const std::string pluginDir;

    HalfFailFixture()
        : root( ( std::filesystem::temp_directory_path() / "exprs_test_halffail_r4" )
                    .generic_string() )
        , userRoot( root + "/user-plugins" )
        , pluginDir( std::string( SICNU_TEST_BROKEN_PLUGIN_DIR ) )
    {
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        halffailUnsetEnv( "SICNU_BROKEN_PLUGIN_MODE" );
        std::filesystem::create_directories( userRoot, ec );
        writeManifest( kBrokenId, kBrokenEntrypoint );
        configure();
    }
    ~HalfFailFixture()
    {
        registry.unloadAll();
        registry.setContributionSink( nullptr );
        halffailUnsetEnv( "SICNU_BROKEN_PLUGIN_MODE" );
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        std::filesystem::remove_all( pluginDir + "/plugin.json", ec );
        std::filesystem::remove_all( pluginDir + "/libgarbage.so", ec );
    }

    void writeManifest( const std::string &id, const std::string &entrypoint,
                        const std::string &dependencies = std::string() )
    {
        std::ofstream manifest( pluginDir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": ")" << id << R"(",
            "name": "Broken Fixture",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint": ")" << entrypoint << R"(",
            "entrypoint_kind": "native",
            "capabilities": ["operator"],)"
                 << dependencies << R"(
            "operators": [{ "id": "test:broken", "display_name": "Broken", "group": "test" }]
        })";
    }

    void configure()
    {
        PluginRegistryOptions options;
        options.roots = { pluginDir + "/.." };
        options.tempDirectory = root;
        options.policy.allowThirdPartyNative = true;
        // Dev mode arms the async last-good capture on SUCCESSFUL loads —
        // the positive control below needs it to prove the snapshot root is
        // real (non-empty after a healthy load), which is what makes the
        // failure cases' emptiness assertions meaningful. Failed loads must
        // still publish nothing (asserted per case).
        options.policy.devMode = true;
        registry.setContributionSink( &sink );
        registry.configure( options );
        registry.setEnabled( kBrokenId, true );
    }

    bool sawCode( PluginDiagnosticCode code ) const
    {
        for ( const PluginDiagnostic &item : registry.diagnostics().items() )
            if ( item.code == code )
                return true;
        return false;
    }

    /// The observable registry state the transaction contract compares
    /// before/after a failed load: contributions, loaded set, and the
    /// record's manifest provenance fields (capability/permission surfaces).
    struct State
    {
        std::vector<std::string> loadedIds;
        std::vector<std::string> operatorIds;
        int factories = 0;
        PluginState recordState = PluginState::Discovered;
        bool hasRecord = false;
        std::vector<std::string> capabilities;
        std::vector<std::string> permissions;
        std::string version;
    };

    State capture( const std::string &id ) const
    {
        State state;
        state.loadedIds = registry.loadedPluginIds();
        state.operatorIds = sink.operatorIds;
        state.factories = sink.factories;
        if ( const PluginRecord *record = registry.record( id ) )
        {
            state.hasRecord = true;
            state.recordState = record->state;
            state.capabilities = record->manifest.capabilities;
            for ( PluginPermission permission : record->manifest.permissions )
                state.permissions.push_back( pluginPermissionName( permission ) );
            state.version = record->manifest.version;
        }
        std::sort( state.capabilities.begin(), state.capabilities.end() );
        std::sort( state.permissions.begin(), state.permissions.end() );
        return state;
    }

    std::vector<std::string> snapshotRootEntries() const
    {
        // <tempDirectory>/sicnu-plugin-snapshots — the same deterministic
        // root UpgradeFixture computes (registry.snapshotRoot() is private).
        const std::string snapshotRoot = root + "/sicnu-plugin-snapshots";
        std::vector<std::string> names;
        std::error_code ec;
        for ( const auto &entry :
              std::filesystem::directory_iterator( snapshotRoot, ec ) )
            names.push_back( entry.path().filename().generic_string() );
        return names;
    }
};

} // namespace

TEST_CASE( "positive control: the broken fixture loads healthy in ok mode",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    BrokenMode mode( "ok" );

    REQUIRE( fixture.registry.load( kBrokenId ) );
    REQUIRE( fixture.registry.isLoaded( kBrokenId ) );
    REQUIRE( fixture.sink.factories == 1 );
    REQUIRE( fixture.sink.operatorIds == std::vector<std::string>{ "test:broken" } );
    // Positive control for the emptiness claims in the failure cases below:
    // snapshotRootEntries() is empty-by-iteration on a MISSING root, so the
    // success path must prove the root actually gets created (bounded wait
    // for the async last-good capture).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
    while ( fixture.snapshotRootEntries().empty()
            && std::chrono::steady_clock::now() < deadline )
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    REQUIRE( !fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "dlopen failure leaves the registry exactly as before the attempt",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    // A SEPARATE garbage entry file (the real fixture library stays intact):
    // it passes containment (regular file inside the root) but is not a
    // shared library, so the failure must land on the dlopen leg.
    const char *garbage = "libgarbage.so";
    {
        std::ofstream entry( fixture.pluginDir + "/" + garbage, std::ios::binary );
        entry << "this is not a shared library";
    }
    fixture.writeManifest( kBrokenId, garbage );
    fixture.configure();

    REQUIRE( fixture.registry.record( kBrokenId ) != nullptr );
    const HalfFailFixture::State before = fixture.capture( kBrokenId );

    REQUIRE_FALSE( fixture.registry.load( kBrokenId ) );

    const HalfFailFixture::State after = fixture.capture( kBrokenId );
    REQUIRE( after.hasRecord == before.hasRecord );
    REQUIRE( after.loadedIds == before.loadedIds );
    REQUIRE( after.operatorIds == before.operatorIds );
    REQUIRE( after.factories == before.factories );
    REQUIRE( after.capabilities == before.capabilities );
    REQUIRE( after.permissions == before.permissions );
    REQUIRE( after.version == before.version );
    // The honest terminal state is Failed — never Loading/Loaded residue.
    REQUIRE( after.recordState == PluginState::Failed );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::LibraryLoadFailed ) );
    // A version that never loaded must not publish a last-good snapshot.
    REQUIRE( fixture.snapshotRootEntries().empty() );

    // Retrying the failed load stays typed and adds no residue.
    REQUIRE_FALSE( fixture.registry.load( kBrokenId ) );
    REQUIRE( fixture.capture( kBrokenId ).factories == 0 );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "entrypoint returning nullptr leaves no half-registered state",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    BrokenMode mode( "nullptr" );
    const HalfFailFixture::State before = fixture.capture( kBrokenId );

    REQUIRE_FALSE( fixture.registry.load( kBrokenId ) );

    const HalfFailFixture::State after = fixture.capture( kBrokenId );
    REQUIRE( after.loadedIds == before.loadedIds );
    REQUIRE( after.factories == before.factories );
    REQUIRE( after.capabilities == before.capabilities );
    REQUIRE( after.recordState == PluginState::Failed );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::LibraryLoadFailed ) );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "entrypoint throwing is typed and leaves no residue",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    BrokenMode mode( "throw" );
    const HalfFailFixture::State before = fixture.capture( kBrokenId );

    REQUIRE_FALSE( fixture.registry.load( kBrokenId ) );

    const HalfFailFixture::State after = fixture.capture( kBrokenId );
    REQUIRE( after.loadedIds == before.loadedIds );
    REQUIRE( after.factories == before.factories );
    REQUIRE( after.recordState == PluginState::Failed );
    // Typed on THIS reason: the exception leg, not the nullptr leg.
    bool sawThrowMessage = false;
    for ( const PluginDiagnostic &item : fixture.registry.diagnostics().items() )
    {
        if ( item.code == PluginDiagnosticCode::LibraryLoadFailed
             && item.message.find( "entrypoint threw" ) != std::string::npos )
            sawThrowMessage = true;
    }
    REQUIRE( sawThrowMessage );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "manifest/binary id mismatch is refused typed before registration",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    BrokenMode mode( "ok" ); // binary reports org.exprs.test.broken-plugin
    // The manifest declares a DIFFERENT id: discovery indexes the record
    // under it, and the loader must refuse the impostor before initialize
    // or any contribution registration (the native analog of the IID gate).
    const char *impostorId = "org.test.impostor";
    fixture.writeManifest( impostorId, kBrokenEntrypoint );
    fixture.configure();
    // setEnabled keyed the enabled set under the previous scan's id; the
    // impostor id needs its own explicit enable after the rescan.
    fixture.registry.setEnabled( impostorId, true );

    const PluginRecord *record = fixture.registry.record( impostorId );
    REQUIRE( record != nullptr );
    const HalfFailFixture::State before = fixture.capture( impostorId );

    REQUIRE_FALSE( fixture.registry.load( impostorId ) );

    const HalfFailFixture::State after = fixture.capture( impostorId );
    REQUIRE( after.loadedIds == before.loadedIds );
    REQUIRE( after.factories == before.factories );
    REQUIRE( after.recordState == PluginState::Failed );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::InitializationFailed ) );
    // The real payload identity was never registered anywhere.
    REQUIRE( fixture.registry.record( kBrokenId ) == nullptr );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "initialize() returning false leaves no half-registered state",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    BrokenMode mode( "initfail" );
    const HalfFailFixture::State before = fixture.capture( kBrokenId );

    REQUIRE_FALSE( fixture.registry.load( kBrokenId ) );

    const HalfFailFixture::State after = fixture.capture( kBrokenId );
    REQUIRE( after.loadedIds == before.loadedIds );
    REQUIRE( after.factories == before.factories );
    REQUIRE( after.operatorIds.empty() ); // registerContributions never ran
    REQUIRE( after.recordState == PluginState::Failed );
    REQUIRE( fixture.sawCode( PluginDiagnosticCode::InitializationFailed ) );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "initialize() throwing is typed and leaves no residue",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    BrokenMode mode( "initthrow" );
    const HalfFailFixture::State before = fixture.capture( kBrokenId );

    REQUIRE_FALSE( fixture.registry.load( kBrokenId ) );

    const HalfFailFixture::State after = fixture.capture( kBrokenId );
    REQUIRE( after.loadedIds == before.loadedIds );
    REQUIRE( after.factories == before.factories );
    REQUIRE( after.recordState == PluginState::Failed );
    bool sawThrowMessage = false;
    for ( const PluginDiagnostic &item : fixture.registry.diagnostics().items() )
    {
        if ( item.code == PluginDiagnosticCode::InitializationFailed
             && item.message.find( "threw during initialize" ) != std::string::npos )
            sawThrowMessage = true;
    }
    REQUIRE( sawThrowMessage );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "an invalid dependency spec refuses validation before any load",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    BrokenMode mode( "ok" );
    // Spec-format violation (empty dependency id): the validator fails the
    // manifest at discovery time, so the record is never loadable and a load
    // attempt writes nothing.
    fixture.writeManifest( kBrokenId, kBrokenEntrypoint,
                           "\n            \"dependencies\": [\"@^1.0\"]," );
    fixture.configure();

    const PluginRecord *record = fixture.registry.record( kBrokenId );
    REQUIRE( record != nullptr );
    REQUIRE( record->state != PluginState::Validated ); // refused structurally
    const HalfFailFixture::State before = fixture.capture( kBrokenId );

    REQUIRE_FALSE( fixture.registry.load( kBrokenId ) );

    const HalfFailFixture::State after = fixture.capture( kBrokenId );
    REQUIRE( after.loadedIds == before.loadedIds );
    REQUIRE( after.factories == before.factories );
    // Validation failures are recorded on the RECORD's diagnostic log (the
    // scan-time verdict), not in the registry's global log.
    bool sawSpec = false;
    for ( const PluginDiagnostic &item : fixture.registry.record( kBrokenId )->diagnostics.items() )
        if ( item.code == PluginDiagnosticCode::ManifestInvalidField )
            sawSpec = true;
    REQUIRE( sawSpec );
    REQUIRE( fixture.snapshotRootEntries().empty() );
}

TEST_CASE( "unloadAll after a failed load cleans every trace",
           "[plugin][halffail][r4]" )
{
    HalfFailFixture fixture;
    BrokenMode mode( "nullptr" );
    REQUIRE_FALSE( fixture.registry.load( kBrokenId ) );
    REQUIRE( fixture.registry.record( kBrokenId ) != nullptr );
    REQUIRE( fixture.registry.record( kBrokenId )->state == PluginState::Failed );

    fixture.registry.unloadAll();

    REQUIRE( fixture.registry.loadedPluginIds().empty() );
    REQUIRE( fixture.sink.factories == 0 );
    // The registry stays usable: a healthy plugin still loads afterwards
    // (the failed attempt poisoned nothing shared).
    fixture.configure();
    BrokenMode healthy( "ok" );
    REQUIRE( fixture.registry.load( kBrokenId ) );
    REQUIRE( fixture.sink.factories == 1 );
}
