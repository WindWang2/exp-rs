// examples/plugins/cpp-operator-demo/plugin.cpp
#include "demo_operator.h"

#include "exprs/plugin_interface.h"

#include <json/json.h>

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace org_example {

Json::Value DemoStatsOperator::run( const Json::Value &params,
                                    sicnu::operators::RSOperatorContext &context )
{
    context.throwIfCancelled();

    const Json::Value &values = params["values"];
    if ( !values.isArray() || values.empty() )
    {
        throw sicnu::operators::RSOperatorError(
            sicnu::operators::ErrorCode::InvalidParameter,
            "parameter 'values' must be a non-empty array of numbers" );
    }

    std::vector<double> numbers;
    numbers.reserve( values.size() );
    for ( const Json::Value &value : values )
    {
        if ( !value.isNumeric() )
        {
            throw sicnu::operators::RSOperatorError(
                sicnu::operators::ErrorCode::TypeMismatch,
                "parameter 'values' must contain only numbers" );
        }
        numbers.push_back( value.asDouble() );
    }

    const double sum = std::accumulate( numbers.begin(), numbers.end(), 0.0 );
    Json::Value result( Json::objectValue );
    result["success"] = true;
    result["count"] = static_cast<Json::Int64>( numbers.size() );
    result["sum"] = sum;
    result["mean"] = sum / static_cast<double>( numbers.size() );
    result["min"] = *std::min_element( numbers.begin(), numbers.end() );
    result["max"] = *std::max_element( numbers.begin(), numbers.end() );
    if ( params.isMember( "label" ) && params["label"].isString() )
        result["label"] = params["label"];
    context.reportProgress( 1.0, "demo stats complete" );
    return result;
}

} // namespace org_example

namespace {

/// The plugin object owns the plugin lifecycle and registers the operator
/// through the ContributionContext handed to it by the host.
class DemoStatsPlugin : public exprs::PluginV1
{
public:
    std::string pluginId() const override { return "org.example.cpp-operator-demo"; }

    bool initialize( exprs::HostServicesV1 &services ) override
    {
        services.log( "info", "cpp-operator-demo initialized" );
        return true;
    }

    void registerContributions( exprs::ContributionContextV1 &context ) override
    {
        context.registerOperatorFactory(
            "demo:stats", []() -> std::unique_ptr<sicnu::operators::RSOperator> {
                return std::make_unique<org_example::DemoStatsOperator>();
            } );
    }

    void shutdown() override {}
};

} // namespace

EXPRS_EXPORT_PLUGIN( DemoStatsPlugin )
