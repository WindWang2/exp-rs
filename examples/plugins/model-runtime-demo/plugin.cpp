// examples/plugins/model-runtime-demo/plugin.cpp
//
// Model runtime plugin: registers a demo "demo-null" framework. The runtime
// ignores the artifact and returns a constant tensor — enough to prove the
// ModelRuntimeRegistry extension path without shipping real weights.
#include "exprs/plugin_interface.h"

#include <algorithm>
#include <stdexcept>

namespace {

class DemoNullRuntime : public exprs::IPluginModelRuntimeV1
{
public:
    explicit DemoNullRuntime( const exprs::PluginModelRequestV1 &request )
        : mArtifact( request.artifactPath )
    {
    }

    std::string backendName() const override { return "demo_null"; }
    std::string deviceName() const override { return "cpu"; }

    bool load( const exprs::PluginModelRequestV1 &, std::string & ) override { return true; }

    exprs::PluginInferenceResultV1 infer( const exprs::PluginTensorV1 &input,
                                          const std::string & ) override
    {
        exprs::PluginInferenceResultV1 result;
        result.success = true;
        result.output = input; // identity "model"
        result.diagnostics["backend"] = "demo_null";
        return result;
    }

    std::vector<std::string> outputTensorNames() const override
    {
        return { "output" };
    }

private:
    std::string mArtifact;
};

class ModelRuntimeDemoPlugin : public exprs::PluginV1
{
public:
    std::string pluginId() const override { return "org.example.model-runtime-demo"; }

    bool initialize( exprs::HostServicesV1 & ) override { return true; }

    void registerContributions( exprs::ContributionContextV1 &context ) override
    {
        context.registerModelRuntime(
            "demo-null",
            []( const exprs::PluginModelRequestV1 &request,
                std::string & ) -> exprs::PluginModelRuntimePtrV1 {
                return std::make_unique<DemoNullRuntime>( request );
            } );
    }

    void shutdown() override {}
};

} // namespace

EXPRS_EXPORT_PLUGIN( ModelRuntimeDemoPlugin )
