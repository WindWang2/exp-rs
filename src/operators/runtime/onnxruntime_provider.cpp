// src/operators/runtime/onnxruntime_provider.cpp
// Compiled always; the ORT-dependent body is guarded by SICNU_WITH_ONNX_RUNTIME
// (see the header for the graceful-degradation contract).
#include "onnxruntime_provider.h"

#include "operators/runtime/model_runtime.h"

#ifdef SICNU_WITH_ONNX_RUNTIME

#include <onnxruntime_cxx_api.h>

#include <opencv2/core.hpp>

#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace sicnu::operators::runtime
{

namespace
{

/// Loads one ONNX session through the ORT C++ API and executes forward
/// passes. Single- and multi-input; outputs follow the graph's head order.
class OnnxRuntimeSession final : public IModelRuntime
{
  public:
    OnnxRuntimeSession( std::string artifactPath, bool modelWantsGpu,
                        const ModelHardwareCapabilities &hw )
        : m_artifactPath( std::move( artifactPath ) ),
          m_useCuda( modelWantsGpu && hw.cudaAvailable )
    {
    }

    bool load( std::string *errorMessage )
    {
      auto fail = [errorMessage]( const std::string &why ) {
        if ( errorMessage )
          *errorMessage = why;
        return false;
      };
      try
      {
        m_env = std::make_unique<Ort::Env>( ORT_LOGGING_LEVEL_WARNING, "exp-rs" );
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel( GraphOptimizationLevel::ORT_ENABLE_ALL );
        if ( m_useCuda )
        {
          // CUDA EP is opt-in through ORT's own build; when unavailable ORT
          // throws here and we surface the failure instead of silently
          // changing devices.
          options.AppendExecutionProvider_CUDA( 0 );
        }
        m_session = std::make_unique<Ort::Session>( *m_env, m_artifactPath.c_str(), options );
      }
      catch ( const Ort::Exception &e )
      {
        return fail( std::string( "failed to load ONNX Runtime session: " ) + e.what() );
      }
      m_deviceName = m_useCuda ? "cuda" : "cpu";
      m_loaded = true;
      return true;
    }

    std::string framework() const override { return "onnxruntime"; }
    std::string backendName() const override { return "onnxruntime"; }
    std::string deviceName() const override { return m_deviceName; }
    std::string artifactPath() const override { return m_artifactPath; }

    std::vector<std::string> inputTensorNames() const
    {
      std::vector<std::string> names;
      const size_t count = m_session->GetInputCount();
      for ( size_t i = 0; i < count; ++i )
        names.emplace_back( m_session->GetInputNameAllocated( i, m_ortAllocator ).get() );
      return names;
    }

    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      return inferMulti( { NamedBlob{ std::string(), nchwBlob } } ).at( 0 );
    }

    bool supportsMultiInput() const override { return true; }

    std::vector<cv::Mat> inferMulti( const std::vector<NamedBlob> &namedBlobs ) override
    {
      if ( !m_loaded )
        throw std::runtime_error( "runtime session is not loaded" );
      if ( namedBlobs.empty() )
        throw std::runtime_error( "multi-input inference needs at least one input blob" );

      std::lock_guard<std::mutex> lock( m_mutex );
      const std::vector<std::string> graphInputs = inputTensorNames();
      if ( graphInputs.size() != namedBlobs.size() )
        throw std::runtime_error( "model expects " + std::to_string( graphInputs.size() ) +
                                  " inputs, " + std::to_string( namedBlobs.size() ) +
                                  " were fed" );

      std::vector<Ort::Value> inputValues;
      std::vector<std::vector<int64_t>> shapes;
      std::vector<std::vector<float>> buffers;
      inputValues.reserve( namedBlobs.size() );
      shapes.reserve( namedBlobs.size() );
      buffers.reserve( namedBlobs.size() );
      for ( size_t i = 0; i < namedBlobs.size(); ++i )
      {
        const cv::Mat &blob = namedBlobs[i].second;
        if ( blob.empty() || blob.dims != 4 || blob.type() != CV_32F )
          throw std::runtime_error( "input blobs must be 4-D CV_32F NCHW ('" +
                                    namedBlobs[i].first + "')" );
        const std::vector<int64_t> shape = { blob.size[0], blob.size[1], blob.size[2],
                                             blob.size[3] };
        const size_t total = static_cast<size_t>( blob.total() );
        buffers.emplace_back( blob.ptr<float>(), blob.ptr<float>() + total );
        inputValues.emplace_back(
          Ort::MemoryInfo::CreateCpu( OrtAllocatorType::OrtArenaAllocator,
                                      OrtMemType::OrtMemTypeDefault ),
          buffers.back().data(), buffers.back().size(), shape );
        shapes.push_back( shape );
      }

      std::vector<const char *> inputNames;
      for ( const std::string &n : graphInputs )
        inputNames.push_back( n.c_str() );
      std::vector<const char *> outputNames;
      const size_t outputCount = m_session->GetOutputCount();
      for ( size_t i = 0; i < outputCount; ++i )
        outputNames.emplace_back(
          m_session->GetOutputNameAllocated( i, m_ortAllocator ).get() );

      auto outputs = m_session->Run( Ort::RunOptions{ nullptr }, inputNames.data(),
                                     inputValues.data(), inputValues.size(),
                                     outputNames.data(), outputNames.size() );

      std::vector<cv::Mat> result;
      for ( auto &value : outputs )
      {
        const auto info = value.GetTensorTypeAndShapeInfo();
        const auto shape = info.GetShape(); // expect 4-D NCHW
        if ( shape.size() != 4 )
          throw std::runtime_error( "ONNX Runtime output is not 4-D NCHW" );
        const int n = static_cast<int>( shape[0] );
        const int c = static_cast<int>( shape[1] );
        const int h = static_cast<int>( shape[2] );
        const int w = static_cast<int>( shape[3] );
        cv::Mat mat( 4, std::vector<int>{ n, c, h, w }.data(), CV_32F );
        std::memcpy( mat.ptr<float>(), value.GetTensorMutableData<float>(),
                     static_cast<size_t>( n ) * c * h * w * sizeof( float ) );
        result.push_back( mat );
      }
      return result;
    }

    std::vector<std::string> outputTensorNames() const override
    {
      std::vector<std::string> names;
      if ( !m_loaded )
        return names;
      const size_t count = m_session->GetOutputCount();
      for ( size_t i = 0; i < count; ++i )
        names.emplace_back( m_session->GetOutputNameAllocated( i, m_ortAllocator ).get() );
      return names;
    }

  private:
    std::string m_artifactPath;
    bool m_useCuda = false;
    bool m_loaded = false;
    std::string m_deviceName = "cpu";
    std::mutex m_mutex;
    Ort::AllocatorWithDefaultOptions m_ortAllocator;
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
};

ModelRuntimePtr makeOnnxRuntime( const ModelInfo &model, const ModelHardwareCapabilities &hw,
                                 std::string *errorMessage )
{
  auto session = std::make_shared<OnnxRuntimeSession>( model.resolvedArtifactPath,
                                                       model.runtime.gpu, hw );
  if ( !session->load( errorMessage ) )
    return nullptr;
  return session;
}

} // namespace

bool onnxRuntimeProviderAvailable()
{
  return true;
}

void registerOnnxRuntimeProvider()
{
  ModelRuntimeRegistry::instance().registerProvider( "onnxruntime", makeOnnxRuntime );
}

std::string onnxRuntimeUnavailableReason()
{
  return {};
}

} // namespace sicnu::operators::runtime

#else // !SICNU_WITH_ONNX_RUNTIME

namespace sicnu::operators::runtime
{

bool onnxRuntimeProviderAvailable()
{
  return false;
}

void registerOnnxRuntimeProvider()
{
  // Graceful degradation: no ONNX Runtime in this build. Models declaring
  // framework "onnxruntime" stay catalog-ready=false at the runtime layer
  // with UnsupportedRuntime / runtime_unavailable.
}

std::string onnxRuntimeUnavailableReason()
{
  return "this build was compiled without ONNX Runtime "
         "(SICNU_WITH_ONNX_RUNTIME off or the dependency was not found); "
         "use the opencv-dnn 'onnx' framework or rebuild with ONNX Runtime";
}

} // namespace sicnu::operators::runtime

#endif
