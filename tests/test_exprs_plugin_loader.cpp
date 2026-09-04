// tests/test_exprs_plugin_loader.cpp — native loading with a real fixture .so
#include <catch2/catch_test_macros.hpp>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_registry.h"
#include "exprs/plugin_validator.h"
#include "exprs/version.h"

#include "operators/framework/rs_operator_context.h"

#include <fstream>
#include <memory>
#include <string>
#include <sys/stat.h>

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
