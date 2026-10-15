// tests/test_r14_C_plugin_registration.cpp — R14-C FIX-5/FIX-6 regressions.
//
// FIX-5 (data-provider take-over): DataProviderRegistry::registerProvider
// let plugin B silently REPLACE plugin A's provider and re-stamp ownership —
// the data sibling of the operator hijack #1462 fixed (e0664f65f). A later
// revoke of A then removed nothing (the entry was B's), while B never knew it
// owned a provider it did not install.
//
// FIX-6 (builtin shadowing): both operator install paths (manifest
// installPluginOperator and binary registerOperatorFactory) registered into
// RSOperatorRegistry unconditionally, and that registry is last-write-wins —
// a manifest-only plugin declaring "rs:ndvi" silently replaced the BUILTIN
// rs:ndvi factory on the JobEngine direct path, and revoking the plugin later
// deleted the builtin's registration.
#include <catch2/catch_test_macros.hpp>

#include "plugins/framework/data_provider_registry.h"
#include "plugins/framework/plugin_runtime_host.h"

#include "exprs/plugin_registry.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <fstream>
#include <memory>
#include <string>

#ifdef _WIN32
#include "exprs/msvc_posix_shim.h"
#include <cstdlib>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace exprs;

namespace {

void mkdirs( const std::string &path )
{
    std::string current;
    size_t start = 0;
    while ( start <= path.size() )
    {
        const size_t next = path.find( '/', start );
        current = path.substr( 0, next == std::string::npos ? path.size() : next );
        if ( !current.empty() )
            ::mkdir( current.c_str(), 0755 );
        if ( next == std::string::npos )
            break;
        start = next + 1;
    }
}

struct StubDataProvider : exprs::IPluginDataProviderV1
{
    std::string marker;
    explicit StubDataProvider( std::string m ) : marker( std::move( m ) ) {}
    Json::Value discover( const Json::Value & ) override { return Json::arrayValue; }
    Json::Value inspect( const std::string & ) override { return Json::objectValue; }
    Json::Value open( const std::string & ) override
    {
        Json::Value v( Json::objectValue );
        v["marker"] = marker;
        return v;
    }
    Json::Value capabilities() const override { return Json::objectValue; }
};

/// Registers a counting "builtin" factory under @p id the same channel the
/// REGISTER_RS_OPERATOR static initializers use, and unregisters it on scope
/// exit so process-wide registry state stays clean.
struct BuiltinGuard
{
    int calls = 0;
    explicit BuiltinGuard( const std::string &id )
    {
        sicnu::operators::RSOperatorRegistry::instance().registerOperator(
            id, [this]() -> std::unique_ptr<sicnu::operators::RSOperator> {
                ++calls;
                return nullptr;
            } );
        id_ = id;
    }
    ~BuiltinGuard() { sicnu::operators::RSOperatorRegistry::instance().unregisterOperator( id_ ); }

private:
    std::string id_;
};

struct HostGuard
{
    ~HostGuard()
    {
        PluginRegistry::instance().unloadAll();
        PluginRegistry::instance().setContributionSink( nullptr );
    }
};

/// A manifest-only external-tool plugin whose operator id is caller-chosen
/// (makeExternalPlugin's shape, with the id exposed for shadowing probes).
std::string makeShadowPlugin( const std::string &root, const std::string &suffix,
                              const std::string &operatorId )
{
    const std::string pluginId = "org.test.r14c-shadow" + suffix;
    const std::string dir = root + "/" + pluginId;
    mkdirs( dir );
    std::ofstream output( dir + "/plugin.json", std::ios::trunc );
    output << R"({
        "manifest_version": 1,
        "id": ")" << pluginId << R"(",
        "name": "Shadow Probe",
        "version": "1.0.0",
        "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
        "abi_version": 1,
        "entrypoint_kind": "manifest",
        "capabilities": ["operator", "external_tools"],
        "permissions": ["external_process", "filesystem_read"],
        "operators": [{
            "id": ")" << operatorId << R"(",
            "display_name": "Shadow Probe",
            "group": "test",
            "inputs": [],
            "outputs": [],
            "external": { "argv": ["/bin/echo", "-n", "shadow"], "timeout_seconds": 30 }
        }]
    })";
    return dir;
}

} // namespace

TEST_CASE( "a different plugin cannot take over an owned data provider id (R14-C FIX-5)",
           "[plugins][registry][r14_C]" )
{
    auto &registry = sicnu::plugins::DataProviderRegistry::instance();
    const std::string providerId = "org.test.r14c-provider";
    const std::string ownerA = "org.test.r14c-owner-a";
    const std::string ownerB = "org.test.r14c-owner-b";
    struct Cleanup
    {
        ~Cleanup()
        {
            sicnu::plugins::DataProviderRegistry::instance().unregisterPlugin( "org.test.r14c-owner-a" );
            sicnu::plugins::DataProviderRegistry::instance().unregisterPlugin( "org.test.r14c-owner-b" );
        }
    } cleanup;

    auto providerA = std::make_shared<StubDataProvider>( "A" );
    auto providerB = std::make_shared<StubDataProvider>( "B" );
    REQUIRE( registry.registerProvider( ownerA, providerId, "A", "", { "r14c" }, providerA ) );

    // The take-over is refused — not silently accepted with re-stamped
    // ownership.
    REQUIRE_FALSE( registry.registerProvider( ownerB, providerId, "B", "", { "r14c" }, providerB ) );

    // A's provider survived untouched.
    const auto found = registry.find( providerId );
    REQUIRE( found != nullptr );
    REQUIRE( found->open( {} )["marker"].asString() == "A" );

    // The OWNER may re-register its own entry (hot reload).
    auto providerA2 = std::make_shared<StubDataProvider>( "A2" );
    REQUIRE( registry.registerProvider( ownerA, providerId, "A", "", { "r14c" }, providerA2 ) );
    REQUIRE( registry.find( providerId )->open( {} )["marker"].asString() == "A2" );
}

TEST_CASE( "a binary plugin cannot shadow a builtin operator id (R14-C FIX-6)",
           "[plugins][host][r14_C]" )
{
    auto &host = sicnu::plugins::PluginRuntimeHost::instance();
    const std::string opId = "test:r14c_builtin_shadow";
    BuiltinGuard builtin( opId );
    struct RevokeAtEnd
    {
        ~RevokeAtEnd()
        {
            sicnu::plugins::PluginRuntimeHost::instance().revokePlugin( "org.test.r14c-shadower" );
        }
    } cleanup;

    int pluginCalls = 0;
    auto pluginFactory = [&pluginCalls]() -> std::unique_ptr<sicnu::operators::RSOperator> {
        ++pluginCalls;
        return nullptr;
    };

    // The shadowing registration is refused.
    REQUIRE_FALSE( host.registerOperatorFactory( "org.test.r14c-shadower", opId, pluginFactory ) );

    // The builtin survived: the registry still resolves to its factory and
    // the host never adopted the id as a plugin operator.
    REQUIRE( sicnu::operators::RSOperatorRegistry::instance().create( opId ) == nullptr );
    REQUIRE( builtin.calls >= 1 );
    REQUIRE( pluginCalls == 0 );
    REQUIRE_FALSE( host.isPluginOperator( opId ) );
}

TEST_CASE( "a manifest operator cannot shadow a builtin operator id (R14-C FIX-6)",
           "[plugins][host][r14_C]" )
{
    HostGuard guard;
    const std::string opId = "test:r14c_manifest_shadow";
    BuiltinGuard builtin( opId );

    const std::string root = "/tmp/exprs_test_r14c_shadow";
    ::system( ( "rm -rf " + root ).c_str() );
    makeShadowPlugin( root, "-manifest", opId );

    exprs::PluginRegistryOptions options;
    options.roots = { root };
    sicnu::plugins::bootstrapPluginRuntime( options );

    // The shadowing manifest's operator was refused: the builtin factory is
    // still the one the direct registry path runs, no atomic-catalog adapter
    // was installed for the id, and the host did not adopt it.
    REQUIRE( builtin.calls == 0 ); // nothing displaced the builtin lazily...
    REQUIRE( sicnu::operators::RSOperatorRegistry::instance().create( opId ) == nullptr );
    REQUIRE( builtin.calls == 1 ); // ...and create() ran the BUILTIN factory
    REQUIRE( sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( opId )
             == nullptr );
    REQUIRE_FALSE( sicnu::plugins::PluginRuntimeHost::instance().isPluginOperator( opId ) );
}
