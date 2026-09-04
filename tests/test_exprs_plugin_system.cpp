// tests/test_exprs_plugin_system.cpp — discovery, registry, policy, package
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "exprs/external_process.h"
#include "exprs/plugin_discovery.h"
#include "exprs/plugin_package.h"
#include "exprs/plugin_permissions.h"
#include "exprs/plugin_registry.h"
#include "exprs/plugin_validator.h"
#include "exprs/version.h"

#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

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
