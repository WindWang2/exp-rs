// src/operators/runtime/onnxruntime_provider.cpp
// Compiled always; the ORT-dependent body is guarded by SICNU_WITH_ONNX_RUNTIME
// (see the header for the graceful-degradation contract).
//
// Platform 7.0: the session binds inputs by NAME (manifest input contracts
// match the graph's own tensor names; positional feeding is only allowed
// when the caller omits names), carries N-D tensors (rank 1..6) with exact
// dtypes, and implements the full runtime contract — warmup, in-forward
// cancellation through ORT RunOptions::SetTerminate, health and honest
// memory estimates. The resolved CUDA index from the registry's device
// resolution is honored through the CUDA execution provider's device id.
#include "onnxruntime_provider.h"

#include "operators/runtime/model_runtime.h"

#ifdef SICNU_WITH_ONNX_RUNTIME

#include <onnxruntime_cxx_api.h>

#include <QFileInfo>

#include <opencv2/core.hpp>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace sicnu::operators::runtime
{

namespace
{

/// Maps a runtime dtype onto the ORT element-type enum (creation side).
bool ortElemTypeForDType( TensorDType dtype, ONNXTensorElementDataType *out )
{
  switch ( dtype )
  {
    case TensorDType::Float32: *out = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT; return true;
    case TensorDType::Float64: *out = ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE; return true;
    case TensorDType::Int32: *out = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32; return true;
    case TensorDType::Int64: *out = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64; return true;
    case TensorDType::UInt8: *out = ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8; return true;
    case TensorDType::Int8: *out = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8; return true;
    case TensorDType::Float16: return false; // carried as raw bits elsewhere; refused here
  }
  return false;
}

/// Maps an ORT element type back onto the runtime dtype (output side).
bool dTypeForOrtElemType( ONNXTensorElementDataType type, TensorDType *out )
{
  switch ( type )
  {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: *out = TensorDType::Float32; return true;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE: *out = TensorDType::Float64; return true;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: *out = TensorDType::Int32; return true;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: *out = TensorDType::Int64; return true;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8: *out = TensorDType::UInt8; return true;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8: *out = TensorDType::Int8; return true;
    default: return false; // float16/string/bf16 outputs: typed refusal, never bit-cast
  }
}

TensorBlob tensorFromOrtValue( Ort::Value &value, const std::string &name )
{
  const auto info = value.GetTensorTypeAndShapeInfo();
  const auto ortShape = info.GetShape();
  TensorDType dtype = TensorDType::Float32;
  if ( !dTypeForOrtElemType( info.GetElementType(), &dtype ) )
    throw std::runtime_error( "output '" + name + "' has an dtype this runtime refuses to "
                              "bit-cast (element type " +
                              std::to_string( static_cast<int>( info.GetElementType() ) ) + ")" );
  TensorBlob blob;
  blob.dtype = dtype;
  blob.shape.assign( ortShape.begin(), ortShape.end() );
  if ( blob.shape.empty() )
    throw std::runtime_error( "output '" + name + "' carries no shape" );
  blob.bytes.resize( blob.expectedByteCount() );
  switch ( dtype )
  {
    case TensorDType::Float32:
      std::memcpy( blob.bytes.data(), value.GetTensorMutableData<float>(), blob.bytes.size() );
      break;
    case TensorDType::Float64:
      std::memcpy( blob.bytes.data(), value.GetTensorMutableData<double>(), blob.bytes.size() );
      break;
    case TensorDType::Int32:
      std::memcpy( blob.bytes.data(), value.GetTensorMutableData<std::int32_t>(), blob.bytes.size() );
      break;
    case TensorDType::Int64:
      std::memcpy( blob.bytes.data(), value.GetTensorMutableData<std::int64_t>(), blob.bytes.size() );
      break;
    case TensorDType::UInt8:
      std::memcpy( blob.bytes.data(), value.GetTensorMutableData<std::uint8_t>(), blob.bytes.size() );
      break;
    case TensorDType::Int8:
      std::memcpy( blob.bytes.data(), value.GetTensorMutableData<std::int8_t>(), blob.bytes.size() );
      break;
    case TensorDType::Float16:
      throw std::runtime_error( "output '" + name + "' float16 transport refused" );
  }
  return blob;
}

/// Loads one ONNX session through the ORT C++ API and executes forward
/// passes. Single- and multi-input, N-D, named bind; outputs follow the
/// graph's head order.
class OnnxRuntimeSession final : public IModelRuntime
{
  public:
    OnnxRuntimeSession( std::string artifactPath, bool modelWantsGpu, int resolvedCudaIndex,
                        const ModelHardwareCapabilities &hw )
        : m_artifactPath( std::move( artifactPath ) ),
          m_useCuda( modelWantsGpu && hw.cudaAvailable ),
          m_cudaIndex( resolvedCudaIndex )
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
          // Bind to exactly the device the registry's resolution picked (the
          // cache key was built from it). When the CUDA EP or the index is
          // unavailable ORT throws here and we surface the failure instead of
          // silently running on another device.
          options.AppendExecutionProvider_CUDA( m_cudaIndex );
        }
        m_session = std::make_unique<Ort::Session>( *m_env, m_artifactPath.c_str(), options );
      }
      catch ( const Ort::Exception &e )
      {
        return fail( std::string( "failed to load ONNX Runtime session: " ) + e.what() );
      }
      m_deviceName = m_useCuda ? "cuda:" + std::to_string( m_cudaIndex ) : "cpu";
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

    // --- Historical cv::Mat surface (raster engines' fast path) -------------
    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      return inferMatMulti( { NamedBlob{ std::string(), nchwBlob } } ).at( 0 );
    }

    bool supportsMultiInput() const override { return true; }

    std::vector<cv::Mat> inferMulti( const std::vector<NamedBlob> &namedBlobs ) override
    {
      return inferMatMulti( namedBlobs );
    }

    // --- Platform 7.0 N-D named surface (THE entry point) --------------------
    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      if ( !m_loaded )
        throw std::runtime_error( "runtime session is not loaded" );
      if ( inputs.empty() )
        throw std::runtime_error( "multi-input inference needs at least one input tensor" );
      if ( m_cancelRequested.load( std::memory_order_relaxed ) )
        throw std::runtime_error( "inference canceled before the forward pass" );

      std::lock_guard<std::mutex> lock( m_mutex );
      const std::vector<std::string> graphInputs = inputTensorNames();
      const std::vector<std::string> graphOutputs = outputTensorNames();
      // Named bind: a fed NAME must match a graph input name exactly; the
      // manifest input contract is the binding document. Positional feeding
      // applies only when the caller omits the name (the historical path).
      std::size_t positional = 0;
      std::vector<std::string> bindNames;
      bindNames.reserve( inputs.size() );
      for ( const NamedTensor &nt : inputs )
      {
        if ( nt.first.empty() )
        {
          if ( positional >= graphInputs.size() )
            throw std::runtime_error( "model expects " + std::to_string( graphInputs.size() )
                                      + " inputs; more were fed positionally" );
          bindNames.push_back( graphInputs[positional++] );
          continue;
        }
        bool matched = false;
        for ( const std::string &graph : graphInputs )
          matched = matched || graph == nt.first;
        if ( !matched )
          throw std::runtime_error( "input contract error: declared input '" + nt.first
                                    + "' matches no graph input (schema mismatch)" );
        bindNames.push_back( nt.first );
      }

      std::vector<Ort::Value> inputValues;
      std::vector<Ort::MemoryInfo> memoryInfos;
      inputValues.reserve( inputs.size() );
      memoryInfos.reserve( inputs.size() );
      for ( std::size_t i = 0; i < inputs.size(); ++i )
      {
        const TensorBlob &blob = inputs[i].second;
        if ( !blob.isValid() )
          throw std::runtime_error( "input '" + bindNames[i]
                                    + "' tensor is structurally invalid (shape/bytes mismatch)" );
        if ( blob.rank() > 6 )
          throw std::runtime_error( "input '" + bindNames[i] + "' rank "
                                    + std::to_string( blob.rank() )
                                    + " exceeds this provider's maximum rank (6)" );
        ONNXTensorElementDataType elemType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
        if ( !ortElemTypeForDType( blob.dtype, &elemType ) )
          throw std::runtime_error( "input '" + bindNames[i]
                                    + "' float16 transport refused (no exact ORT binding)" );
        ( void )elemType; // consumed through the CreateTensor dtype overloads below
        memoryInfos.push_back( Ort::MemoryInfo::CreateCpu( OrtAllocatorType::OrtArenaAllocator,
                                                           OrtMemType::OrtMemTypeDefault ) );
        const std::vector<std::int64_t> &shape = blob.shape;
        switch ( blob.dtype )
        {
          case TensorDType::Float32:
            inputValues.push_back( Ort::Value::CreateTensor<float>(
              memoryInfos.back(), reinterpret_cast<float *>( blob.bytes.data() ),
              blob.elementCount(), shape.data(), shape.size() ) );
            break;
          case TensorDType::Float64:
            inputValues.push_back( Ort::Value::CreateTensor<double>(
              memoryInfos.back(), reinterpret_cast<double *>( blob.bytes.data() ),
              blob.elementCount(), shape.data(), shape.size() ) );
            break;
          case TensorDType::Int32:
            inputValues.push_back( Ort::Value::CreateTensor<std::int32_t>(
              memoryInfos.back(), reinterpret_cast<std::int32_t *>( blob.bytes.data() ),
              blob.elementCount(), shape.data(), shape.size() ) );
            break;
          case TensorDType::Int64:
            inputValues.push_back( Ort::Value::CreateTensor<std::int64_t>(
              memoryInfos.back(), reinterpret_cast<std::int64_t *>( blob.bytes.data() ),
              blob.elementCount(), shape.data(), shape.size() ) );
            break;
          case TensorDType::UInt8:
            inputValues.push_back( Ort::Value::CreateTensor<std::uint8_t>(
              memoryInfos.back(), blob.bytes.data(), blob.elementCount(), shape.data(),
              shape.size() ) );
            break;
          case TensorDType::Int8:
            inputValues.push_back( Ort::Value::CreateTensor<std::int8_t>(
              memoryInfos.back(), reinterpret_cast<std::int8_t *>( blob.bytes.data() ),
              blob.elementCount(), shape.data(), shape.size() ) );
            break;
          case TensorDType::Float16:
            throw std::runtime_error( "input '" + bindNames[i] + "' float16 transport refused" );
        }
      }

      // Output selection: requested names (validated against the graph) or
      // the graph's full head order.
      std::vector<std::string> wanted = outputNames;
      if ( wanted.empty() )
        wanted = graphOutputs;
      if ( wanted.empty() )
        throw std::runtime_error( "ONNX Runtime graph exposes no output names" );
      for ( const std::string &w : wanted )
      {
        bool known = false;
        for ( const std::string &graph : graphOutputs )
          known = known || graph == w;
        if ( !known )
          throw std::runtime_error( "output contract error: requested head '" + w
                                    + "' matches no graph output" );
      }
      std::vector<const char *> inputNamePtrs;
      for ( const std::string &n : bindNames )
        inputNamePtrs.push_back( n.c_str() );
      std::vector<const char *> outputNamePtrs;
      for ( const std::string &n : wanted )
        outputNamePtrs.push_back( n.c_str() );

      Ort::RunOptions runOptions;
      {
        std::lock_guard<std::mutex> runLock( m_activeRunMutex );
        m_activeRunOptions = &runOptions;
      }
      std::vector<Ort::Value> outputs;
      try
      {
        outputs = m_session->Run( runOptions, inputNamePtrs.data(), inputValues.data(),
                                  inputValues.size(), outputNamePtrs.data(), outputNamePtrs.size() );
      }
      catch ( const Ort::Exception &e )
      {
        std::lock_guard<std::mutex> runLock( m_activeRunMutex );
        m_activeRunOptions = nullptr;
        if ( m_cancelRequested.load( std::memory_order_relaxed ) )
        {
          recordFailure( "inference canceled during the forward pass" );
          throw std::runtime_error( "inference canceled during the forward pass" );
        }
        recordFailure( e.what() );
        throw std::runtime_error( std::string( "forward pass failed: " ) + e.what() );
      }
      {
        std::lock_guard<std::mutex> runLock( m_activeRunMutex );
        m_activeRunOptions = nullptr;
      }

      if ( outputs.size() != wanted.size() )
      {
        recordFailure( "forward pass produced fewer outputs than requested" );
        throw std::runtime_error( "forward pass produced " + std::to_string( outputs.size() )
                                  + " outputs, " + std::to_string( wanted.size() )
                                  + " were requested (output invalid)" );
      }
      std::vector<NamedTensor> result;
      result.reserve( outputs.size() );
      for ( std::size_t i = 0; i < outputs.size(); ++i )
      {
        try
        {
          result.push_back( NamedTensor{ wanted[i], tensorFromOrtValue( outputs[i], wanted[i] ) } );
        }
        catch ( const std::exception &e )
        {
          recordFailure( e.what() );
          throw;
        }
      }
      m_forwards.fetch_add( 1, std::memory_order_relaxed );
      {
        std::lock_guard<std::mutex> healthLock( m_healthMutex );
        m_lastError.clear();
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

    ProviderCapabilities capabilities() const override
    {
      ProviderCapabilities caps;
      caps.multiInput = true;
      caps.namedBind = true;
      caps.maxRank = 6;
      caps.batch = true;
      caps.cancelInForward = true;
      caps.inputDtypes = { "float32", "float64", "int32", "int64", "uint8", "int8" };
      caps.outputDtypes = { "float32", "float64", "int32", "int64", "uint8", "int8" };
      return caps;
    }

    // --- Platform 4.0 unified contract ---------------------------------------
    /// Best-effort throwaway forward on a small probe tensor. Non-fatal by
    /// contract; the outcome lands in health(), never in load().
    void warmup() override
    {
      if ( !m_loaded )
        return;
      try
      {
        const std::vector<std::string> graphInputs = inputTensorNames();
        if ( graphInputs.empty() )
          return;
        std::vector<NamedTensor> probe;
        for ( const std::string &name : graphInputs )
          probe.push_back(
            NamedTensor{ name, TensorBlob::zeros( { 1, 3, 64, 64 }, TensorDType::Float32 ) } );
        inferNamed( probe, {} );
      }
      catch ( const std::exception &e )
      {
        std::lock_guard<std::mutex> healthLock( m_healthMutex );
        m_lastError = std::string( "warmup skipped: " ) + e.what();
      }
    }

    /// Cooperative cancel: checkpoints before the next forward AND terminates
    /// a forward already handed to ORT (RunOptions::SetTerminate — the EP
    /// polls it between kernel launches; granularity is ORT's, honest).
    void requestCancel() override
    {
      m_cancelRequested.store( true, std::memory_order_relaxed );
      std::lock_guard<std::mutex> runLock( m_activeRunMutex );
      if ( m_activeRunOptions )
        m_activeRunOptions->SetTerminate();
    }
    void clearCancel() override
    {
      m_cancelRequested.store( false, std::memory_order_relaxed );
      std::lock_guard<std::mutex> runLock( m_activeRunMutex );
      if ( m_activeRunOptions )
        m_activeRunOptions->UnsetTerminate();
    }

    SessionHealth health() const override
    {
      SessionHealth health;
      health.ok = m_loaded && !m_cancelRequested.load( std::memory_order_relaxed );
      health.forwardsCompleted = m_forwards.load( std::memory_order_relaxed );
      health.failures = m_failures.load( std::memory_order_relaxed );
      std::lock_guard<std::mutex> healthLock( m_healthMutex );
      health.lastError = m_lastError;
      return health;
    }

    SessionMemoryEstimate memoryEstimate() const override
    {
      // Honest estimate: the serialized weights are known; the engine's peak
      // working set is not discoverable through this API — 0 = unknown.
      SessionMemoryEstimate estimate;
      const QFileInfo info( QString::fromStdString( m_artifactPath ) );
      if ( info.exists() && info.isFile() )
        estimate.weightsMb = static_cast<int>( ( info.size() + ( 1 << 20 ) - 1 ) >> 20 );
      return estimate;
    }

  private:
    /// cv::Mat fast path with the historical rank-4 float32 output contract.
    std::vector<cv::Mat> inferMatMulti( const std::vector<NamedBlob> &namedBlobs )
    {
      std::vector<NamedTensor> inputs;
      inputs.reserve( namedBlobs.size() );
      for ( const NamedBlob &nb : namedBlobs )
      {
        if ( nb.second.empty() || nb.second.dims != 4 || nb.second.type() != CV_32F )
          throw std::runtime_error( "input blobs must be 4-D CV_32F NCHW ('" + nb.first
                                    + "')" );
        inputs.push_back( NamedTensor{ nb.first, TensorBlob::fromMat( nb.second ) } );
      }
      std::vector<NamedTensor> outputs = inferNamed( inputs, {} );
      std::vector<cv::Mat> mats;
      mats.reserve( outputs.size() );
      for ( NamedTensor &nt : outputs )
      {
        if ( nt.second.rank() != 4 )
          throw std::runtime_error( "ONNX Runtime output '" + nt.first
                                    + "' is not 4-D NCHW (the cv::Mat path carries rank-4; use "
                                      "inferNamed for N-D heads)" );
        mats.push_back( nt.second.toMat() );
      }
      return mats;
    }

    void recordFailure( const std::string &what )
    {
      m_failures.fetch_add( 1, std::memory_order_relaxed );
      std::lock_guard<std::mutex> healthLock( m_healthMutex );
      m_lastError = what;
    }

    std::string m_artifactPath;
    bool m_useCuda = false;
    int m_cudaIndex = 0;
    bool m_loaded = false;
    std::string m_deviceName = "cpu";
    std::mutex m_mutex; // serializes forward passes (ORT sessions are not thread-safe for Run)
    std::mutex m_activeRunMutex;
    Ort::RunOptions *m_activeRunOptions = nullptr;
    std::atomic<bool> m_cancelRequested{ false };
    std::atomic<std::uint64_t> m_forwards{ 0 };
    std::atomic<std::uint64_t> m_failures{ 0 };
    mutable std::mutex m_healthMutex;
    std::string m_lastError;
    Ort::AllocatorWithDefaultOptions m_ortAllocator;
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
};

ModelRuntimePtr makeOnnxRuntime( const ModelInfo &model, const ModelHardwareCapabilities &hw,
                                 std::string *errorMessage )
{
  auto session = std::make_shared<OnnxRuntimeSession>( model.resolvedArtifactPath,
                                                       model.runtime.gpu,
                                                       model.runtime.resolvedCudaIndex, hw );
  if ( !session->load( errorMessage ) )
    return nullptr;
  return session;
}

} // namespace

bool onnxRuntimeProviderAvailable()
{
  return true;
}

void registerOnnxRuntimeProvider( ModelRuntimeRegistry &registry )
{
  registry.registerProvider(
    "onnxruntime", makeOnnxRuntime,
    ProviderTraits{ /*maxAddressableCudaIndex*/ 63 } );
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

void registerOnnxRuntimeProvider( ModelRuntimeRegistry & )
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
