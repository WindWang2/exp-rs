// tests/test_plugins_runtime_host.cpp — manifest contribution installation,
// external tool operators, and agent tool execution through the host bridge.
#include <catch2/catch_test_macros.hpp>

#include "plugins/framework/data_provider_registry.h"
#include "plugins/framework/external_tool_operator.h"
#include "plugins/framework/plugin_agent_tool_provider.h"
#include "plugins/framework/plugin_execution_barrier.h"
#include "plugins/framework/plugin_runtime_host.h"

#include "exprs/plugin_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <fstream>

#include <sys/stat.h>
#ifdef _WIN32
#include <cstdlib> // _exit

#ifdef _WIN32
#include "exprs/msvc_posix_shim.h"
#endif
#else
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

std::string makeExternalPlugin( const std::string &root, const std::string &suffix = {} )
{
    // Distinct plugin/operator ids per test: the execution barrier and the
    // process-wide registries keep permanent state for unloaded plugins.
    const std::string pluginId = "org.test.ext-echo" + suffix;
    const std::string operatorId = "test:ext_echo" + suffix;
    const std::string dir = root + "/" + pluginId;
    mkdirs( dir );
    std::ofstream output( dir + "/plugin.json", std::ios::trunc );
    output << R"({
        "manifest_version": 1,
        "id": ")" << pluginId << R"(",
        "name": "Ext Echo",
        "version": "1.0.0",
        "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
        "abi_version": 1,
        "entrypoint_kind": "manifest",
        "capabilities": ["operator", "external_tools", "agent_tool"],
        "permissions": ["external_process", "filesystem_read"],
        "operators": [{
            "id": ")" << operatorId << R"(",
            "display_name": "Ext Echo",
            "group": "test",
            "inputs": [{ "name": "text", "type": "string", "required": true }],
            "outputs": [],
            "external": { "argv": ["/bin/echo", "-n", "ECHOED:${text}"], "timeout_seconds": 30 }
        }],
        "agent_tools": [{
            "id": ")" << operatorId + "_noop" << R"(",
            "display_name": "Noop",
            "input_schema": { "type": "object", "properties": {} }
        }]
    })";
    return dir;
}

struct HostGuard
{
    ~HostGuard()
    {
        PluginRegistry::instance().unloadAll();
        PluginRegistry::instance().setContributionSink( nullptr );
    }
};
} // namespace

TEST_CASE( "runtime host installs manifest contributions and executes them", "[plugins][host]" )
{
    HostGuard guard;
    const std::string root = "/tmp/exprs_test_host";
    ::system( ( "rm -rf " + root ).c_str() );
    makeExternalPlugin( root );

    exprs::PluginRegistryOptions options;
    options.roots = { root };
    sicnu::plugins::bootstrapPluginRuntime( options );

    // The plugin operator is discoverable through the canonical registry.
    auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( "test:ext_echo" );
    REQUIRE( adapter != nullptr );
    REQUIRE( adapter->descriptor().id == "test:ext_echo" );
    REQUIRE( adapter->descriptor().inputs.size() == 1 );

    // Executing through the adapter runs the external tool (lazy plugin path).
    Json::Value params;
    params["text"] = "hello";
    const Json::Value result = adapter->execute( params, nullptr, nullptr );
    REQUIRE( result.get( "success", false ).asBool() );
    REQUIRE( result.get( "stdout", "" ).asString() == "ECHOED:hello" );

    // Manifest-only external tool missing a parameter fails closed.
    Json::Value emptyParams( Json::objectValue );
    const Json::Value failure = adapter->execute( emptyParams, nullptr, nullptr );
    REQUIRE_FALSE( failure.get( "success", true ).asBool() );
}

TEST_CASE( "external tool operator publishes declared outputs transactionally", "[plugins][host]" )
{
    exprs::ManifestOperator declaration;
    declaration.id = "test:writer";
    declaration.displayName = "Writer";
    declaration.hasExternalTool = true;
    declaration.external.argv = { "/bin/sh", "-c", "printf 'data' > ${output}" };
    declaration.external.timeoutSeconds = 30;
    exprs::ManifestPort output;
    output.name = "output";
    output.type = "string";
    output.required = true;
    declaration.outputs.push_back( output );

    sicnu::plugins::ExternalToolOperator writer( "test:writer", declaration, "/tmp" );
    sicnu::operators::RSOperatorContext context;

    Json::Value params;
    params["output"] = "/tmp/exprs_test_writer_out.txt";
    ::unlink( params["output"].asCString() );

    const Json::Value result = writer.run( params, context );
    REQUIRE( result.get( "success", false ).asBool() );
    REQUIRE( result["output"].asString() == "/tmp/exprs_test_writer_out.txt" );

    std::ifstream check( "/tmp/exprs_test_writer_out.txt" );
    std::string content( ( std::istreambuf_iterator<char>( check ) ),
                         std::istreambuf_iterator<char>() );
    REQUIRE( content == "data" );
    ::unlink( "/tmp/exprs_test_writer_out.txt" );

    SECTION( "failing tools leave no published output" )
    {
        sicnu::plugins::ExternalToolOperator failing( "test:writer", declaration, "/tmp" );
        Json::Value failParams;
        failParams["output"] = "/tmp/exprs_test_writer_fail.txt";
        declaration.external.argv = { "/bin/sh", "-c", "exit 1" };
        sicnu::plugins::ExternalToolOperator failing2( "test:writer", declaration, "/tmp" );
        REQUIRE_THROWS_AS( failing2.run( failParams, context ),
                           sicnu::operators::RSOperatorError );
        std::ifstream missing( "/tmp/exprs_test_writer_fail.txt" );
        REQUIRE_FALSE( missing.good() );
    }
}

TEST_CASE( "unload refuses while an execution lease is active (issue #747)",
           "[plugins][host][unload]" )
{
    HostGuard guard;
    const std::string root = "/tmp/exprs_test_host_unload";
    ::system( ( "rm -rf " + root ).c_str() );
    makeExternalPlugin( root, "-unload" );

    exprs::PluginRegistryOptions options;
    options.roots = { root };
    sicnu::plugins::bootstrapPluginRuntime( options );

    const std::string pluginId = "org.test.ext-echo-unload";
    const std::string operatorId = "test:ext_echo-unload";
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    REQUIRE( registry.load( pluginId ) );
    REQUIRE( registry.isLoaded( pluginId ) );
    REQUIRE( sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( operatorId )
             != nullptr ); // registered at bootstrap, untouched by load

    // An in-flight execution holds an owner-scoped lease (exactly what
    // PluginOperatorAdapter::execute holds while the plugin's run() is on
    // the stack). Unload must NOT unmap code under it.
    auto &barrier = sicnu::plugins::PluginExecutionBarrier::instance();
    auto lease = barrier.acquire( pluginId );
    REQUIRE( lease );

    REQUIRE_FALSE( registry.unload( pluginId, 200 ) );
    REQUIRE( registry.isLoaded( pluginId ) );
    const PluginRecord *record = registry.record( pluginId );
    REQUIRE( record );
    REQUIRE( record->state == PluginState::Loaded );
    bool sawInUse = false;
    for ( const auto &item : registry.diagnostics().forPlugin( pluginId ) )
        sawInUse = sawInUse || item.code == PluginDiagnosticCode::PluginInUse;
    REQUIRE( sawInUse );
    // Refused unload reopens the barrier: the plugin is still usable, and a
    // re-dispatched execution runs end to end.
    REQUIRE_FALSE( barrier.isRefusing( pluginId ) );
    {
        auto adapter =
            sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( operatorId );
        REQUIRE( adapter != nullptr );
        Json::Value params;
        params["text"] = "again";
        const Json::Value result = adapter->execute( params, nullptr, nullptr );
        REQUIRE( result.get( "success", false ).asBool() );
    }

    // The execution drains: unload proceeds, contributions are revoked and
    // the barrier stays closed (stale handles refuse with a typed failure).
    lease.reset();
    REQUIRE( registry.unload( pluginId, 2000 ) );
    REQUIRE_FALSE( registry.isLoaded( pluginId ) );
    REQUIRE( sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( operatorId )
             == nullptr );
    REQUIRE( barrier.isRefusing( pluginId ) );
    REQUIRE_FALSE( barrier.acquire( pluginId ) );
}

TEST_CASE( "enable after unload restores contributions and execution (issue #755 round-trip)",
           "[plugins][host][roundtrip]" )
{
    HostGuard guard;
    const std::string root = "/tmp/exprs_test_host_rt";
    ::system( ( "rm -rf " + root ).c_str() );
    makeExternalPlugin( root, "-rt" );

    exprs::PluginRegistryOptions options;
    options.roots = { root };
    sicnu::plugins::bootstrapPluginRuntime( options );

    const std::string pluginId = "org.test.ext-echo-rt";
    const std::string operatorId = "test:ext_echo-rt";
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    auto &atomic = sicnu::processing::AtomicAlgorithmRegistry::instance();
    auto &barrier = sicnu::plugins::PluginExecutionBarrier::instance();

    auto executeOk = [&atomic, &operatorId]( const std::string &text ) {
        auto adapter = atomic.findAdapter( operatorId );
        REQUIRE( adapter != nullptr );
        Json::Value params;
        params["text"] = text;
        return adapter->execute( params, nullptr, nullptr ).get( "success", false ).asBool();
    };

    // Load → execute → unload → enable/load → execute → unload (epic flow).
    REQUIRE( registry.load( pluginId ) );
    REQUIRE( executeOk( "first" ) );
    REQUIRE( registry.unload( pluginId ) );
    REQUIRE( atomic.findAdapter( operatorId ) == nullptr );
    REQUIRE( barrier.isRefusing( pluginId ) );

    // Re-enable: load must reopen the barrier AND reinstall the manifest
    // contributions (P0 review finding — neither happened before).
    REQUIRE( registry.setEnabled( pluginId, true ) );
    REQUIRE( registry.load( pluginId ) );
    REQUIRE_FALSE( barrier.isRefusing( pluginId ) );
    REQUIRE( executeOk( "second" ) );

    // And a full second unload closes cleanly again.
    REQUIRE( registry.unload( pluginId ) );
    REQUIRE( atomic.findAdapter( operatorId ) == nullptr );
}

TEST_CASE( "direct RSOperatorRegistry path is lease-guarded (issue #747)",
           "[plugins][host][direct]" )
{
    HostGuard guard;
    const std::string root = "/tmp/exprs_test_host_direct";
    ::system( ( "rm -rf " + root ).c_str() );
    makeExternalPlugin( root, "-direct" );

    exprs::PluginRegistryOptions options;
    options.roots = { root };
    sicnu::plugins::bootstrapPluginRuntime( options );

    const std::string pluginId = "org.test.ext-echo-direct";
    const std::string operatorId = "test:ext_echo-direct";
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    auto &direct = sicnu::operators::RSOperatorRegistry::instance();
    auto &barrier = sicnu::plugins::PluginExecutionBarrier::instance();

    REQUIRE( registry.load( pluginId ) );

    // Direct creation works while open, and the produced operator holds a
    // lease for its whole lifetime (create → run → destroy).
    {
        auto op = direct.create( operatorId );
        REQUIRE( op != nullptr );
        REQUIRE( barrier.activeCount( pluginId ) == 1 );
        sicnu::operators::RSOperatorContext context;
        Json::Value params;
        params["text"] = "direct";
        const Json::Value result = op->run( params, context );
        REQUIRE( result.get( "success", false ).asBool() );
    }
    REQUIRE( barrier.activeCount( pluginId ) == 0 ); // released on destruction

    // While draining, the direct path refuses with a null operator — the
    // same typed-failure contract JobEngine already handles.
    barrier.beginDrain( pluginId );
    REQUIRE( direct.create( operatorId ) == nullptr );
    barrier.cancelDrain( pluginId );
    REQUIRE( direct.create( operatorId ) != nullptr );
}
