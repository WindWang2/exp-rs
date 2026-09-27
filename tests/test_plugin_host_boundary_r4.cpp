// tests/test_plugin_host_boundary_r4.cpp — containment & trust boundary
// matrix for the plugin system's path policy (track R4 WP-D).
//
// The seams pinned here are the ones that exist on master INSIDE this
// track's file whitelist:
//   - exprs::PathPolicy (src/sdk/exprs/path_policy.{h,cpp}) — the single
//     owner of entrypoint containment (canonical comparison, symlink hops
//     out of the root are escapes, ".." components, absolute candidates);
//   - PluginDiscovery::inspectDirectory — the typed per-directory verdict;
//   - the loader's LOAD-TIME containment re-check (plugin_loader.cpp:384,
//     issue #756) — validation and load read the filesystem at different
//     times, so the entrypoint is re-confined immediately before dlopen.
//
// Boundary matrix:
//   1. case variants        entrypoint bytes are matched exactly; POSIX is
//                           case-sensitive and the policy does NOT fold case
//   2. path separators      "\" is an ordinary filename character on POSIX —
//                           never a separator; policy decisions are
//                           platform-native, not dual-syntax
//   3. prefix relaxation    discovery matches plugin DIRECTORIES by their
//                           manifest id only — a "foo_<id>" directory is a
//                           different record, never a wildcard match
//   4. symlinked dir hop    a symlinked subdirectory hop out of the root is
//                           an escape (canonical containment)
//   5. symlink file escape  an entrypoint that is a symlink pointing outside
//                           the root is an escape — at validation AND again
//                           at load time
//   6. empty plugin dir     a directory without plugin.json is invisible to
//                           the scan and typed (Broken) on direct inspect —
//                           never a crash
//   7. ".." entrypoint      rejected lexically before any filesystem access
//   8. absolute entrypoint  rejected lexically before any filesystem access
//
// NOT pinned here (documented in DECISIONS.md, D-2): #1334's legacy-host
// gates (module whitelist, world-writable directory rejection, Qt-metadata
// IID pre-check) live in src/core/plugin_host.cpp — outside this track's
// whitelist and NOT YET on master (PR #1334 is open). Asserting them today
// would mean red tests for unbuilt code or edits outside the whitelist.
#include <catch2/catch_test_macros.hpp>

#include "exprs/path_policy.h"
#include "exprs/plugin_discovery.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_registry.h"

#include <functional>
#include <memory>

#include <filesystem>
#include <fstream>
#include <stdlib.h>
#include <string>

using namespace exprs;

#ifndef SICNU_TEST_HELLO_PLUGIN_DIR
#error "SICNU_TEST_HELLO_PLUGIN_DIR must point at the built hello fixture plugin dir"
#endif

namespace {

const char *kHelloId = "org.exprs.test.hello-plugin";

/// Minimal contribution sink — the registry's load preflight refuses with
/// RegistrationFailed (state untouched) when NO sink is installed, which
/// would bypass the load-time containment recheck these tests exercise.
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

#ifdef _WIN32
const char *kHelloEntrypoint = "libhello_plugin.dll";
#else
const char *kHelloEntrypoint = "libhello_plugin.so";
#endif

/// A scratch plugin-root tree with the fixture library copied INTO the root
/// and an outside/ directory holding files the root must not reach.
struct BoundaryFixture
{
    const std::string root;
    const std::string pluginDir;
    const std::string outsideFile;

    BoundaryFixture()
        : root( ( std::filesystem::temp_directory_path() / "exprs_test_boundary_r4" )
                    .generic_string() )
        , pluginDir( root + "/" + kHelloId )
        , outsideFile( root + "/outside/libhello_plugin.so" )
    {
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
        std::filesystem::create_directories( pluginDir, ec );
        std::filesystem::create_directories( root + "/outside", ec );
        std::filesystem::copy_file( std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/"
                                        + kHelloEntrypoint,
                                    outsideFile, ec );
        // The in-root copy the manifests below point at.
        std::filesystem::copy_file( std::string( SICNU_TEST_HELLO_PLUGIN_DIR ) + "/"
                                        + kHelloEntrypoint,
                                    pluginDir + "/" + kHelloEntrypoint, ec );
    }
    ~BoundaryFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all( root, ec );
    }

    void writeManifest( const std::string &entrypoint )
    {
        std::ofstream manifest( pluginDir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": ")" << kHelloId << R"(",
            "name": "Hello Fixture",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint": ")" << entrypoint << R"(",
            "entrypoint_kind": "native",
            "capabilities": ["operator"],
            "operators": [{ "id": "test:hello", "display_name": "Test Hello", "group": "test" }]
        })";
    }

    PluginRecord inspect() const
    {
        PluginDiagnosticLog log;
        return PluginDiscovery::inspectDirectory( pluginDir, log );
    }

    bool logHasCode( const PluginDiagnosticLog &log, PluginDiagnosticCode code ) const
    {
        for ( const PluginDiagnostic &item : log.items() )
            if ( item.code == code )
                return true;
        return false;
    }
};

} // namespace

TEST_CASE( "entrypoint case variants are matched exactly, never folded",
           "[plugin][boundary][r4]" )
{
    BoundaryFixture fixture;
    // POSIX filesystems are case-sensitive and the policy treats the
    // entrypoint as exact bytes: "Libhello_plugin.so" is a MISSING file,
    // typed — not a silent match of the real payload.
    fixture.writeManifest( "Libhello_plugin.so" );
    const PluginRecord record = fixture.inspect();
    REQUIRE( record.state != PluginState::Validated );
    const bool typedRefusal =
        fixture.logHasCode( record.diagnostics, PluginDiagnosticCode::EntrypointOutsideRoot )
        || fixture.logHasCode( record.diagnostics, PluginDiagnosticCode::EntrypointMissing )
        || fixture.logHasCode( record.diagnostics,
                               PluginDiagnosticCode::ManifestInvalidField );
    REQUIRE( typedRefusal );

    // The same verdict through the containment seam itself.
    std::string resolved;
    REQUIRE( PathPolicy::checkPayloadInsideRoot( fixture.pluginDir,
                                                 "Libhello_plugin.so", resolved )
             == PathPolicyRejection::Missing );
}

TEST_CASE( "backslash separators are ordinary characters, not separators",
           "[plugin][boundary][r4]" )
{
    BoundaryFixture fixture;
    // Platform-native decision: on POSIX "sub\\lib.so" is a single weird
    // filename (missing here), NOT a path with two components. The policy
    // never re-interprets the other platform's separator.
    fixture.writeManifest( "sub\\libhello_plugin.so" );
    const PluginRecord record = fixture.inspect();
    REQUIRE( record.state != PluginState::Validated );

    std::string resolved;
    REQUIRE( PathPolicy::checkPayloadInsideRoot( fixture.pluginDir,
                                                 "sub\\libhello_plugin.so", resolved )
             == PathPolicyRejection::Missing );
}

TEST_CASE( "a prefixed directory is a different record, never a wildcard match",
           "[plugin][boundary][r4]" )
{
    // Trust boundary: discovery indexes plugin DIRECTORIES and keys records
    // by the manifest id. A "foo_<id>" directory carries its own manifest —
    // it is a DIFFERENT record (if valid) and can never satisfy a lookup
    // for <id>; there is no prefix/suffix matching anywhere.
    BoundaryFixture fixture;
    fixture.writeManifest( kHelloEntrypoint ); // real record at <root>/<id>

    const std::string impostorDir = fixture.root + "/foo_" + kHelloId;
    std::error_code ec;
    std::filesystem::create_directories( impostorDir, ec );
    {
        std::ofstream manifest( impostorDir + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.exprs.test.other-plugin",
            "name": "Impostor",
            "version": "9.9.9",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": )" << pluginAbiVersion() << R"(,
            "entrypoint_kind": "manifest",
            "capabilities": ["operator"],
            "operators": [{ "id": "test:hello", "display_name": "Impostor", "group": "test" }]
        })";
    }

    PluginRegistry &registry = PluginRegistry::instance();
    PluginRegistryOptions options;
    options.roots = { fixture.root };
    options.policy.allowThirdPartyNative = true;
    registry.configure( options );

    const PluginRecord *real = registry.record( kHelloId );
    REQUIRE( real != nullptr );
    REQUIRE( real->directory == fixture.pluginDir );
    REQUIRE( real->manifest.version == "1.0.0" );
    // The prefixed directory is its own record under its own id — and the
    // real id resolves to the real directory, not the impostor's.
    const PluginRecord *other = registry.record( "org.exprs.test.other-plugin" );
    REQUIRE( other != nullptr );
    REQUIRE( other->directory == impostorDir );
}

TEST_CASE( "a symlinked directory hop out of the root is an escape",
           "[plugin][boundary][r4]" )
{
    BoundaryFixture fixture;
    // <pluginDir>/hop -> <root>/outside ; the entrypoint inside the plugin
    // dir only REACHES the payload through a directory symlink that leaves
    // the root — canonical containment rejects it.
    std::error_code ec;
    std::filesystem::create_directory_symlink( fixture.root + "/outside",
                                               fixture.pluginDir + "/hop", ec );
    REQUIRE( !ec );
    fixture.writeManifest( "hop/libhello_plugin.so" );

    std::string resolved;
    REQUIRE( PathPolicy::checkPayloadInsideRoot( fixture.pluginDir,
                                                 "hop/libhello_plugin.so", resolved )
             == PathPolicyRejection::OutsideRoot );

    const PluginRecord record = fixture.inspect();
    REQUIRE( record.state != PluginState::Validated );
}

TEST_CASE( "a symlinked entrypoint file escaping the root is refused at "
           "validation and again at load",
           "[plugin][boundary][r4]" )
{
    BoundaryFixture fixture;
    // The scan must see a CLEAN payload (record lands Validated); the escape
    // is planted afterwards, which is exactly the TOCTOU window the loader's
    // load-time re-check (#756) exists for.
    const std::string payload = fixture.pluginDir + "/payload.so";
    std::error_code ec;
    std::filesystem::copy_file( fixture.outsideFile, payload, ec );
    REQUIRE( !ec );
    fixture.writeManifest( "payload.so" );

    const PluginRecord validated = fixture.inspect();
    REQUIRE( validated.state == PluginState::Validated );

    PluginRegistry &registry = PluginRegistry::instance();
    PluginRegistryOptions options;
    options.roots = { fixture.root };
    options.tempDirectory = fixture.root;
    options.policy.allowThirdPartyNative = true;
    AcceptingSink sink; // the load preflight needs a sink to reach the loader
    registry.setContributionSink( &sink );
    // A failed REQUIRE below must not leave the singleton pointing at the
    // destroyed stack sink for the rest of the binary's cases.
    struct SinkReset
    {
        PluginRegistry &registry;
        ~SinkReset() { registry.setContributionSink( nullptr ); }
    } sinkReset{ registry };
    registry.configure( options );
    REQUIRE( registry.record( kHelloId ) != nullptr );
    REQUIRE( registry.record( kHelloId )->state == PluginState::Validated );
    registry.setEnabled( kHelloId, true );

    // NOW swap the payload for a symlink that leaves the root.
    std::filesystem::remove( payload, ec );
    REQUIRE( !ec );
    std::filesystem::create_symlink( fixture.outsideFile,
                                     fixture.pluginDir + "/payload.so", ec );
    REQUIRE( !ec );

    // Canonical containment refuses the swapped payload at the canonical check.
    std::string resolved;
    REQUIRE( PathPolicy::checkPayloadInsideRoot( fixture.pluginDir, "payload.so",
                                                 resolved )
             == PathPolicyRejection::OutsideRoot );

    REQUIRE_FALSE( registry.load( kHelloId ) );
    REQUIRE_FALSE( registry.isLoaded( kHelloId ) );
    const PluginRecord *after = registry.record( kHelloId );
    REQUIRE( after != nullptr );
    REQUIRE( after->state == PluginState::Failed );
    bool sawEscape = false;
    for ( const PluginDiagnostic &item : registry.diagnostics().items() )
        if ( item.code == PluginDiagnosticCode::EntrypointOutsideRoot )
            sawEscape = true;
    REQUIRE( sawEscape );
}

TEST_CASE( "an empty plugin directory is invisible to the scan and typed on inspect",
           "[plugin][boundary][r4]" )
{
    BoundaryFixture fixture;
    std::error_code ec;
    std::filesystem::remove_all( fixture.pluginDir + "/plugin.json", ec );
    std::filesystem::create_directories( fixture.root + "/empty-plugin", ec );

    // Scan: no manifest, no record — silently skipped, no crash.
    PluginRegistry &registry = PluginRegistry::instance();
    PluginRegistryOptions options;
    options.roots = { fixture.root };
    options.policy.allowThirdPartyNative = true;
    registry.configure( options );
    REQUIRE( registry.record( "empty-plugin" ) == nullptr );
    REQUIRE( registry.record( "" ) == nullptr );

    // Direct inspect: typed Broken verdict, not an exception.
    PluginDiagnosticLog log;
    const PluginRecord record =
        PluginDiscovery::inspectDirectory( fixture.root + "/empty-plugin", log );
    REQUIRE( record.state == PluginState::Broken );
    REQUIRE( !log.items().empty() );
}

TEST_CASE( "a '..' entrypoint is rejected lexically before any filesystem access",
           "[plugin][boundary][r4]" )
{
    // The lexical pre-check does not touch the filesystem: a ".." candidate
    // is DotDot even when the target file does not exist at all.
    REQUIRE( PathPolicy::checkRelativeLexically( "../outside.so" )
             == PathPolicyRejection::DotDot );
    REQUIRE( PathPolicy::checkRelativeLexically( "a/../../outside.so" )
             == PathPolicyRejection::DotDot );

    BoundaryFixture fixture;
    fixture.writeManifest( "../outside/libhello_plugin.so" );
    std::string resolved;
    REQUIRE( PathPolicy::checkPayloadInsideRoot( fixture.pluginDir,
                                                 "../outside/libhello_plugin.so",
                                                 resolved )
             == PathPolicyRejection::DotDot );
    const PluginRecord record = fixture.inspect();
    REQUIRE( record.state != PluginState::Validated );
}

TEST_CASE( "an absolute entrypoint is rejected lexically",
           "[plugin][boundary][r4]" )
{
    REQUIRE( PathPolicy::checkRelativeLexically( "/etc/passwd" )
             == PathPolicyRejection::Absolute );

    BoundaryFixture fixture;
    fixture.writeManifest( fixture.outsideFile );
    const PluginRecord record = fixture.inspect();
    REQUIRE( record.state != PluginState::Validated );
}
