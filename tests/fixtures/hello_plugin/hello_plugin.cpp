// tests/fixtures/hello_plugin/hello_plugin.cpp
// Fixture native plugin for loader/registry integration tests.
#include "exprs/plugin_interface.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"

#include <json/json.h>

namespace {

class HelloOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "test:hello"; }
    std::string displayName() const override { return "Test Hello"; }
    std::string group() const override { return "test"; }
    std::string description() const override { return "Fixture operator for loader tests"; }
    std::string determinismGrade() const override { return "bit_exact"; }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        return schema;
    }

    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context ) override
    {
        context.reportProgress( 1.0, "hello" );
        Json::Value result( Json::objectValue );
        result["success"] = true;
        result["hello"] = params.get( "name", "world" );
        return result;
    }
};

class HelloPlugin : public exprs::PluginV1
{
public:
    std::string pluginId() const override { return "org.exprs.test.hello-plugin"; }
    bool initialize( exprs::HostServicesV1 & ) override { return true; }

    void registerContributions( exprs::ContributionContextV1 &context ) override
    {
        context.registerOperatorFactory(
            "test:hello", []() -> std::unique_ptr<sicnu::operators::RSOperator> {
                return std::make_unique<HelloOperator>();
            } );
    }

    void shutdown() override { mShutdownCalled = true; }
    bool mShutdownCalled = false;
};

} // namespace

EXPRS_EXPORT_PLUGIN( HelloPlugin )
