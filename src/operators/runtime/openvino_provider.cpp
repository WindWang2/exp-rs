// src/operators/runtime/openvino_provider.cpp — optional OpenVINO provider.
//
// Framework token "openvino": the artifact is an IR (.xml, with .bin beside
// it) or a directly readable ONNX model. Execution: ov::Core::read_model →
// compile (device follows the registry's resolution verdict — the single
// repair that verdict cannot make is supplied here: a GPU-wanting manifest
// demoted to cpu only because the host has no CUDA is served by OpenVINO's
// own GPU plugin when one is exposed) → one infer request per forward. CPU
// inference is deterministic per build; the provider details record the
// OpenVINO version honestly.
#include "operators/runtime/openvino_provider.h"

#ifdef SICNU_WITH_OPENVINO

#include "operators/framework/rs_operator_error.h"

#include <openvino/openvino.hpp>

#include <opencv2/core.hpp>

#include <QFileInfo>
#include <QString>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sicnu::operators::runtime {

namespace {

class OpenVinoRuntime final : public IModelRuntime
{
  public:
    bool load( const ModelInfo &model, const ModelHardwareCapabilities &hw, std::string *error )
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
        const std::string device = selectDevice( model, hw, error );
        if ( device.empty() )
          return false; // typed refusal — selectDevice filled *error
        mCompiled = mCore->compile_model( mModel, device );
        mInfer = mCompiled.create_infer_request();
        mDevice = device;
        mResolvedCudaIndex = model.runtime.resolvedCudaIndex;
        // R15-C C05 (B8): the REAL artifact path — the placeholder
        // "openvino-model" fabricated an identity for every payload/provenance
        // consumer (every other provider reports the path it loaded).
        mArtifact = path;
        mLoaded = true;
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
    std::string deviceName() const override
    {
      // R15-C C05 (B7): payloads and the provenance sidecar speak the
      // platform device vocabulary ("cpu" | "cuda:N", model_runtime.h).
      // OpenVINO's own tokens ("CPU"/"GPU") broke every consumer matching on
      // that vocabulary — and OV's GPU plugin is not necessarily a CUDA
      // device, so a bare "GPU" claim was as wrong as dropping the resolved
      // index. Report the platform token for the CPU plugin and, for the GPU
      // plugin, the device index the registry's resolver actually picked
      // (runtime.resolvedCudaIndex) in the platform spelling. Device 0 is
      // the DEFAULT/first GPU (resolvedCudaIndex is 0 both for it and for an
      // unresolved CPU pick), so the guard is >= 0 — "cuda:0" is the honest
      // platform spelling of "the GPU plugin ran on the first device".
      if ( mDevice == "CPU" )
        return std::string( "cpu" );
      if ( mResolvedCudaIndex >= 0 )
        return "cuda:" + std::to_string( mResolvedCudaIndex );
      return mDevice;
    }
    std::string artifactPath() const override { return mArtifact; }

    ProviderRuntimeDetails providerDetails() const override
    {
      ProviderRuntimeDetails details;
      details.executionProvider = "openvino";
      details.runtimeVersion = ov::get_openvino_version().buildNumber;
      return details;
    }

    // R15-C C05 (B9): the base class defaults reported every healthy
    // OpenVINO session as UNHEALTHY (ok=false) with unknown weights (0 MiB)
    // — the other four providers report honestly, so a health probe or a
    // memory report classified by provider gave contradictory answers for
    // the same model.
    SessionHealth health() const override
    {
      SessionHealth health;
      health.ok = mLoaded && !m_cancelRequested.load( std::memory_order_relaxed );
      health.forwardsCompleted = m_forwards.load( std::memory_order_relaxed );
      health.failures = m_failures.load( std::memory_order_relaxed );
      std::lock_guard<std::mutex> healthLock( m_healthMutex );
      health.lastError = m_lastError;
      return health;
    }

    SessionMemoryEstimate memoryEstimate() const override
    {
      // Honest estimate: the serialized weights are known (the IR .xml plus
      // its sibling .bin when present); the engine's peak working set is not
      // discoverable through this API — 0 = unknown, never invented.
      SessionMemoryEstimate estimate;
      const QFileInfo info( QString::fromStdString( mArtifact ) );
      if ( info.exists() && info.isFile() )
        estimate.weightsMb = static_cast<int>( ( info.size() + ( 1 << 20 ) - 1 ) >> 20 );
      if ( mArtifact.size() > 4
           && mArtifact.compare( mArtifact.size() - 4, 4, ".xml" ) == 0 )
      {
        const QFileInfo bin( QString::fromStdString( mArtifact.substr( 0, mArtifact.size() - 4 )
                                                     + ".bin" ) );
        if ( bin.exists() && bin.isFile() )
          estimate.weightsMb += static_cast<int>( ( bin.size() + ( 1 << 20 ) - 1 ) >> 20 );
      }
      return estimate;
    }

    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      // The registry hands ONE cached session to any worker thread; the
      // shared ov::InferRequest must never run concurrently (same
      // serialization contract as the ORT/OpenCV providers).
      std::lock_guard<std::mutex> lock( m_inferMutex );
      try
      {
        // Bridge NCHW float32 into the declared input shape — the sizes MUST
        // agree elementwise; a mismatch is a typed error, never an overflow.
        // The depth check precedes the element math: a CV_8U/CV_64F blob of
        // matching ELEMENT count would memcpy 4x/0.5x the real buffer (heap
        // overread) — the interface contract is float32 NCHW.
        if ( nchwBlob.empty() || nchwBlob.dims != 4 || nchwBlob.type() != CV_32F )
          throw RSOperatorError( ErrorCode::InvalidInputData,
                                 "OpenVINO input must be a 4-D float32 NCHW blob" );
        const std::size_t elems = nchwBlob.total() * nchwBlob.channels();
        std::size_t expected = 1;
        for ( std::int64_t d : mInputShape )
          expected *= static_cast<std::size_t>( d );
        if ( elems != expected )
          throw RSOperatorError( ErrorCode::InvalidInputData,
                                 "fed blob size does not match the OpenVINO input shape ("
                                   + std::to_string( expected ) + " elements expected)" );
        ov::Tensor input( ov::element::f32, mInputShape );
        std::memcpy( input.data<float>(), nchwBlob.ptr<const float>(), elems * sizeof( float ) );
        mInfer.set_tensor( mInputName, input );
        mInfer.infer();
        const ov::Tensor output = mInfer.get_tensor( mOutputName );
        if ( output.get_element_type() != ov::element::f32 )
          throw RSOperatorError( ErrorCode::ComputationError,
                                 "OpenVINO output must be float32 (got "
                                   + output.get_element_type().get_type_name() + ")" );
        ov::Shape shape = output.get_shape();
        std::vector<int> dims( shape.begin(), shape.end() );
        cv::Mat out( static_cast<int>( dims.size() ), dims.data(), CV_32F );
        const std::size_t outElems = static_cast<std::size_t>( out.total() ) * out.channels();
        std::memcpy( out.ptr<float>(), output.data<const float>(), outElems * sizeof( float ) );
        m_forwards.fetch_add( 1, std::memory_order_relaxed );
        return out;
      }
      catch ( const RSOperatorError & )
      {
        throw; // typed errors cross unchanged — the catch-all below must not
               // rebrand InvalidInputData as RuntimeProviderFailed
      }
      catch ( const std::exception &e )
      {
        m_failures.fetch_add( 1, std::memory_order_relaxed );
        {
          std::lock_guard<std::mutex> healthLock( m_healthMutex );
          m_lastError = e.what();
        }
        throw RSOperatorError( ErrorCode::RuntimeProviderFailed,
                               std::string( "OpenVINO inference failed: " ) + e.what() );
      }
    }

    void requestCancel() override { m_cancelRequested.store( true, std::memory_order_relaxed ); }
    void clearCancel() override { m_cancelRequested.store( false, std::memory_order_relaxed ); }

  private:
    // Device selection for compile_model. The registry's resolution verdict
    // is authoritative; the one repair it cannot make is supplied here: a
    // GPU-wanting manifest demoted to cpu only because the host has no CUDA
    // is served by OpenVINO's own GPU plugin when one is exposed (#1576).
    // That demotion — and only that one — is repaired here: hw.cudaAvailable
    // separates it from every other cpu verdict, because an explicit cpu
    // request or a capacity demotion is only ever recorded where CUDA
    // exists. The device itself still comes from OpenVINO's own plugin
    // registry (its "GPU" token is the Intel plugin, never a CUDA index).
    // Returns "" with *error set when the request cannot be honored.
    std::string selectDevice( const ModelInfo &model,
                              const ModelHardwareCapabilities &hw, std::string *error )
    {
      bool gpu = model.runtime.gpu; // the registry's verdict
      if ( !gpu && !hw.cudaAvailable && model.gpu && !manifestPinsCpu( model ) )
        gpu = true; // the repairable CUDA-absence demotion (#1576)
      if ( !gpu )
        return "CPU";
      const std::vector<std::string> devices = mCore->get_available_devices();
      if ( std::find( devices.begin(), devices.end(), "GPU" ) != devices.end() )
        return "GPU";
      if ( model.runtime.gpu )
      {
        // The verdict resolved (and booked) a GPU device; a build without
        // the GPU plugin cannot execute it — refuse instead of silently
        // dropping to CPU underneath a live GPU reservation.
        if ( error )
          *error = "device resolution selected a GPU device but this OpenVINO build "
                   "exposes no GPU plugin (available devices: " + joinDevices( devices ) + ")";
        return {};
      }
      // The repairable demotion has no Intel plugin to land on either:
      // CPU is the only device this build can offer for it.
      return "CPU";
    }

    // True when the manifest's runtime.device token explicitly asks for
    // CPU. An operator-side --device override never reaches the factory
    // (model_execution_service.cpp passes it as explicitDevice), and that
    // token also never lands in model.runtime.device — so the repair above
    // can still serve an explicit cpu override on OpenVINO's GPU plugin
    // (faithful to 9b42bc93f7; the override-vs-repair seam is a known
    // follow-up, recorded by the r23-B R2 review).
    static bool manifestPinsCpu( const ModelInfo &model )
    {
      RequestedDevice request;
      return !model.runtime.device.empty()
             && RequestedDevice::parse( model.runtime.device, &request )
             && request.kind == RequestedDevice::Kind::Cpu;
    }

    static std::string joinDevices( const std::vector<std::string> &devices )
    {
      std::string joined;
      for ( const std::string &device : devices )
        joined += ( joined.empty() ? "" : ", " ) + device;
      return joined.empty() ? std::string( "none" ) : joined;
    }

    std::string mArtifact;
    std::string mDevice = "CPU";
    int mResolvedCudaIndex = 0; ///< registry-resolved CUDA index (R15-C C05/B7)
    std::string mInputName;
    std::string mOutputName;
    ov::Shape mInputShape;
    std::mutex m_inferMutex;
    std::atomic<bool> mLoaded{ false };
    std::atomic<bool> m_cancelRequested{ false };
    std::atomic<std::uint64_t> m_forwards{ 0 };
    std::atomic<std::uint64_t> m_failures{ 0 };
    mutable std::mutex m_healthMutex; ///< locked by the const health() probe
    std::string m_lastError;
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
  if ( !session->load( model, hw, errorMessage ) )
    return nullptr;
  return session;
}

} // namespace

bool openVinoProviderAvailable()
{
  return true;
}

void registerOpenVinoProvider( ModelRuntimeRegistry &registry )
{
  // ProviderTraits{} (maxAddressableCudaIndex 0): OpenVINO addresses NO
  // CUDA device — its "GPU" token is the Intel plugin, reached through
  // OpenVINO's own device registry, not a CUDA index. Declaring a
  // multi-device index here would make the registry promote cudaAvailable
  // for this framework and let a manifest book NVIDIA VRAM (cuda:N) this
  // backend can never touch, while the CUDA-centric resolution still
  // cannot SEE an Intel GPU. The registry's verdict therefore stays
  // authoritative; the provider only supplies the one repair that
  // resolution cannot make (selectDevice above): on a host without CUDA
  // an auto request from a GPU-wanting manifest is demoted to cpu there,
  // and OpenVINO's own device list decides whether the Intel plugin can
  // take it.
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
