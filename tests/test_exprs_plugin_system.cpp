// tests/test_exprs_plugin_system.cpp — discovery, registry, policy, package
#include <catch2/catch_test_macros.hpp>
#ifdef _WIN32
#include "exprs/msvc_posix_shim.h"
static void portableSetenv(const char *key, const char *value)
{
    _putenv((std::string(key) + "=" + value).c_str());
}
#define setenv(k, v, o) portableSetenv(k, v)
#endif
#include <catch2/catch_approx.hpp>

#include "exprs/external_process.h"
#include "exprs/plugin_discovery.h"
#include "exprs/plugin_index.h"
#include "exprs/plugin_package.h"
#include "exprs/plugin_permissions.h"
#include "exprs/plugin_registry.h"
#include "exprs/plugin_validator.h"
#include "exprs/version.h"

#include <filesystem>
#include <fstream>

#include <sys/stat.h>
#ifdef _WIN32
#include <cstdlib> // _exit
#else
#include <unistd.h>
#endif

using namespace exprs;

namespace {
/// Creates a manifest-only plugin directory (external tool operator).
std::string makePluginDir( const std::string &root, const std::string &id,
                           const std::string &operatorId, const std::string &argv0 = "/bin/echo" )
{
    const std::string dir = root + "/" + id;
    ::mkdir( root.c_str(), 0755 );
    ::mkdir( dir.c_str(), 0755 );
    std::ofstream output( dir + "/plugin.json", std::ios::trunc );
    output << R"({
        "manifest_version": 1,
        "id": ")" << id << R"(",
        "name": "Test Plugin",
        "version": "1.0.0",
        "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
        "abi_version": 1,
        "entrypoint_kind": "manifest",
        "capabilities": ["operator", "external_tools"],
        "permissions": ["external_process", "filesystem_read"],
        "operators": [{
            "id": ")" << operatorId << R"(",
            "display_name": "Echo",
            "group": "test",
            "inputs": [{ "name": "text", "type": "string", "required": true }],
            "external": { "argv": [")" << argv0 << R"(", "-n", "ECHO:", "${text}"], "timeout_seconds": 30 }
        }]
    })";
    return dir;
}

namespace {
/// Redirects the user plugin root (and the enable/disable index) away from
/// the real user profile for the whole binary.
const bool kUserRootRedirected = []() {
    ::setenv( "SICNU_PLUGIN_USER_ROOT", "/tmp/exprs_test_userroot", 1 );
    ::system( "rm -rf /tmp/exprs_test_userroot" );
    return true;
}();
} // namespace

struct RegistryGuard
{
    ~RegistryGuard()
    {
        PluginRegistry::instance().unloadAll();
        PluginRegistry::instance().setContributionSink( nullptr );
    }
};
} // namespace

TEST_CASE( "discovery scans roots and caches manifests", "[plugin][discovery]" )
{
    const std::string root = "/tmp/exprs_test_discovery";
    ::system( ( "rm -rf " + root ).c_str() );
    makePluginDir( root, "org.test.alpha", "alpha:echo" );

    PluginDiscoveryOptions options;
    options.roots = { root };
    PluginDiagnosticLog log;
    const auto records = PluginDiscovery::scan( options, log );
    REQUIRE( records.size() == 1 );
    REQUIRE( records[0].manifest.id == "org.test.alpha" );
    REQUIRE( records[0].state == PluginState::Discovered );

    // Second scan uses the manifest index cache and still finds the plugin.
    const auto cached = PluginDiscovery::scan( options, log );
    REQUIRE( cached.size() == 1 );
    REQUIRE( cached[0].manifest.operators.size() == 1 );

    // Broken manifests produce Broken records, not silent drops.
    ::mkdir( ( root + "/org.test.broken" ).c_str(), 0755 );
    std::ofstream bad( root + "/org.test.broken/plugin.json", std::ios::trunc );
    bad << "{ not json";
    bad.close();
    const auto withBroken = PluginDiscovery::scan( options, log );
    REQUIRE( withBroken.size() == 2 );
    bool sawBroken = false;
    for ( const auto &record : withBroken )
        sawBroken = sawBroken || record.state == PluginState::Broken;
    REQUIRE( sawBroken );
}

TEST_CASE( "duplicate plugin ids: first root wins", "[plugin][discovery]" )
{
    const std::string rootA = "/tmp/exprs_test_dup_a";
    const std::string rootB = "/tmp/exprs_test_dup_b";
    ::system( ( "rm -rf " + rootA + " " + rootB ).c_str() );
    makePluginDir( rootA, "org.test.same", "same:echo" );
    makePluginDir( rootB, "org.test.same", "same:echo2" );

    PluginDiscoveryOptions options;
    options.roots = { rootA, rootB };
    PluginDiagnosticLog log;
    const auto records = PluginDiscovery::scan( options, log );
    REQUIRE( records.size() == 2 );
    REQUIRE( records[0].state == PluginState::Discovered );
    REQUIRE( records[1].state == PluginState::Broken );
}

TEST_CASE( "registry lifecycle: validate, policy, disable", "[plugin][registry]" )
{
    RegistryGuard guard;
    const std::string root = "/tmp/exprs_test_registry";
    ::system( ( "rm -rf " + root ).c_str() );
    makePluginDir( root, "org.test.life", "life:echo" );

    PluginRegistryOptions options;
    options.roots = { root };
    auto &registry = PluginRegistry::instance();
    registry.setContributionSink( nullptr );
    registry.configure( options );

    const PluginRecord *record = registry.record( "org.test.life" );
    REQUIRE( record != nullptr );
    REQUIRE( record->state == PluginState::Validated ); // manifest kind: nothing to load natively
    REQUIRE( registry.isEnabled( "org.test.life" ) );

    // Manifest-kind plugins report "loaded" without a binary.
    REQUIRE( registry.load( "org.test.life" ) );
    REQUIRE( registry.isLoaded( "org.test.life" ) );
    REQUIRE( registry.unload( "org.test.life" ) );

    // Disable persists and blocks the validated state.
    REQUIRE( registry.setEnabled( "org.test.life", false ) );
    REQUIRE_FALSE( registry.isEnabled( "org.test.life" ) );
    REQUIRE( registry.record( "org.test.life" )->state == PluginState::Disabled );
    REQUIRE_FALSE( registry.load( "org.test.life" ) );
    registry.setEnabled( "org.test.life", true );
    REQUIRE( registry.isEnabled( "org.test.life" ) );
}

TEST_CASE( "registry policy blocks plugin ids", "[plugin][registry]" )
{
    RegistryGuard guard;
    const std::string root = "/tmp/exprs_test_policy";
    ::system( ( "rm -rf " + root ).c_str() );
    makePluginDir( root, "org.test.blocked", "blocked:echo" );

    PluginRegistryOptions options;
    options.roots = { root };
    options.policy.blockedPluginIds = { "org.test.blocked" };
    auto &registry = PluginRegistry::instance();
    registry.configure( options );

    const PluginRecord *record = registry.record( "org.test.blocked" );
    REQUIRE( record != nullptr );
    REQUIRE( record->state == PluginState::Blocked );
    REQUIRE( record->diagnostics.hasErrorsFor( "org.test.blocked" ) );
    REQUIRE_FALSE( registry.load( "org.test.blocked" ) );
}

TEST_CASE( "plugin packages install and uninstall with traversal protection", "[plugin][package]" )
{
    const std::string pkgRoot = "/tmp/exprs_test_pkg_src";
    const std::string source = pkgRoot + "/org.test.package";
    ::system( "rm -rf /tmp/exprs_test_pkg_src" );
    makePluginDir( pkgRoot, "org.test.package", "package:echo" );

    // A symlink escape attempt must be refused.
    ::symlink( "/etc", ( source + "/etc_link" ).c_str() );

    PluginDiagnosticLog log;
    std::string installed;
    SECTION( "symlink escape refused" )
    {
        REQUIRE_FALSE( PluginPackage::install( source, installed, log ) );
        REQUIRE( log.hasErrors() );
    }
    SECTION( "clean package installs into the user root" )
    {
        ::unlink( ( source + "/etc_link" ).c_str() );
        REQUIRE( PluginPackage::install( source, installed, log ) );
        REQUIRE_FALSE( installed.empty() );
        REQUIRE( PluginPackage::installedIds().size() >= 1 );
        // uninstall removes it again
        REQUIRE( PluginPackage::uninstall( "org.test.package", log ) );
    }
    ::system( "rm -rf /tmp/exprs_test_pkg_src" );
}

TEST_CASE( "staged install verifies declared checksums with rollback", "[plugin][package]" )
{
    const std::string pkgRoot = "/tmp/exprs_test_pkg_ck";
    ::system( "rm -rf /tmp/exprs_test_pkg_ck" );
    ::mkdir( pkgRoot.c_str(), 0755 );

    // Payload with one payload file and matching checksums (computed with
    // the SAME sha256 the SDK uses — round-trips through install()).
    const std::string source = pkgRoot + "/org.test.ck";
    const std::string body = "payload-for-checksum-verification\n";
    {
        ::mkdir( source.c_str(), 0755 );
        std::ofstream payload( source + "/payload.txt", std::ios::trunc );
        payload << body;
    }
    // Compute the digest via the installed SDK path: install a package
    // WITHOUT checksums first, hash the staged copy, then rebuild the
    // source manifest with the declared digest.
    PluginDiagnosticLog log;
    std::string installed;
    {
        std::ofstream manifest( source + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.ck",
            "name": "CK",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": []
        })";
    }
    REQUIRE( PluginPackage::install( source, installed, log ) );
    REQUIRE_FALSE( installed.empty() );

    // A CORRECT checksum (known-answer from an independent sha256 tool)
    // upgrades cleanly through the staged path.
    {
        std::ofstream manifest( source + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.ck",
            "name": "CK",
            "version": "2.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": [],
            "package": {
                "checksums": { "payload.txt": "31492367e97f4ab8dd5f118e03a606f4ae02c586a46a46810e247e5fe9958dd6" },
                "sbom": { "format": "spdx", "path": "sbom.spdx" }
            }
        })";
    }
    log = PluginDiagnosticLog();
    REQUIRE( PluginPackage::install( source, installed, log ) );
    {
        exprs::PluginDiagnostic parseError;
        exprs::PluginManifest upgraded;
        REQUIRE( exprs::loadManifestFromFile( installed + "/plugin.json", upgraded, parseError ) );
        REQUIRE( upgraded.version == "2.0.0" );
        REQUIRE( upgraded.package["sbom"]["format"].asString() == "spdx" );
    }

    // A WRONG checksum refuses the downgrade and keeps the v2.0.0 install.
    {
        std::ofstream manifest( source + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.ck",
            "name": "CK",
            "version": "3.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": [],
            "package": { "checksums": { "payload.txt": "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" } }
        })";
    }
    log = PluginDiagnosticLog();
    REQUIRE_FALSE( PluginPackage::install( source, installed, log ) );
    REQUIRE( log.hasErrors() );
    // Previous install (v2.0.0) survived the failed upgrade.
    exprs::PluginDiagnostic parseError;
    exprs::PluginManifest survivor;
    REQUIRE( exprs::loadManifestFromFile( installed + "/plugin.json", survivor, parseError ) );
    REQUIRE( survivor.version == "2.0.0" );
    // No staging leftovers.
    REQUIRE( !std::filesystem::exists(
        exprs::PluginDiscovery::userPluginRoot() + "/.staging/org.test.ck" ) );

    ::system( "rm -rf /tmp/exprs_test_pkg_ck" );
}

TEST_CASE( "staged-install sha256 matches reference vectors at block boundaries",
           "[plugin][package]" )
{
    // Padded to 55/56/63/64 bytes: every len%64 class of the hand-rolled
    // implementation (padding wrap, two-complement block, exact block).
    const std::vector<std::pair<std::string, std::string>> vectors = {
        { "a", "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" },
        { "ab", "fb8e20fc2e4c3f248c60c39bd652f3c1347298bb977b8b4d5903b85055620603" },
        { std::string( 55, 'x' ), "d5e285683cd4efc02d021a5c62014694958901005d6f71e89e0989fac77e4072" },
        { std::string( 56, 'x' ), "04c26261370ee7541549d16dee320c723e3fd14671e66a099afe0a377c16888e" },
        { std::string( 63, 'x' ), "75220b47218278e656f2013bb8f0c455a25eaf01e86c64924e9d48d89776d6f2" },
        { std::string( 64, 'x' ), "7ce100971f64e7001e8fe5a51973ecdfe1ced42befe7ee8d5fd6219506b5393c" },
    };
    for ( const auto &[ body, digest ] : vectors )
    {
        const std::string pkgRoot = "/tmp/exprs_test_pkg_vec";
        ::system( "rm -rf /tmp/exprs_test_pkg_vec" );
        ::mkdir( pkgRoot.c_str(), 0755 );
        const std::string source = pkgRoot + "/org.test.vec";
        ::mkdir( source.c_str(), 0755 );
        {
            std::ofstream payload( source + "/payload.txt", std::ios::trunc );
            payload << body;
            std::ofstream manifest( source + "/plugin.json", std::ios::trunc );
            manifest << R"({
                "manifest_version": 1,
                "id": "org.test.vec",
                "name": "VEC",
                "version": "1.0.0",
                "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
                "abi_version": 1,
                "entrypoint_kind": "manifest",
                "operators": [],
                "package": { "checksums": { "payload.txt": ")" << digest << R"(" } }
            })";
        }
        PluginDiagnosticLog log;
        std::string installed;
        INFO( "vector length " << body.size() );
        REQUIRE( PluginPackage::install( source, installed, log ) );
        REQUIRE( PluginPackage::uninstall( "org.test.vec", log ) );
    }
    ::system( "rm -rf /tmp/exprs_test_pkg_vec" );
}

// -- plugin-platform 9.0: packaging 3.0 ---------------------------------------

TEST_CASE( "version ranges match semver boundaries exactly", "[plugin][package][p12]" )
{
    // Exact / bare.
    REQUIRE( exprs::PluginPackage::versionSatisfiesRange( "2.0.0", "2.0.0" ) );
    REQUIRE_FALSE( exprs::PluginPackage::versionSatisfiesRange( "2.0.1", "2.0.0" ) );
    REQUIRE( exprs::PluginPackage::versionSatisfiesRange( "9.9.9", "" ) ); // bare id: any

    // Caret: same major (0.x pins the minor, 0.0.x pins the patch — npm).
    REQUIRE( exprs::PluginPackage::versionSatisfiesRange( "2.3.9", "^2.0.0" ) );
    REQUIRE_FALSE( exprs::PluginPackage::versionSatisfiesRange( "3.0.0", "^2.0.0" ) );
    REQUIRE( exprs::PluginPackage::versionSatisfiesRange( "0.2.9", "^0.2.0" ) );
    REQUIRE_FALSE( exprs::PluginPackage::versionSatisfiesRange( "0.3.0", "^0.2.0" ) );
    REQUIRE( exprs::PluginPackage::versionSatisfiesRange( "0.0.3", "^0.0.3" ) );
    REQUIRE_FALSE( exprs::PluginPackage::versionSatisfiesRange( "0.0.4", "^0.0.3" ) );

    // Tilde: same minor.
    REQUIRE( exprs::PluginPackage::versionSatisfiesRange( "2.3.9", "~2.3.0" ) );
    REQUIRE_FALSE( exprs::PluginPackage::versionSatisfiesRange( "2.4.0", "~2.3.0" ) );

    // >= and junk (fail closed).
    REQUIRE( exprs::PluginPackage::versionSatisfiesRange( "3.0.0", ">=2.0.0" ) );
    REQUIRE( exprs::PluginPackage::versionSatisfiesRange( "2.0.0", ">=2.0.0" ) );
    REQUIRE_FALSE( exprs::PluginPackage::versionSatisfiesRange( "1.9.9", ">=2.0.0" ) );
    REQUIRE_FALSE( exprs::PluginPackage::versionSatisfiesRange( "2.0.0", "^banana" ) );
    REQUIRE_FALSE( exprs::PluginPackage::versionSatisfiesRange( "banana", "^2.0.0" ) );
}

TEST_CASE( "interrupted-install staging leftovers are swept before a new install",
           "[plugin][package][p12]" )
{
    namespace fs = std::filesystem;
    const std::string pkgRoot = "/tmp/exprs_test_pkg_stale";
    ::system( "rm -rf /tmp/exprs_test_pkg_stale" );
    const std::string source = pkgRoot + "/org.test.stale";
    makePluginDir( pkgRoot, "org.test.stale", "stale:echo" );

    // Simulate a crashed install's staging directory of a DEAD process
    // (pid+1: the install path only unconditionally removes the CURRENT
    // pid's directory, so this leftover can only disappear through the
    // 24 h sweep — the behavior under test).
    const std::string stagingDir =
        exprs::PluginDiscovery::userPluginRoot() + "/.staging/org.test.stale."
        + std::to_string( static_cast<long>( ::getpid() ) + 1 );
    std::error_code ec;
    fs::create_directories( fs::path( stagingDir ) / "junk", ec );
    {
        std::ofstream marker( stagingDir + "/junk/partial.bin", std::ios::trunc );
        marker << "half-written";
    }
    const auto stale = fs::file_time_type::clock::now() - std::chrono::hours( 48 );
    fs::last_write_time( fs::path( stagingDir ), stale, ec );

    PluginDiagnosticLog log;
    std::string installed;
    REQUIRE( exprs::PluginPackage::install( source, installed, log ) );
    // The sweep must have REMOVED the dead process's leftover.
    REQUIRE_FALSE( fs::exists( fs::path( stagingDir ) ) );
    // The new install holds the real payload, not the crashed staging copy.
    exprs::PluginDiagnostic parseError;
    exprs::PluginManifest installedManifest;
    REQUIRE( exprs::loadManifestFromFile( installed + "/plugin.json", installedManifest, parseError ) );
    REQUIRE( installedManifest.id == "org.test.stale" );
    REQUIRE( exprs::PluginPackage::uninstall( "org.test.stale", log ) );
    ::system( "rm -rf /tmp/exprs_test_pkg_stale" );
}

TEST_CASE( "dependency constraints are probed at install time, warnings not blocks",
           "[plugin][package][p12]" )
{
    const std::string pkgRoot = "/tmp/exprs_test_pkg_dep";
    ::system( "rm -rf /tmp/exprs_test_pkg_dep" );
    makePluginDir( pkgRoot, "org.test.depbase", "dep:echo" );

    PluginDiagnosticLog log;
    std::string installed;
    // Base plugin (the constraint target) is installed first.
    REQUIRE( exprs::PluginPackage::install( pkgRoot + "/org.test.depbase", installed, log ) );

    // A dependent package whose constraint matches the installed version.
    const std::string dependent = pkgRoot + "/org.test.depped";
    {
        ::mkdir( dependent.c_str(), 0755 );
        std::ofstream manifest( dependent + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.depped",
            "name": "Dependent",
            "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": [],
            "dependencies": ["org.test.depbase@^1.0.0"]
        })";
    }
    log = PluginDiagnosticLog();
    REQUIRE( exprs::PluginPackage::install( dependent, installed, log ) );
    bool sawSatisfiedInfo = false;
    for ( const auto &item : log.items() )
    {
        if ( item.severity == exprs::PluginDiagnosticSeverity::Info
             && item.message.find( "dependency satisfied: org.test.depbase@^1.0.0" )
                    != std::string::npos )
            sawSatisfiedInfo = true;
    }
    REQUIRE( sawSatisfiedInfo );

    // An unsatisfiable constraint installs but records a typed warning.
    {
        std::ofstream manifest( dependent + "/plugin.json", std::ios::trunc );
        manifest << R"({
            "manifest_version": 1,
            "id": "org.test.depped",
            "name": "Dependent",
            "version": "1.0.1",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "operators": [],
            "dependencies": ["org.test.depbase@^9.0.0"]
        })";
    }
    log = PluginDiagnosticLog();
    REQUIRE( exprs::PluginPackage::install( dependent, installed, log ) );
    bool sawTypedWarning = false;
    for ( const auto &item : log.items() )
    {
        if ( item.code == exprs::PluginDiagnosticCode::DependencyUnresolved
             && item.severity == exprs::PluginDiagnosticSeverity::Warning
             && item.message.find( "org.test.depbase@^9.0.0" ) != std::string::npos )
            sawTypedWarning = true;
    }
    REQUIRE( sawTypedWarning );

    REQUIRE( exprs::PluginPackage::uninstall( "org.test.depped", log ) );
    REQUIRE( exprs::PluginPackage::uninstall( "org.test.depbase", log ) );
    ::system( "rm -rf /tmp/exprs_test_pkg_dep" );
}

// -- plugin-platform 9.0: offline plugin index ---------------------------------

TEST_CASE( "offline index scans, filters compatibility and honors pins",
           "[plugin][index][p12]" )
{
    namespace fs = std::filesystem;
    const std::string root = "/tmp/exprs_test_index";
    ::system( "rm -rf /tmp/exprs_test_index" );
    std::error_code ec;
    fs::create_directories( fs::path( root ) / "good", ec );
    fs::create_directories( fs::path( root ) / "badapi", ec );
    fs::create_directories( fs::path( root ) / "badplatform", ec );

    auto writeManifest = []( const std::string &dir, const std::string &api,
                             const std::string &platformsJson ) {
        std::ofstream out( dir + "/plugin.json", std::ios::trunc );
        out << R"({
            "manifest_version": 1,
            "id": "org.test.index.)" << dir << R"(",
            "name": "Index Fixture",
            "version": "1.2.3",
            "api_version": ")" << api << R"(",
            "abi_version": 1,
            "entrypoint_kind": "manifest",
            "platforms": )" << platformsJson << R"(,
            "operators": []
        })";
    };
    // (ids must differ; build per-directory manifests explicitly)
    {
        std::ofstream out( root + "/good/plugin.json", std::ios::trunc );
        out << R"({
            "manifest_version": 1, "id": "org.test.index.good",
            "name": "Good", "version": "1.2.3",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(", "abi_version": 1,
            "entrypoint_kind": "manifest", "operators": []
        })";
    }
    {
        std::ofstream out( root + "/badapi/plugin.json", std::ios::trunc );
        out << R"({
            "manifest_version": 1, "id": "org.test.index.badapi",
            "name": "BadApi", "version": "1.0.0",
            "api_version": "9.9", "abi_version": 1,
            "entrypoint_kind": "manifest", "operators": []
        })";
    }
    {
        std::ofstream out( root + "/badplatform/plugin.json", std::ios::trunc );
        out << R"({
            "manifest_version": 1, "id": "org.test.index.badplatform",
            "name": "BadPlatform", "version": "1.0.0",
            "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(", "abi_version": 1,
            "entrypoint_kind": "manifest", "platforms": ["atarist"],
            "operators": []
        })";
    }

    const Json::Value index = exprs::PluginIndex::build( { root }, "2026-09-12T00:00:00Z" );
    REQUIRE( index["host"]["platform"].isString() );
    REQUIRE( index["generatedAt"].asString() == "2026-09-12T00:00:00Z" );
    const Json::Value &plugins = index["plugins"];
    REQUIRE( plugins.size() == 3 );
    // Sorted by id, deterministic output.
    REQUIRE( plugins[0]["id"].asString() < plugins[1]["id"].asString() );
    REQUIRE( plugins[1]["id"].asString() < plugins[2]["id"].asString() );
    for ( const Json::Value &plugin : plugins )
    {
        if ( plugin["id"].asString() == "org.test.index.good" )
        {
            REQUIRE( plugin["compatible"].asBool() );
            REQUIRE( plugin["reason"].asString().empty() );
        }
        else if ( plugin["id"].asString() == "org.test.index.badapi" )
        {
            REQUIRE_FALSE( plugin["compatible"].asBool() );
            REQUIRE( plugin["reason"].asString().find( "api_version" ) != std::string::npos );
        }
        else
        {
            REQUIRE_FALSE( plugin["compatible"].asBool() );
            REQUIRE( plugin["reason"].asString().find( "platform" ) != std::string::npos );
        }
    }

    // Pins are a pure annotation: pinned matches / upgrade / downgrade.
    std::map<std::string, std::string> pins;
    pins["org.test.index.good"] = "1.2.3";   // exact
    pins["org.test.index.badapi"] = "2.0.0"; // newer than available -> upgrade
    const Json::Value annotated = exprs::PluginIndex::applyPins( index, pins );
    for ( const Json::Value &plugin : annotated["plugins"] )
    {
        const std::string id = plugin["id"].asString();
        if ( id == "org.test.index.good" )
            REQUIRE( plugin["pin"].asString() == "pinned" );
        else if ( id == "org.test.index.badapi" )
            REQUIRE( plugin["pin"].asString() == "upgrade" );
        else
            REQUIRE( plugin["pin"].asString() == "unpinned" );
    }

    ::system( "rm -rf /tmp/exprs_test_index" );
}
