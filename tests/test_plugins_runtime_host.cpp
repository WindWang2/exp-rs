// tests/test_plugins_runtime_host.cpp — manifest contribution installation,
// external tool operators, and agent tool execution through the host bridge.
#include <catch2/catch_test_macros.hpp>

#include "plugins/framework/data_provider_registry.h"
#include "plugins/framework/external_tool_operator.h"
#include "plugins/framework/plugin_agent_tool_provider.h"
#include "plugins/framework/plugin_runtime_host.h"

#include "exprs/plugin_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

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

std::string makeExternalPlugin( const std::string &root )
{
    const std::string dir = root + "/org.test.ext-echo";
    mkdirs( dir );
    std::ofstream output( dir + "/plugin.json", std::ios::trunc );
    output << R"({
        "manifest_version": 1,
        "id": "org.test.ext-echo",
        "name": "Ext Echo",
        "version": "1.0.0",
        "api_version": ")" << EXP_RS_PLUGIN_API_VERSION << R"(",
        "abi_version": 1,
        "entrypoint_kind": "manifest",
        "capabilities": ["operator", "external_tools", "agent_tool"],
        "permissions": ["external_process", "filesystem_read"],
        "operators": [{
            "id": "test:ext_echo",
            "display_name": "Ext Echo",
            "group": "test",
            "inputs": [{ "name": "text", "type": "string", "required": true }],
            "outputs": [],
            "external": { "argv": ["/bin/echo", "-n", "ECHOED:${text}"], "timeout_seconds": 30 }
        }],
        "agent_tools": [{
            "id": "test:noop",
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
