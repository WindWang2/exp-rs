// examples/plugins/agent-tool-demo/plugin.cpp
//
// Agent tool plugin: contributes one MCP/agent-callable tool
// ("demo:uniform_sample") with a structured schema and bounded results.
#include "exprs/plugin_interface.h"

#include <json/json.h>

#include <cmath>
#include <random>
#include <stdexcept>

namespace {

Json::Value errorResult( const std::string &message, const std::string &code )
{
    Json::Value failure( Json::objectValue );
    failure["success"] = false;
    Json::Value error( Json::objectValue );
    error["message"] = message;
    error["code"] = code;
    error["category"] = "validation";
    error["retryable"] = false;
    failure["error"] = error;
    return failure;
}

class UniformSampleTool : public exprs::IPluginAgentToolV1
{
public:
    Json::Value execute( const Json::Value &params ) override
    {
        const Json::Value &count = params["count"];
        if ( !count.isIntegral() || count.asInt64() < 1 || count.asInt64() > 1000 )
            return errorResult( "parameter 'count' must be an integer in [1, 1000]",
                                "INVALID_PARAMETER" );
        double low = 0.0;
        double high = 1.0;
        if ( params.isMember( "low" ) && params["low"].isNumeric() )
            low = params["low"].asDouble();
        if ( params.isMember( "high" ) && params["high"].isNumeric() )
            high = params["high"].asDouble();
        if ( !( high > low ) )
            return errorResult( "'high' must be greater than 'low'", "INVALID_PARAMETER" );

        std::mt19937_64 engine{ std::random_device{}() };
        std::uniform_real_distribution<double> distribution( low, high );
        Json::Value samples( Json::arrayValue );
        const int n = static_cast<int>( count.asInt64() );
        for ( int index = 0; index < n; ++index )
            samples.append( distribution( engine ) );

        Json::Value result( Json::objectValue );
        result["success"] = true;
        result["samples"] = samples;
        result["truncated"] = false;
        return result;
    }
};

class AgentToolDemoPlugin : public exprs::PluginV1
{
public:
    std::string pluginId() const override { return "org.example.agent-tool-demo"; }

    bool initialize( exprs::HostServicesV1 & ) override { return true; }

    void registerContributions( exprs::ContributionContextV1 &context ) override
    {
        context.registerAgentTool( "demo:uniform_sample",
                                   std::make_shared<UniformSampleTool>() );
    }

    void shutdown() override {}
};

} // namespace

EXPRS_EXPORT_PLUGIN( AgentToolDemoPlugin )
