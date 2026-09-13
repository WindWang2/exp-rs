// src/operators/runtime/tensorrt_provider.cpp — optional TensorRT provider.
//
// Framework token "tensorrt": the artifact is a PREBUILT serialized engine
// (.engine/.plan, TRT version-locked by design — a rebuilt engine is a
// different artifact digest, which the package identity already handles).
// Execution: one float32 NCHW input binding, one float32 output binding,
// enqueueV3 + stream sync. GPU-only by nature; the resolved CUDA index from
// the device planner selects the CUDA device through the runtime's own
// device ordinal (TRT has no multi-runtime enumeration here).
//
// HONEST STATUS: this translation unit only compiles when TensorRT is
// present at configure time; on hosts without it the stub below keeps the
// typed-unavailability contract. The compiled path is capability-gated in
// the tests (SKIP with reason when the provider is absent).
#include "operators/runtime/tensorrt_provider.h"

#include "operators/framework/rs_operator_error.h"

#ifdef SICNU_WITH_TENSORRT

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include <opencv2/core.hpp>

#include <QDebug>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace sicnu::operators::runtime {

namespace {

/// TRT v8+ logger sink (error/warning only — the platform logs verdicts).
class TrtLogger final : public nvinfer1::ILogger
{
  public:
    void log( Severity severity, const char *msg ) noexcept override
    {
      if ( severity >= Severity::kWARNING )
        qWarning( "tensorrt: %s", msg );
    }
};

TrtLogger &trtLogger()
{
  static TrtLogger logger;
  return logger;
}

struct EngineDeleter
{
    void operator()( nvinfer1::ICudaEngine *e ) const { if ( e ) e->destroy(); }
    void operator()( nvinfer1::IExecutionContext *c ) const { if ( c ) c->destroy(); }
};

class TensorRtRuntime final : public IModelRuntime
{
  public:
    TensorRtRuntime( const std::string &enginePath, int cudaIndex )
      : m_cudaIndex( cudaIndex ) { ( void )enginePath; }

    bool load( const std::string &enginePath, std::string *error )
    {
      std::ifstream file( enginePath, std::ios::binary );
      if ( !file )
      {
        if ( error )
          *error = "cannot open TensorRT engine file: " + enginePath;
        return false;
      }
      std::vector<char> blob( ( std::istreambuf_iterator<char>( file ) ),
                              std::istreambuf_iterator<char>() );
      mRuntime.reset( nvinfer1::createInferRuntime( trtLogger() ) );
      if ( !mRuntime )
      {
        if ( error )
          *error = "createInferRuntime failed";
        return false;
      }
      mEngine.reset( mRuntime->deserializeCudaEngine( blob.data(), blob.size() ) );
      if ( !mEngine )
      {
        if ( error )
          *error = "deserializeCudaEngine failed (engine/plan file does not match this "
                   "TensorRT build or GPU?)";
        return false;
      }
      mContext.reset( mEngine->createExecutionContext() );
      if ( !mContext )
      {
        if ( error )
          *error = "createExecutionContext failed";
        return false;
      }
      if ( cudaSetDevice( m_cudaIndex ) != cudaSuccess )
      {
        if ( error )
          *error = "cudaSetDevice failed for index " + std::to_string( m_cudaIndex );
        return false;
      }
      if ( cudaStreamCreate( &mStream ) != cudaSuccess )
      {
        if ( error )
          *error = "cudaStreamCreate failed";
        return false;
      }
      return true;
    }

    ~TensorRtRuntime() override
    {
      if ( mStream )
        cudaStreamDestroy( mStream );
    }

    std::string framework() const override { return "tensorrt"; }
    std::string backendName() const override { return "tensorrt"; }
    std::string deviceName() const override
    {
      return m_cudaIndex > 0 ? "cuda:" + std::to_string( m_cudaIndex ) : "cuda";
    }
    std::string artifactPath() const override { return mArtifact; }

    ProviderRuntimeDetails providerDetails() const override
    {
      ProviderRuntimeDetails details;
      details.executionProvider = "tensorrt";
      details.runtimeVersion = std::to_string( NV_TENSORRT_MAJOR ) + "."
        + std::to_string( NV_TENSORRT_MINOR ) + "." + std::to_string( NV_TENSORRT_PATCH );
      return details;
    }

    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      // Single-input, single-output contract: bindings 0/1 by index order.
      const int inputIndex = 0;
      const int outputIndex = 1;
      const std::size_t inputElems = static_cast<std::size_t>( nchwBlob.total() ) * nchwBlob.channels();
      void *deviceInput = nullptr;
      void *deviceOutput = nullptr;
      const std::size_t outputElems = [ & ] {
        nvinfer1::Dims dims = mContext->getBindingDimensions( outputIndex );
        std::size_t n = 1;
        for ( int i = 0; i < dims.nbDims; ++i )
          n *= static_cast<std::size_t>( std::max( 1, dims.d[i] ) );
        return n;
      }();
      const std::size_t outputBytes = outputElems * sizeof( float );

      if ( cudaMalloc( &deviceInput, inputElems * sizeof( float ) ) != cudaSuccess
           || cudaMalloc( &deviceOutput, outputBytes ) != cudaSuccess )
        throw RSOperatorError( ErrorCode::ComputationError,
                               "TensorRT cudaMalloc failed (input/output bindings)" );
      cudaMemcpyAsync( deviceInput, nchwBlob.ptr<const float>(),
                       inputElems * sizeof( float ), cudaMemcpyHostToDevice, mStream );
      void *bindings[2] = { deviceInput, deviceOutput };
      const bool ok = mContext->enqueueV2( bindings, mStream, nullptr );
      cudaStreamSynchronize( mStream );
      cv::Mat out;
      if ( ok )
      {
        nvinfer1::Dims dims = mContext->getBindingDimensions( outputIndex );
        std::vector<int> shape( dims.d, dims.d + dims.nbDims );
        out.create( static_cast<int>( shape.size() ), shape.data(), CV_32F );
        cudaMemcpy( out.ptr<float>(), deviceOutput, outputBytes, cudaMemcpyDeviceToHost );
      }
      cudaFree( deviceInput );
      cudaFree( deviceOutput );
      if ( !ok )
        throw RSOperatorError( ErrorCode::RuntimeProviderFailed,
                               "TensorRT enqueueV2 failed — see the runtime log" );
      return out;
    }

  private:
    std::string mArtifact = "tensorrt-engine";
    int m_cudaIndex = 0;
    cudaStream_t mStream = nullptr;
    std::unique_ptr<nvinfer1::IRuntime> mRuntime;
    std::unique_ptr<nvinfer1::ICudaEngine, EngineDeleter> mEngine;
    std::unique_ptr<nvinfer1::IExecutionContext, EngineDeleter> mContext;
};

ModelRuntimePtr makeTensorRtRuntime( const ModelInfo &model,
                                     const ModelHardwareCapabilities &hw,
                                     std::string *errorMessage )
{
  if ( model.resolvedArtifactPath.empty() )
  {
    if ( errorMessage )
      *errorMessage = "model has no resolved artifact path";
    return nullptr;
  }
  int cudaIndex = model.runtime.resolvedCudaIndex;
  if ( cudaIndex < 0 )
    cudaIndex = 0;
  auto session = std::make_shared<TensorRtRuntime>( model.resolvedArtifactPath, cudaIndex );
  if ( !session->load( model.resolvedArtifactPath, errorMessage ) )
    return nullptr;
  ( void )hw;
  return session;
}

} // namespace

bool tensorRTProviderAvailable()
{
  return true;
}

void registerTensorRTProvider( ModelRuntimeRegistry &registry )
{
  registry.registerProvider( "tensorrt", makeTensorRtRuntime,
                             ProviderTraits{ /*maxAddressableCudaIndex*/ 63 } );
}

std::string tensorRTUnavailableReason()
{
  return {};
}

} // namespace sicnu::operators::runtime

#else // !SICNU_WITH_TENSORRT

namespace sicnu::operators::runtime
{

bool tensorRTProviderAvailable()
{
  return false;
}

void registerTensorRTProvider( ModelRuntimeRegistry & )
{
  // Graceful degradation: no TensorRT in this build. Models declaring
  // framework "tensorrt" surface runtime_unavailable (UnsupportedRuntime).
}

std::string tensorRTUnavailableReason()
{
  return "this build was compiled without TensorRT (SICNU_WITH_TENSORRT off or the "
         "dependency was not found); prebuilt engines are a GPU-deployment option — "
         "use the onnx/onnxruntime frameworks here";
}

} // namespace sicnu::operators::runtime

#endif
