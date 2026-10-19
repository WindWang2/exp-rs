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

#include <cmath>
#include <cstring>
#include <algorithm>
#include <memory>
#include <mutex>
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
        // The IModelRuntime artifactPath() contract: the session reports the
        // file it was actually loaded from (cache keys / payloads /
        // provenance), never a hardcoded label.
        m_artifactPath = path;
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
    std::string artifactPath() const override { return m_artifactPath; }

    ProviderRuntimeDetails providerDetails() const override
    {
      ProviderRuntimeDetails details;
      details.executionProvider = "openvino";
      details.runtimeVersion = ov::get_openvino_version().buildNumber;
      return details;
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
        return out;
      }
      catch ( const RSOperatorError & )
      {
        throw; // typed errors cross unchanged — the catch-all below must not
               // rebrand InvalidInputData as RuntimeProviderFailed
      }
      catch ( const std::exception &e )
      {
        throw RSOperatorError( ErrorCode::RuntimeProviderFailed,
                               std::string( "OpenVINO inference failed: " ) + e.what() );
      }
    }

  private:
    // Device selection. The registry's resolution verdict (runtime.gpu as
    // rewritten by acquire, model_runtime.cpp:1098-1100) is authoritative
    // for the GPU direction: it already priced in an explicit cpu request
    // (operator --device or the manifest's runtime.device token), a
    // recorded capacity demotion and the host's CUDA reality — and the
    // cache key plus the VRAM ledger booking were built from it. Re-deriving
    // the device from the manifest's intent fields would silently run a
    // different device than the one booked: an explicit cpu honored as
    // "GPU", or a capacity demotion undone while its ledger ticket stands.
    //
    // The ONE thing that verdict cannot see is a non-CUDA GPU: the
    // registry's resolution is CUDA-centric (cudaAvailable is promoted
    // only for CUDA-addressed backends), so on a host without CUDA an
    // auto request from a GPU-wanting manifest demotes to cpu and the
    // manifest's GPU demand never reaches the provider (#1576). That
    // demotion — and only that one — is repaired here: hw.cudaAvailable
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
    // (model_execution_service.cpp passes it as explicitDevice), so it is
    // honored through the verdict, never re-derived here.
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

    // The artifact this session was loaded from (IModelRuntime contract:
    // cache keys / payloads / provenance) — the resolved path load() read,
    // never a hardcoded label.
    std::string m_artifactPath;
    std::string mDevice = "CPU";
    std::string mInputName;
    std::string mOutputName;
    ov::Shape mInputShape;
    std::mutex m_inferMutex;
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
