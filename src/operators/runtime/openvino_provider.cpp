// src/operators/runtime/openvino_provider.cpp — optional OpenVINO provider.
//
// Framework token "openvino": the artifact is an IR (.xml, with .bin beside
// it) or a directly readable ONNX model. Execution: ov::Core::read_model →
// compile (device from the platform's device planner: "CPU" fallback, "GPU"
// when the manifest asked for it) → one infer request per forward. CPU
// inference is deterministic per build; the provider details record the
// OpenVINO version honestly.
#include "operators/runtime/openvino_provider.h"

#ifdef SICNU_WITH_OPENVINO

#include "operators/framework/rs_operator_error.h"

#include <openvino/openvino.hpp>

#include <opencv2/core.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace sicnu::operators::runtime {

namespace {

class OpenVinoRuntime final : public IModelRuntime
{
  public:
    bool load( const ModelInfo &model, std::string *error )
    {
      try
      {
        mCore = std::make_unique<ov::Core>();
        const std::string &path = model.resolvedArtifactPath;
        // read_model handles both IR (.xml, with .bin beside it) and ONNX
        // directly; no format branch is needed.
        mModel = mCore->read_model( path );
        if ( !mModel )
        {
          if ( error )
            *error = "OpenVINO read_model failed for: " + path;
          return false;
        }
        const std::string device = model.runtime.gpu ? "GPU" : "CPU";
        mCompiled = mCore->compile_model( mModel, device );
        mInfer = mCompiled.create_infer_request();
        mDevice = device;
        // Remember the input/output names for the blob bridge.
        mInputName = mModel->input().get_any_name();
        mInputShape = mModel->input().get_shape();
        mOutputName = mModel->output().get_any_name();
        return true;
      }
      catch ( const std::exception &e )
      {
        if ( error )
          *error = std::string( "OpenVINO load failed: " ) + e.what();
        return false;
      }
    }

    std::string framework() const override { return "openvino"; }
    std::string backendName() const override { return "openvino"; }
    std::string deviceName() const override { return mDevice; }
    std::string artifactPath() const override { return mArtifact; }

    ProviderRuntimeDetails providerDetails() const override
    {
      ProviderRuntimeDetails details;
      details.executionProvider = "openvino";
      details.runtimeVersion = ov::get_openvino_version().buildNumber;
      return details;
    }

    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      try
      {
        // Bridge NCHW float32 into the declared input shape.
        ov::Tensor input( ov::element::f32, mInputShape );
        float *dst = input.data<float>();
        const std::size_t elems = nchwBlob.total() * nchwBlob.channels();
        std::memcpy( dst, nchwBlob.ptr<const float>(), elems * sizeof( float ) );
        mInfer.set_tensor( mInputName, input );
        mInfer.infer();
        const ov::Tensor output = mInfer.get_tensor( mOutputName );
        ov::Shape shape = output.get_shape();
        std::vector<int> dims( shape.begin(), shape.end() );
        cv::Mat out( static_cast<int>( dims.size() ), dims.data(), CV_32F );
        std::memcpy( out.ptr<float>(), output.data<const float>(),
                     output.get_byte_size() );
        return out;
      }
      catch ( const std::exception &e )
      {
        throw RSOperatorError( ErrorCode::RuntimeProviderFailed,
                               std::string( "OpenVINO inference failed: " ) + e.what() );
      }
    }

  private:
    std::string mArtifact = "openvino-model";
    std::string mDevice = "CPU";
    std::string mInputName;
    std::string mOutputName;
    ov::Shape mInputShape;
    std::unique_ptr<ov::Core> mCore;
    std::shared_ptr<ov::Model> mModel;
    ov::CompiledModel mCompiled;
    ov::InferRequest mInfer;
};

ModelRuntimePtr makeOpenVinoRuntime( const ModelInfo &model,
                                     const ModelHardwareCapabilities &hw,
                                     std::string *errorMessage )
{
  if ( model.resolvedArtifactPath.empty() )
  {
    if ( errorMessage )
      *errorMessage = "model has no resolved artifact path";
    return nullptr;
  }
  auto session = std::make_shared<OpenVinoRuntime>();
  if ( !session->load( model, errorMessage ) )
    return nullptr;
  ( void )hw;
  return session;
}

} // namespace

bool openVinoProviderAvailable()
{
  return true;
}

void registerOpenVinoProvider( ModelRuntimeRegistry &registry )
{
  registry.registerProvider( "openvino", makeOpenVinoRuntime, ProviderTraits{} );
}

std::string openVinoUnavailableReason()
{
  return {};
}

} // namespace sicnu::operators::runtime

#else // !SICNU_WITH_OPENVINO

namespace sicnu::operators::runtime
{

bool openVinoProviderAvailable()
{
  return false;
}

void registerOpenVinoProvider( ModelRuntimeRegistry & )
{
  // Graceful degradation: no OpenVINO in this build. Models declaring
  // framework "openvino" surface runtime_unavailable (UnsupportedRuntime).
}

std::string openVinoUnavailableReason()
{
  return "this build was compiled without OpenVINO (SICNU_WITH_OPENVINO off or the "
         "dependency was not found); use the onnx/onnxruntime frameworks here";
}

} // namespace sicnu::operators::runtime

#endif
