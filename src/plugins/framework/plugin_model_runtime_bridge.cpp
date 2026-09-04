/***************************************************************************
 * src/plugins/framework/plugin_model_runtime_bridge.cpp
 ***************************************************************************/
#include "plugin_model_runtime_bridge.h"

#include "operators/runtime/model_runtime.h"

#include <map>
#include <mutex>

namespace sicnu::plugins {

namespace {

std::mutex &factoryMutex()
{
    static std::mutex instance;
    return instance;
}

struct StoredFactory
{
    std::string pluginId;
    exprs::PluginModelRuntimeFactoryV1 factory;
};

std::map<std::string, StoredFactory> &storedFactories()
{
    static std::map<std::string, StoredFactory> instance;
    return instance;
}

#if defined( SICNU_HAS_OPENCV )
/// Adapter: exprs::IPluginModelRuntimeV1 -> sicnu::operators::runtime::IModelRuntime.
class PluginModelRuntimeAdapter : public sicnu::operators::runtime::IModelRuntime
{
public:
    PluginModelRuntimeAdapter( std::string framework, std::string artifactPath,
                               std::unique_ptr<exprs::IPluginModelRuntimeV1> runtime )
        : mFramework( std::move( framework ) )
        , mArtifactPath( std::move( artifactPath ) )
        , mRuntime( std::move( runtime ) )
    {
    }

    std::string framework() const override { return mFramework; }
    std::string backendName() const override { return mRuntime->backendName(); }
    std::string deviceName() const override { return mRuntime->deviceName(); }
    std::string artifactPath() const override { return mArtifactPath; }

    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
        return infer( nchwBlob, {} );
    }

    cv::Mat infer( const cv::Mat &nchwBlob, const std::string &outputName ) override
    {
        exprs::PluginTensorV1 input;
        input.batch = nchwBlob.dims >= 4 ? nchwBlob.size[0] : 1;
        input.channels = nchwBlob.dims >= 4 ? nchwBlob.size[1]
                                            : ( nchwBlob.channels() > 0 ? nchwBlob.channels() : 1 );
        input.rows = nchwBlob.dims >= 4 ? nchwBlob.size[2] : nchwBlob.rows;
        input.cols = nchwBlob.dims >= 4 ? nchwBlob.size[3] : nchwBlob.cols;
        const size_t total = static_cast<size_t>( input.batch ) * input.channels * input.rows
                             * input.cols;
        input.data.resize( total );
        if ( nchwBlob.type() == CV_32F )
        {
            std::memcpy( input.data.data(), nchwBlob.ptr<float>(),
                         total * sizeof( float ) );
        }
        else
        {
            cv::Mat converted;
            nchwBlob.convertTo( converted, CV_32F );
            std::memcpy( input.data.data(), converted.ptr<float>(), total * sizeof( float ) );
        }

        const exprs::PluginInferenceResultV1 result = mRuntime->infer( input, outputName );
        if ( !result.success )
            throw std::runtime_error( result.error );

        cv::Mat output( 4, std::vector<int>{ result.output.batch, result.output.channels,
                                             result.output.rows, result.output.cols }
                               .data(),
                        CV_32F, const_cast<float *>( result.output.data.data() ) );
        return output.clone();
    }

    std::vector<std::string> outputTensorNames() const override
    {
        return mRuntime->outputTensorNames();
    }

private:
    std::string mFramework;
    std::string mArtifactPath;
    std::unique_ptr<exprs::IPluginModelRuntimeV1> mRuntime;
};
#endif

} // namespace

void storePluginModelRuntimeFactory( const std::string &framework, const std::string &pluginId,
                                     exprs::PluginModelRuntimeFactoryV1 factory )
{
    std::lock_guard<std::mutex> lock( factoryMutex() );
    storedFactories()[framework] = StoredFactory{ pluginId, std::move( factory ) };
}

void clearPluginModelRuntimeFactory( const std::string &framework )
{
    std::lock_guard<std::mutex> lock( factoryMutex() );
    storedFactories().erase( framework );
}

#if defined( SICNU_HAS_OPENCV )
bool registerPluginModelRuntime( const std::string &framework, const std::string &pluginId )
{
    sicnu::operators::runtime::ModelRuntimeRegistry::instance().registerProvider(
        framework,
        [framework, pluginId]( const sicnu::operators::ModelInfo &model,
                               const sicnu::operators::runtime::ModelHardwareCapabilities &hw,
                               std::string *errorMessage )
            -> sicnu::operators::runtime::ModelRuntimePtr {
            (void)hw;
            exprs::PluginModelRuntimeFactoryV1 factory;
            {
                std::lock_guard<std::mutex> lock( factoryMutex() );
                auto iterator = storedFactories().find( framework );
                if ( iterator == storedFactories().end() )
                {
                    if ( errorMessage )
                        *errorMessage = "plugin runtime '" + framework
                                        + "' has no loaded plugin backend";
                    return nullptr;
                }
                factory = iterator->second.factory;
            }
            if ( !factory )
            {
                if ( errorMessage )
                    *errorMessage = "plugin runtime factory unavailable";
                return nullptr;
            }
            exprs::PluginModelRequestV1 request;
            request.modelName = model.name;
            request.artifactPath = !model.resolvedArtifactPath.empty() ? model.resolvedArtifactPath
                                              : model.artifact.path;
            request.manifest = model.toJson();
            request.gpuRequested = hw.cudaAvailable;
            std::string error;
            exprs::PluginModelRuntimePtrV1 runtime = factory( request, error );
            if ( !runtime )
            {
                if ( errorMessage )
                    *errorMessage = error.empty() ? "plugin runtime factory failed" : error;
                return nullptr;
            }
            return std::make_shared<PluginModelRuntimeAdapter>( framework, model.artifact.path,
                                                                std::move( runtime ) );
        } );
    (void)pluginId;
    return true;
}
#endif

} // namespace sicnu::plugins
