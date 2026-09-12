// src/operators/runtime/model_runtime.cpp
#include "operators/runtime/model_runtime.h"

#include "operators/framework/artifact_digest.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/http_provider.h"
#include "operators/runtime/nvml_inventory.h"
#include "operators/runtime/opencv_dnn_runtime.h"
#include "operators/runtime/onnxruntime_provider.h"
#include "operators/runtime/python_worker_provider.h"

#include <QDateTime>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <opencv2/dnn.hpp>

#include <algorithm>
#include <limits>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace sicnu::operators::runtime {

namespace {

/// Digest fallback for ad-hoc (non-catalog) models whose ModelInfo carries no
/// precomputed contentDigest — the direct file path passed to rs:infer. Memo
/// is keyed on (path, size, mtime) and bounded: re-hashing is only a cost.
struct DigestMemoEntry
{
  std::string path;
  unsigned long long sizeBytes = 0;
  qint64 mtimeMs = 0;
  std::string digest;
};

std::mutex &digestMemoMutex()
{
  static std::mutex mutex;
  return mutex;
}

std::vector<DigestMemoEntry> &digestMemo()
{
  static std::vector<DigestMemoEntry> memo;
  return memo;
}

/// Content digest for session identity. Catalog models always carry one;
/// ad-hoc file references get it computed here (memoized per file state).
/// Returns "" when the artifact is not locally readable.
/// Content digest for session identity. Catalog models always carry one;
/// ad-hoc file references get it computed here (memoized per file state).
/// Returns "" when the artifact is not locally readable. Platform 9.0 (M7):
/// a model with a digest-bound package (aux files) keys its session on the
/// PACKAGE digest — changed ontology/config bytes never reuse a session
/// loaded for the previous package, exactly like changed weights.
std::string contentDigestFor( const ModelInfo &model )
{
  if ( !model.contentDigest.empty() )
  {
    if ( model.packageDigest.empty() )
      return model.contentDigest;
    return model.contentDigest + "|pkg:" + model.packageDigest;
  }
  if ( model.resolvedArtifactPath.empty() )
    return std::string();

  const QString path = QString::fromStdString( model.resolvedArtifactPath );
  const QFileInfo info( path );
  if ( !info.exists() || !info.isFile() )
    return std::string();
  const unsigned long long sizeBytes = static_cast<unsigned long long>( info.size() );
  const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();

  {
    // Memo hit path is lock-scoped; the hash itself (potentially GBs) runs
    // OUTSIDE the mutex so concurrent acquires of other models never wait
    // behind a first hash.
    std::lock_guard<std::mutex> lock( digestMemoMutex() );
    auto &memo = digestMemo();
    for ( const auto &entry : memo )
    {
      if ( entry.path == model.resolvedArtifactPath && entry.sizeBytes == sizeBytes
           && entry.mtimeMs == mtimeMs )
        return entry.digest;
    }
  }
  const std::string digest = sicnu::operators::artifactSha256Hex( model.resolvedArtifactPath );
  if ( digest.empty() )
    return std::string(); // unreadable artifact — caller falls back
  {
    std::lock_guard<std::mutex> lock( digestMemoMutex() );
    auto &memo = digestMemo();
    // Re-check: another thread may have hashed the same state meanwhile.
    for ( const auto &entry : memo )
    {
      if ( entry.path == model.resolvedArtifactPath && entry.sizeBytes == sizeBytes
           && entry.mtimeMs == mtimeMs )
        return entry.digest;
    }
    if ( memo.size() > 64 )
      memo.clear();
    memo.push_back( DigestMemoEntry{ model.resolvedArtifactPath, sizeBytes, mtimeMs, digest } );
  }
  return digest;
}

/// Best-effort identity when the artifact bytes are not locally readable
/// (fake providers, virtual/plugin references). Distinguishes different
/// file states but not a same-state byte swap — no real provider can load
/// unreadable bytes, so no stale-content session can be served.
std::string identityFallbackFor( const ModelInfo &model )
{
  if ( model.resolvedArtifactPath.empty() )
    return "no-artifact";
  const QFileInfo info( QString::fromStdString( model.resolvedArtifactPath ) );
  return model.resolvedArtifactPath + "@" + std::to_string( static_cast<unsigned long long>( info.size() ) )
           + "@" + std::to_string( info.lastModified().toMSecsSinceEpoch() );
}

} // namespace


// --- Platform 7.0: default named-tensor bridge ------------------------------
// Bridges through the historical cv::Mat surface with an exact-dtype policy:
// the bridge carries exactly what infer/inferMulti always carried (rank-4
// float32 semantics through the engines), refuses everything else loudly,
// and providers override inferNamed for true N-D support.
std::vector<NamedTensor> IModelRuntime::inferNamed( const std::vector<NamedTensor> &inputs,
                                                    const std::vector<std::string> &outputNames )
{
  ( void )outputNames; // the historical transport cannot select specific heads
  if ( inputs.empty() )
    throw std::runtime_error( "inferNamed needs at least one input tensor" );

  auto toRankedBlob = []( const NamedTensor &nt ) {
    cv::Mat mat;
    try
    {
      mat = nt.second.toMat();
    }
    catch ( const std::exception &e )
    {
      throw std::runtime_error( std::string( "input '" ) + ( nt.first.empty() ? "<default>" : nt.first )
                                + "' cannot cross the named-tensor bridge: " + e.what() );
    }
    if ( mat.dims != 4 )
      throw std::runtime_error( "input '" + ( nt.first.empty() ? std::string( "<default>" ) : nt.first )
                                + "' has rank " + std::to_string( mat.dims )
                                + "; the default named-tensor bridge carries exactly rank-4 tensors "
                                  "(this provider does not declare N-D support)" );
    return mat;
  };

  if ( inputs.size() == 1 )
  {
    const cv::Mat out = infer( toRankedBlob( inputs.front() ) );
    return { NamedTensor{ std::string(), TensorBlob::fromMat( out ) } };
  }

  std::vector<NamedBlob> blobs;
  blobs.reserve( inputs.size() );
  for ( const NamedTensor &nt : inputs )
    blobs.push_back( NamedBlob{ nt.first, toRankedBlob( nt ) } );
  std::vector<cv::Mat> outs = inferMulti( blobs );
  std::vector<NamedTensor> named;
  named.reserve( outs.size() );
  for ( const cv::Mat &m : outs )
    named.push_back( NamedTensor{ std::string(), TensorBlob::fromMat( m ) } );
  return named;
}


ModelHardwareCapabilities ModelHardwareCapabilities::detect()
{
  ModelHardwareCapabilities caps;

  // Honest backend probe: OpenCV enumerates the (backend, target) pairs its
  // build actually supports. CUDA appears only in CUDA-enabled builds.
  try
  {
    const auto backends = cv::dnn::getAvailableBackends();
    for ( const auto &[backend, target] : backends )
    {
      if ( backend == cv::dnn::DNN_BACKEND_CUDA && target == cv::dnn::DNN_TARGET_CUDA )
        caps.cudaAvailable = true;
      if ( target == cv::dnn::DNN_TARGET_OPENCL )
        caps.openclAvailable = true;
    }
  }
  catch ( const cv::Exception & )
  {
    // Enumeration is best-effort; absence of a backend is not an error.
  }
  // cv::dnn exposes no device enumeration: an OpenCV CUDA build addresses
  // exactly one device through this backend. Direct-CUDA runtimes get their
  // REAL device truth from the NVML probe below.
  caps.cudaDeviceCount = caps.cudaAvailable ? 1 : 0;

  // Platform 9.0 real driver probe: NVML reports actual devices, names,
  // total AND free VRAM (fresh per call — the acquire path intentionally
  // re-detects so admission sees live pressure). Failure degrades honestly.
  const NvidiaInventory nvidia = NvidiaInventory::probe();
  if ( nvidia.available && !nvidia.devices.empty() )
  {
    caps.cudaRuntimeAvailable = true;
    caps.cudaDeviceCount = std::max( caps.cudaDeviceCount,
                                     static_cast<int>( nvidia.devices.size() ) );
    caps.deviceNames.resize( nvidia.devices.size() );
    caps.deviceTotalVramMb.resize( nvidia.devices.size(), 0 );
    caps.deviceFreeVramMb.resize( nvidia.devices.size(), -1 );
    for ( const NvidiaDeviceInfo &device : nvidia.devices )
    {
      if ( device.index < 0
           || device.index >= static_cast<int>( nvidia.devices.size() ) )
        continue;
      caps.deviceNames[static_cast<std::size_t>( device.index )] = device.name;
      caps.deviceTotalVramMb[static_cast<std::size_t>( device.index )] = device.totalVramMb;
      caps.deviceFreeVramMb[static_cast<std::size_t>( device.index )] = device.freeVramMb;
    }
  }

  // Explicit overrides (tests and constrained deployments). SICNU_MODEL_GPU
  // forces BOTH cuda gates — it simulates (or hides) a GPU host as a whole.
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QString gpu = env.value( QStringLiteral( "SICNU_MODEL_GPU" ) );
  if ( gpu == QStringLiteral( "1" ) || gpu.compare( QStringLiteral( "true" ), Qt::CaseInsensitive ) == 0 )
  {
    caps.cudaAvailable = true;
    caps.cudaRuntimeAvailable = true;
  }
  else if ( gpu == QStringLiteral( "0" ) || gpu.compare( QStringLiteral( "false" ), Qt::CaseInsensitive ) == 0 )
  {
    caps.cudaAvailable = false;
    caps.cudaRuntimeAvailable = false;
  }
  bool vramOk = false;
  const int vramMb = env.value( QStringLiteral( "SICNU_MODEL_VRAM_MB" ) ).toInt( &vramOk );
  if ( vramOk && vramMb > 0 )
    caps.vramBudgetMb = vramMb;
  bool devicesOk = false;
  const int devices = env.value( QStringLiteral( "SICNU_MODEL_CUDA_DEVICES" ) ).toInt( &devicesOk );
  if ( devicesOk && devices >= 0 )
  {
    // Keep the per-device inventory entries that survive the resize — a
    // shrunken list drops tail devices, a grown list extends with unknowns.
    const std::size_t newSize = static_cast<std::size_t>( devices );
    caps.deviceNames.resize( newSize );
    caps.deviceTotalVramMb.resize( newSize, 0 );
    caps.deviceFreeVramMb.resize( newSize, -1 );
    caps.cudaDeviceCount = devices;
  }
  const QString freeCsv = env.value( QStringLiteral( "SICNU_MODEL_VRAM_FREE_MB" ) );
  if ( !freeCsv.isEmpty() )
  {
    const QStringList parts = freeCsv.split( ',' );
    caps.deviceFreeVramMb.resize( parts.size(), -1 );
    caps.deviceNames.resize( parts.size() );
    caps.deviceTotalVramMb.resize( parts.size(), 0 );
    for ( int i = 0; i < parts.size() && i < caps.deviceFreeVramMb.size(); ++i )
    {
      bool ok = false;
      const int freeMb = parts[i].trimmed().toInt( &ok );
      caps.deviceFreeVramMb[static_cast<std::size_t>( i )] = ok && freeMb >= 0 ? freeMb : -1;
    }
    caps.cudaDeviceCount =
      std::max( caps.cudaDeviceCount, static_cast<int>( parts.size() ) );
  }

  // Consistency: an OpenCV-CUDA claim implies at least one device; a real
  // driver implies at least one device; with NEITHER cuda path there is
  // nothing addressable — even if a stale override said otherwise.
  if ( caps.cudaAvailable && caps.cudaDeviceCount < 1 )
    caps.cudaDeviceCount = 1;
  if ( caps.cudaRuntimeAvailable && caps.cudaDeviceCount < 1 )
    caps.cudaDeviceCount = 1;
  if ( !caps.cudaAvailable && !caps.cudaRuntimeAvailable )
    caps.cudaDeviceCount = 0;

  return caps;
}

bool RequestedDevice::parse( const std::string &token, RequestedDevice *out )
{
  std::string t;
  t.reserve( token.size() );
  for ( char c : token )
    t.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) );
  if ( out )
    *out = RequestedDevice{};
  if ( t.empty() || t == "auto" )
    return true; // default-constructed = Auto
  if ( t == "cpu" )
  {
    if ( out )
      *out = RequestedDevice::cpu();
    return true;
  }
  if ( t == "cuda" )
  {
    if ( out )
      *out = RequestedDevice::cuda( 0 );
    return true;
  }
  if ( t.rfind( "cuda:", 0 ) == 0 )
  {
    const std::string indexPart = t.substr( 5 );
    if ( indexPart.empty() )
      return false;
    for ( char c : indexPart )
    {
      if ( !std::isdigit( static_cast<unsigned char>( c ) ) )
        return false;
    }
    const long index = std::strtol( indexPart.c_str(), nullptr, 10 );
    if ( index < 0 || index > 63 )
      return false;
    if ( out )
      *out = RequestedDevice::cuda( static_cast<int>( index ) );
    return true;
  }
  return false;
}

std::string RequestedDevice::toString() const
{
  switch ( kind )
  {
    case Kind::Cpu:
      return "cpu";
    case Kind::Cuda:
      return "cuda:" + std::to_string( cudaIndex );
    case Kind::Auto:
      break;
  }
  return "auto";
}

bool resolveDevice( const RequestedDevice &request,
                    const ModelHardwareCapabilities &hw,
                    bool modelWantsGpu, int estimatedVramMb,
                    int maxAddressableCudaIndex, bool allowCpuFallback,
                    ResolvedDevice *out, std::string *why )
{
  return resolveDevice( request, hw, modelWantsGpu, estimatedVramMb, maxAddressableCudaIndex,
                        allowCpuFallback, {}, out, why );
}

bool resolveDevice( const RequestedDevice &request,
                    const ModelHardwareCapabilities &hw,
                    bool modelWantsGpu, int estimatedVramMb,
                    int maxAddressableCudaIndex, bool allowCpuFallback,
                    const std::vector<int> &freeVramMbByIndex,
                    ResolvedDevice *out, std::string *why )
{
  return resolveDevice( request, hw, modelWantsGpu, estimatedVramMb, maxAddressableCudaIndex,
                        allowCpuFallback, freeVramMbByIndex, DevicePlacementPolicy::LowestFitting,
                        out, why );
}

bool resolveDevice( const RequestedDevice &request,
                    const ModelHardwareCapabilities &hw,
                    bool modelWantsGpu, int estimatedVramMb,
                    int maxAddressableCudaIndex, bool allowCpuFallback,
                    const std::vector<int> &freeVramMbByIndex,
                    DevicePlacementPolicy policy,
                    ResolvedDevice *out, std::string *why )
{
  const auto fitsBudget = [ & ]() {
    return hw.vramBudgetMb <= 0 || estimatedVramMb <= hw.vramBudgetMb;
  };
  // Ledger fit: negative free = unenforced (no constraint); a positive
  // capacity requires the estimate to fit the FREE value.
  const auto freeOnDevice = [ & ]( int index ) -> int {
    return index >= 0 && index < static_cast<int>( freeVramMbByIndex.size() )
             ? freeVramMbByIndex[static_cast<std::size_t>( index )]
             : -1;
  };
  const auto fitsLedger = [ & ]( int index ) {
    const int freeMb = freeOnDevice( index );
    return freeMb < 0 || estimatedVramMb <= freeMb;
  };
  const auto cudaAvailable = [&]( int index ) {
    return hw.cudaAvailable && index >= 0 && index < hw.cudaDeviceCount
           && index <= maxAddressableCudaIndex;
  };
  const auto answer = [ &out ]( ResolvedDevice device ) {
    if ( out )
      *out = device;
    return true;
  };
  const auto refuse = [ &why ]( const std::string &reason ) {
    if ( why )
      *why = reason;
    return false;
  };

  ResolvedDevice cpu;
  cpu.gpu = false;

  switch ( request.kind )
  {
    case RequestedDevice::Kind::Cpu:
      return answer( cpu );
    case RequestedDevice::Kind::Cuda:
    {
      // A model that prefers CPU never lands on a GPU: the manifest's GPU
      // flag expresses where the weights were designed to run, and an
      // explicit cuda request for a CPU-only model is a configuration error
      // that must fail loudly (silently running on CPU would mislead).
      if ( !modelWantsGpu )
        return refuse( "model is not GPU-capable (manifest runtime.gpu is false)" );
      if ( !hw.cudaAvailable )
        return refuse( "CUDA is unavailable on this host" );
      if ( !cudaAvailable( request.cudaIndex ) )
        return refuse( "cuda:" + std::to_string( request.cudaIndex )
                       + " is not addressable by this backend/host (addressable indices: 0.."
                         + std::to_string( std::min( hw.cudaDeviceCount - 1, maxAddressableCudaIndex ) )
                         + ")" );
      if ( fitsBudget() && fitsLedger( request.cudaIndex ) )
      {
        ResolvedDevice device;
        device.gpu = true;
        device.cudaIndex = request.cudaIndex;
        return answer( device );
      }
      if ( allowCpuFallback )
        return answer( cpu ); // documented demotion: over-budget, fallback allowed
      return refuse( "model needs " + std::to_string( estimatedVramMb )
                     + " MiB VRAM but the budget is " + std::to_string( hw.vramBudgetMb )
                     + " MiB and cpu_fallback is disabled" );
    }
    case RequestedDevice::Kind::Auto:
    {
      // Deterministic multi-device selection (Platform 7.0): auto is NOT a
      // trivial cuda:0 — it picks the LOWEST addressable index that fits both
      // the global budget and the planner ledger's free VRAM, else cpu (or a
      // typed refusal when the model forbids fallback). Equal inputs always
      // yield equal outputs. Platform 8.0 WP-B: under LeastLoaded the choice
      // among FITTING devices falls to the largest free VRAM (ties → lowest
      // index), spreading concurrent sessions across cards.
      if ( modelWantsGpu && hw.cudaAvailable )
      {
        const int maxIndex = std::min( hw.cudaDeviceCount - 1, maxAddressableCudaIndex );
        int chosen = -1;
        // INT_MIN start: the first fitting index always seeds the choice, so
        // unknown-free devices (−1) keep a deterministic lowest-index order
        // and known-free devices win by their actual free VRAM.
        int chosenFree = std::numeric_limits<int>::min();
        for ( int index = 0; index <= maxIndex; ++index )
        {
          if ( !( fitsBudget() && fitsLedger( index ) ) )
            continue;
          if ( policy == DevicePlacementPolicy::LowestFitting )
          {
            chosen = index;
            break;
          }
          const int freeMb = freeOnDevice( index );
          if ( freeMb > chosenFree )
          {
            chosenFree = freeMb;
            chosen = index;
          }
        }
        if ( chosen >= 0 )
        {
          ResolvedDevice device;
          device.gpu = true;
          device.cudaIndex = chosen;
          return answer( device );
        }
      }
      if ( modelWantsGpu && !allowCpuFallback )
        return refuse( "no fitting CUDA device and cpu_fallback is disabled" );
      return answer( cpu );
    }
  }
  return answer( cpu );
}

InferenceFailureKind classifyInferenceError( const std::string &message )
{
  std::string lower;
  lower.reserve( message.size() );
  for ( char c : message )
    lower.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) );

  auto contains = [ &lower ]( const char *needle ) {
    return lower.find( needle ) != std::string::npos;
  };

  if ( contains( "cancel" ) )
    return InferenceFailureKind::Canceled;
  if ( contains( "out of memory" ) || contains( "bad_alloc" )
       || contains( "cuda_error_out_of_memory" ) || contains( "cudamalloc" )
       || contains( "alloc failed" ) || contains( "allocation failure" ) )
    return InferenceFailureKind::OutOfMemory;
  // Platform 9.0 taxonomy: a provider that hit its time budget is a Timeout
  // (LIVE but unresponsive), classified before the crash patterns — "timed
  // out or exited" messages describe the timeout, the exit note is a side
  // observation. Deterministic: order matters, never both.
  if ( contains( "timed out" ) || contains( "timed-out" ) || contains( "time budget exceeded" ) )
    return InferenceFailureKind::Timeout;
  // Platform 7.0 taxonomy: external-provider death beats shape/corrupt checks
  // — a worker that died mid-run must not read as a model or tensor problem.
  if ( contains( "worker exited" ) || contains( "worker crashed" ) || contains( "provider crashed" )
       || contains( "terminated unexpectedly" ) || contains( "connection refused" )
       || contains( "connection reset" ) || contains( "broken pipe" )
       || contains( "no response from provider" )
       || contains( "provider error" ) )
    return InferenceFailureKind::ProviderCrash;
  if ( contains( "not addressable" ) || contains( "device unavailable" )
       || contains( "cuda is unavailable" ) || contains( "cannot honor device" )
       || contains( "device request" )
       // Platform 9.0 CUDA-stack realities (real-GPU-lane evidence):
       // the provider library could not register, or the GPU stack is
       // incomplete at run time (cuDNN absent), or the driver/hardware
       // raised a non-OOM CUDA error. All are device-availability
       // failures — never misclassified as model or tensor problems.
       || contains( "cudaexecutionprovider" ) || contains( "cudnn is unavailable" )
       || contains( "cuda_error" ) || contains( "cuda failure" )
       || contains( "no cuda-capable device" ) )
    return InferenceFailureKind::DeviceUnavailable;
  if ( contains( "schema" ) || contains( "contract" ) || contains( "manifest" )
       || contains( "not part of" ) || contains( "does not declare" )
       || contains( "no runtime provider available" ) || contains( "wire protocol" ) || contains( "matches no graph" ) )
    return InferenceFailureKind::IncompatibleSchema;
  if ( contains( "not loaded" ) )
    return InferenceFailureKind::NotLoaded;
  if ( contains( "shape" ) || contains( "dimension" ) || contains( "size mismatch" )
       || contains( "wrong input" ) || contains( "channels" ) )
    return InferenceFailureKind::ShapeMismatch;
  // Platform 7.0: a forward that RAN but produced an unusable output is an
  // output-validity failure, not an unknown crash.
  if ( contains( "output invalid" ) || contains( "invalid output" ) || contains( "empty output" )
       || contains( "no usable output" ) || contains( "output head" ) )
    return InferenceFailureKind::OutputInvalid;
  if ( contains( "failed to load" ) || contains( "parse" ) || contains( "protobuf" )
       || contains( "proto:" ) || contains( "corrupt" ) )
    return InferenceFailureKind::CorruptModel;
  return InferenceFailureKind::Unknown;
}

ErrorCode errorCodeForInferenceFailure( InferenceFailureKind kind )
{
  switch ( kind )
  {
    case InferenceFailureKind::OutOfMemory:
    case InferenceFailureKind::CorruptModel:
    case InferenceFailureKind::OutputInvalid:
      return ErrorCode::ComputationError;
    case InferenceFailureKind::Canceled:
      return ErrorCode::Cancelled;
    case InferenceFailureKind::ShapeMismatch:
      return ErrorCode::InvalidInputData;
    case InferenceFailureKind::NotLoaded:
      return ErrorCode::NotInitialized;
    case InferenceFailureKind::IncompatibleSchema:
      return ErrorCode::InvalidInputData;
    case InferenceFailureKind::DeviceUnavailable:
      return ErrorCode::DeviceUnavailable;
    case InferenceFailureKind::ProviderCrash:
      return ErrorCode::RuntimeProviderFailed;
    case InferenceFailureKind::Timeout:
      return ErrorCode::ExternalProcessTimeout;
    case InferenceFailureKind::Unknown:
      break;
  }
  return ErrorCode::Unknown;
}

ModelReadiness evaluateRuntimeReadiness( const ModelInfo &model,
                                         const ModelHardwareCapabilities &hw,
                                         std::string *reason )
{
  auto fail = [reason]( ModelReadiness state, const std::string &why ) {
    if ( reason )
      *reason = why;
    return state;
  };

  if ( !ModelRuntimeRegistry::instance().hasProvider( model.framework ) )
    return fail( ModelReadiness::UnsupportedRuntime,
                 "no runtime provider available for framework '" + model.framework + "' in this build" );

  // Platform 4.0: an explicit runtime.device token is evaluated through the
  // same deterministic resolution acquire uses, so readiness and execution
  // can never disagree about device feasibility. Legacy manifests (no token)
  // keep the historical gpu && !cpu_fallback check.
  //
  // Platform 9.0: DIRECT-CUDA runtimes (onnxruntime — the only provider with
  // a multi-device maxAddressableCudaIndex) resolve against the REAL driver
  // probe (hw.cudaRuntimeAvailable), not the OpenCV-CUDA backend claim; the
  // same effective view acquire uses. Readiness and execution stay one truth.
  ModelHardwareCapabilities effectiveHw = hw;
  if ( const auto traits = ModelRuntimeRegistry::instance().providerTraits( model.framework );
       traits && traits->maxAddressableCudaIndex > 0 && hw.cudaRuntimeAvailable )
    effectiveHw.cudaAvailable = true;

  if ( model.runtime.device.empty() )
  {
    if ( model.runtime.gpu && !model.runtime.cpuFallback )
    {
      if ( !effectiveHw.cudaAvailable )
        return fail( ModelReadiness::IncompatibleHardware,
                     "model requires GPU execution but CUDA is unavailable and CPU fallback is disabled" );
      if ( effectiveHw.vramBudgetMb > 0 && model.runtime.estimatedVramMb > effectiveHw.vramBudgetMb )
        return fail( ModelReadiness::IncompatibleHardware,
                     "model requires " + std::to_string( model.runtime.estimatedVramMb )
                       + " MiB VRAM but the host budget is " + std::to_string( effectiveHw.vramBudgetMb ) + " MiB" );
    }
    return ModelReadiness::Ready;
  }

  RequestedDevice request;
  if ( !RequestedDevice::parse( model.runtime.device, &request ) )
    return fail( ModelReadiness::InvalidManifest,
                 "runtime.device '" + model.runtime.device
                   + "' is not parsable (supported: cpu, cuda, cuda:N, auto)" );
  int maxCudaIndex = 0;
  if ( const auto traits = ModelRuntimeRegistry::instance().providerTraits( model.framework ) )
    maxCudaIndex = traits->maxAddressableCudaIndex;
  ResolvedDevice resolved;
  std::string why;
  if ( !resolveDevice( request, effectiveHw, model.runtime.gpu, model.runtime.estimatedVramMb,
                       maxCudaIndex, model.runtime.cpuFallback, &resolved, &why ) )
    return fail( ModelReadiness::IncompatibleHardware,
                 "device '" + model.runtime.device + "' cannot execute this model: " + why );
  return ModelReadiness::Ready;
}

ModelRuntimeRegistry &ModelRuntimeRegistry::instance()
{
  static ModelRuntimeRegistry registry;
  return registry;
}

namespace {
/// Platform 8.0 WP-B: the placement policy is a deployment knob, not just a
/// test seam — SICNU_MODEL_PLACEMENT=least_loaded spreads concurrent
/// sessions across cards at construction time (documented in
/// docs/inference/platform-8.md). Unknown tokens keep the 7.0 default.
DevicePlacementPolicy placementPolicyFromEnv()
{
  const QByteArray token = qgetenv( "SICNU_MODEL_PLACEMENT" );
  if ( token.compare( "least_loaded", Qt::CaseInsensitive ) == 0 )
    return DevicePlacementPolicy::LeastLoaded;
  return DevicePlacementPolicy::LowestFitting;
}
} // namespace

ModelRuntimeRegistry::ModelRuntimeRegistry()
    : m_placementPolicy( placementPolicyFromEnv() )
{
  registerProvider( "onnx",
                    []( const ModelInfo &model, const ModelHardwareCapabilities &hw,
                        std::string *errorMessage ) -> ModelRuntimePtr {
                      if ( model.resolvedArtifactPath.empty() )
                      {
                        if ( errorMessage )
                          *errorMessage = "model has no resolved artifact path";
                        return nullptr;
                      }
                      auto session = std::make_shared<OpenCvDnnRuntime>(
                        model.resolvedArtifactPath, model.runtime.gpu, hw );
                      if ( !session->load( errorMessage ) )
                        return nullptr;
                      return session;
                    } );

  // Platform 3.0: the optional ONNX Runtime provider registers itself when
  // compiled in (SICNU_WITH_ONNX_RUNTIME); otherwise this is a no-op stub and
  // models declaring framework "onnxruntime" surface runtime_unavailable.
  registerOnnxRuntimeProvider( *this );
  // Platform 7.0: external provider contracts — same registry seam. The HTTP
  // provider registers when Qt6::Network is compiled in (otherwise a stub);
  // the Python worker provider needs Qt Core alone and registers always.
  // All of them take *this because calling instance() from inside the ctor
  // would re-enter the static initializer.
  registerHttpProvider( *this );
  registerPythonWorkerProvider( *this );
}

ModelRuntimePtr ModelRuntimeRegistry::acquire( const ModelInfo &model, std::string *errorMessage )
{
  // Manifest contract → device request. Legacy manifests (no runtime.device)
  // keep the historical Auto behavior: cuda when the model wants GPU and the
  // host offers it, cpu otherwise.
  RequestedDevice request;
  if ( !model.runtime.device.empty() )
  {
    if ( !RequestedDevice::parse( model.runtime.device, &request ) )
    {
      if ( errorMessage )
        *errorMessage = "model '" + model.name + "' declares an unparsable runtime.device '"
                          + model.runtime.device + "' (supported: cpu, cuda, cuda:N, auto)";
      return nullptr;
    }
  }
  return acquire( model, request, errorMessage );
}

ModelRuntimePtr ModelRuntimeRegistry::acquire( const ModelInfo &model, const RequestedDevice &request,
                                               std::string *errorMessage )
{
  const ModelHardwareCapabilities hw = hardware();
  const std::string framework = model.framework;

  // Platform 4.0 session identity: the cache key anchors on the artifact's
  // CONTENT digest (SHA-256 of the bytes) whenever the bytes are readable, so
  // the same path with different bytes can never be served a stale session —
  // the digest differs and the old entry only leaves via LRU eviction. Equal
  // bytes at different paths share one session (weights dominate the memory
  // cost). When the artifact is not locally readable (fake providers in
  // tests, plugin runtimes with virtual references), identity falls back to
  // the path/size/mtime triple — every real provider would fail the load
  // anyway, so no stale-content risk is introduced.
  const std::string digest = contentDigestFor( model );
  const std::string identityComponent =
    digest.empty() ? identityFallbackFor( model ) : digest;

  std::unique_lock<std::mutex> lock( m_mutex );
  // Idle eviction runs once per acquire attempt, BEFORE the cache lookup —
  // a stale entry must be dropped, not served (contract: "dropped on the
  // next acquire"). Cheap when the idle window is disabled.
  evictExpiredLocked( QDateTime::currentMSecsSinceEpoch() );
  // Copy the provider entry out: the factory runs OUTSIDE the lock, where a
  // concurrent registerProvider could otherwise invalidate the iterator.
  ModelRuntimeFactory factory;
  ProviderTraits traits;
  {
    const auto provider = m_providers.find( framework );
    if ( provider == m_providers.end() )
    {
      lock.unlock();
      if ( errorMessage )
        *errorMessage = "no runtime provider available for framework '" + framework + "' in this build";
      return nullptr;
    }
    factory = provider->second.factory;
    traits = provider->second.traits;
  }

  // Platform 9.0: DIRECT-CUDA runtimes (traits.maxAddressableCudaIndex > 0 —
  // the onnxruntime provider) resolve against the REAL driver probe
  // (cudaRuntimeAvailable); the opencv_dnn backend keeps the historical
  // cv::dnn-backend gate. One effective view feeds resolution, the pressure
  // valve AND the factory so readiness, acquire and the session agree.
  ModelHardwareCapabilities effectiveHw = hw;
  if ( traits.maxAddressableCudaIndex > 0 && hw.cudaRuntimeAvailable )
    effectiveHw.cudaAvailable = true;

  // Platform 4.0 device resolution (deterministic pure function), Platform
  // 7.0 ledger-aware: the planner's per-device free VRAM feeds the choice so
  // auto picks the lowest FITTING index and explicit cuda:N must fit its card.
  ResolvedDevice device;
  std::string deviceWhy;
  const int maxIndex = std::min( hw.cudaDeviceCount - 1, traits.maxAddressableCudaIndex );
  const auto buildFreeList = [ & ]() {
    const DeviceInventory inventory = DeviceInventory::fromHardware( effectiveHw );
    std::vector<int> freeMbByIndex;
    if ( maxIndex >= 0 )
    {
      freeMbByIndex.resize( static_cast<std::size_t>( maxIndex ) + 1, -1 );
      for ( int i = 0; i <= maxIndex; ++i )
      {
        const DeviceInfo *info = inventory.device( i );
        // Seed the ledger capacity from the inventory (idempotent): without
        // this the production path never registers a capacity and freeMb
        // would read 0, silently demoting every GPU request to cpu.
        if ( info && info->vramCapacityMb > 0 )
          m_ledger.setCapacity( i, info->vramCapacityMb );
        // Platform 9.0: the free verdict is the HONEST minimum of what the
        // ledger still reserves for sessions and what the driver reports as
        // physically free (other processes on the card count). Either source
        // alone is used as-is; both unknown stays -1 (unenforced, -1 ignored).
        const int ledgerFree =
          info && info->vramCapacityMb > 0 ? m_ledger.freeMb( i ) : -1;
        const int realFree = info ? info->freeVramMb : -1;
        int freeMb = -1;
        if ( ledgerFree >= 0 && realFree >= 0 )
          freeMb = std::min( ledgerFree, realFree );
        else if ( ledgerFree >= 0 )
          freeMb = ledgerFree;
        else
          freeMb = realFree;
        freeMbByIndex[static_cast<std::size_t>( i )] = freeMb;
      }
    }
    return freeMbByIndex;
  };
  if ( !resolveDevice( request, effectiveHw, model.runtime.gpu, model.runtime.estimatedVramMb,
                       traits.maxAddressableCudaIndex, model.runtime.cpuFallback,
                       buildFreeList(), m_placementPolicy, &device, &deviceWhy ) )
  {
    // Platform 7.0 pressure valve on the RESOLUTION path: a GPU-fit refusal
    // may be recoverable by evicting cached sessions — ONE bounded pass over
    // the addressable devices, then a single resolution retry. A request the
    // hardware cannot honor (bad index, no CUDA, cpu request) stays refused:
    // eviction cannot create hardware. Models allowing CPU fallback never
    // reach this refusal through auto (they demote), so eviction only serves
    // runs that demand GPU.
    bool recovered = false;
    if ( model.runtime.gpu && effectiveHw.cudaAvailable && maxIndex >= 0
         && request.kind != RequestedDevice::Kind::Cpu )
    {
      // Explicit cuda:N evicts ONLY that device; auto sweeps every
      // addressable device. Either way: one bounded pass, one retry.
      if ( request.kind == RequestedDevice::Kind::Cuda )
        evictDeviceLocked( request.cudaIndex );
      else
      {
        for ( int i = 0; i <= maxIndex; ++i )
          evictDeviceLocked( i );
      }
      recovered = resolveDevice( request, effectiveHw, model.runtime.gpu, model.runtime.estimatedVramMb,
                                 traits.maxAddressableCudaIndex, model.runtime.cpuFallback,
                                 buildFreeList(), m_placementPolicy, &device, &deviceWhy );
    }
    if ( !recovered )
    {
      // A GPU-demanding request that failed even after the eviction pass is
      // a device-capacity refusal — tagged so payloads and the failure
      // taxonomy classify it as DeviceUnavailable.
      if ( model.runtime.gpu && request.kind != RequestedDevice::Kind::Cpu
           && deviceWhy.find( "cpu_fallback" ) != std::string::npos )
        deviceWhy = "device unavailable — " + deviceWhy;
      lock.unlock();
      if ( errorMessage )
        *errorMessage = "cannot honor device request '" + request.toString() + "' for model '"
                          + model.name + "': " + ( deviceWhy.empty() ? std::string( "unresolvable" )
                                                                     : deviceWhy );
      return nullptr;
    }
  }
  const std::string key = framework + "|" + device.toString() + "|" + identityComponent;

  // Platform 7.0 admission: the acquisition reserves its manifest estimate on
  // the resolved GPU device for the lifetime of the cache entry (bounded,
  // deterministic, never a scheduler). Reservations on CPU are a no-op.
  const int estimateMb = model.runtime.estimatedVramMb;
  if ( device.gpu && estimateMb > 0 )
  {
    std::string admitWhy;
    if ( !m_ledger.tryReserve( device.cudaIndex, estimateMb, identityComponent, &admitWhy ) )
    {
      // Memory-pressure valve: ONE bounded eviction pass over the sessions
      // pinned to this device, then a single retry. No loops, no queueing.
      evictDeviceLocked( device.cudaIndex );
      if ( !m_ledger.tryReserve( device.cudaIndex, estimateMb, identityComponent, &admitWhy ) )
      {
        lock.unlock();
        if ( errorMessage )
          *errorMessage = "cannot honor device request '" + request.toString() + "' for model '"
                            + model.name + "': device unavailable — " + admitWhy;
        return nullptr;
      }
    }
  }

  const auto cached = m_cache.find( key );
  if ( cached != m_cache.end() )
  {
    cached->second.lastUsed = ++m_useCounter;
    cached->second.lastUsedMs = QDateTime::currentMSecsSinceEpoch();
    ++m_cacheHits;
    return cached->second.session;
  }
  ++m_cacheMisses;
  lock.unlock();

  // Load outside the registry lock: weight parsing is slow and must not
  // block concurrent acquires of other models. The factory sees the RESOLVED
  // gpu decision via a copy of the manifest contract (providers keep reading
  // runtime.gpu — the single source both paths share). The resolved CUDA
  // index travels with it so multi-device providers bind to exactly the
  // device the cache key was built from.
  ModelInfo effective = model;
  effective.runtime.gpu = device.gpu;
  effective.runtime.resolvedCudaIndex = device.gpu ? device.cudaIndex : 0;
  std::string error;
  ModelRuntimePtr session;
  try
  {
    session = factory( effective, effectiveHw, &error );
  }
  catch ( ... )
  {
    // The failed acquisition must return its reservation even when the
    // factory throws — otherwise the identity stays reserved forever and
    // every later acquire of this model is refused.
    if ( device.gpu && estimateMb > 0 )
    {
      lock.lock();
      m_ledger.release( device.cudaIndex, estimateMb, identityComponent );
      lock.unlock();
    }
    throw;
  }
  if ( !session )
  {
    // The failed acquisition must return its reservation — admission and
    // load are one transaction.
    if ( device.gpu && estimateMb > 0 )
    {
      lock.lock();
      m_ledger.release( device.cudaIndex, estimateMb, identityComponent );
      lock.unlock();
    }
    if ( errorMessage )
      *errorMessage = error.empty() ? "failed to load model session" : error;
    return nullptr;
  }

  lock.lock();
  // Re-run idle eviction: time passed while the weights were loading.
  // Another thread may have loaded the same key meanwhile; prefer theirs and
  // let ours be released, keeping the cache size invariant simple.
  const auto raced = m_cache.find( key );
  if ( raced != m_cache.end() )
  {
    raced->second.lastUsed = ++m_useCounter;
    raced->second.lastUsedMs = QDateTime::currentMSecsSinceEpoch();
    // The raced entry and this acquisition share the same ledger holder
    // identity (framework+device+digest) — our tryReserve re-asserted the
    // entry's own reservation, so there is nothing to release here.
    return raced->second.session;
  }
  CacheEntry entry;
  entry.session = session;
  entry.lastUsed = ++m_useCounter;
  entry.lastUsedMs = QDateTime::currentMSecsSinceEpoch();
  entry.cudaIndex = device.gpu ? device.cudaIndex : -1;
  entry.reservedVramMb = device.gpu ? std::max( 0, estimateMb ) : 0;
  entry.ledgerHolder = identityComponent;
  m_cache[key] = std::move( entry );
  ++m_totalLoaded;
  while ( m_cache.size() > m_maxSessions )
  {
    auto lru = m_cache.begin();
    for ( auto it = m_cache.begin(); it != m_cache.end(); ++it )
    {
      if ( it->second.lastUsed < lru->second.lastUsed )
        lru = it;
    }
    dropReservationLocked( lru->second );
    m_cache.erase( lru );
    ++m_evictions;
  }
  return session;
}

void ModelRuntimeRegistry::evictExpiredLocked( std::int64_t nowMs )
{
  if ( m_idleEvictionMs == 0 )
    return; // disabled
  for ( auto it = m_cache.begin(); it != m_cache.end(); )
  {
    if ( nowMs - it->second.lastUsedMs > static_cast<std::int64_t>( m_idleEvictionMs ) )
    {
      dropReservationLocked( it->second );
      it = m_cache.erase( it );
      ++m_evictions;
    }
    else
      ++it;
  }
}

void ModelRuntimeRegistry::dropReservationLocked( const CacheEntry &entry )
{
  if ( entry.cudaIndex >= 0 && entry.reservedVramMb > 0 && !entry.ledgerHolder.empty() )
    m_ledger.release( entry.cudaIndex, entry.reservedVramMb, entry.ledgerHolder );
}

void ModelRuntimeRegistry::evictDeviceLocked( int cudaIndex )
{
  // LRU-ordered eviction of the sessions pinned to one device. Called only
  // from the failed-admission path: ONE pass, bounded by the cache size.
  std::vector<std::unordered_map<std::string, CacheEntry>::iterator> victims;
  for ( auto it = m_cache.begin(); it != m_cache.end(); ++it )
    if ( it->second.cudaIndex == cudaIndex )
      victims.push_back( it );
  std::sort( victims.begin(), victims.end(),
             []( const auto &a, const auto &b ) { return a->second.lastUsed < b->second.lastUsed; } );
  for ( auto &victim : victims )
  {
    dropReservationLocked( victim->second );
    m_cache.erase( victim );
    ++m_evictions;
  }
}

void ModelRuntimeRegistry::releaseAll()
{
  std::lock_guard<std::mutex> lock( m_mutex );
  for ( auto &entry : m_cache )
    dropReservationLocked( entry.second );
  m_cache.clear();
}

void ModelRuntimeRegistry::release( const std::string &framework, const std::string &identity )
{
  if ( identity.empty() )
    return; // an empty identity would suffix-match everything — refuse by contract
  std::lock_guard<std::mutex> lock( m_mutex );
  // Cache keys are framework|device|identity, and the identity token (digest
  // or fallback) concludes the key — so a suffix match over entries sharing
  // the framework prefix is an exact identity match across devices.
  const std::string prefix = framework + "|";
  for ( auto it = m_cache.begin(); it != m_cache.end(); )
  {
    const std::string &key = it->first;
    // Exact final-segment match: the identity must be the WHOLE text after
    // the last '|', not a suffix of it (fallback identities can be suffixes
    // of one another; digests cannot, but the contract is uniform).
    const std::string::size_type lastSep = key.rfind( '|' );
    if ( lastSep != std::string::npos && key.size() - lastSep - 1 == identity.size()
         && key.compare( lastSep + 1, identity.size(), identity ) == 0
         && key.compare( 0, prefix.size(), prefix ) == 0 )
    {
      dropReservationLocked( it->second );
      it = m_cache.erase( it );
    }
    else
      ++it;
  }
}

void ModelRuntimeRegistry::setMaxCachedSessions( std::size_t maxSessions )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_maxSessions = std::max<std::size_t>( 1, maxSessions );
  while ( m_cache.size() > m_maxSessions )
  {
    auto lru = m_cache.begin();
    for ( auto it = m_cache.begin(); it != m_cache.end(); ++it )
    {
      if ( it->second.lastUsed < lru->second.lastUsed )
        lru = it;
    }
    dropReservationLocked( lru->second );
    m_cache.erase( lru );
  }
}

std::size_t ModelRuntimeRegistry::maxCachedSessions() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_maxSessions;
}

std::size_t ModelRuntimeRegistry::cachedSessionCount() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_cache.size();
}

std::size_t ModelRuntimeRegistry::totalSessionsLoaded() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_totalLoaded;
}

void ModelRuntimeRegistry::resetLoadCount()
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_totalLoaded = 0;
}

void ModelRuntimeRegistry::setIdleEvictionMs( std::uint64_t idleMs )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_idleEvictionMs = idleMs;
}

std::uint64_t ModelRuntimeRegistry::idleEvictionMs() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_idleEvictionMs;
}

ModelRuntimeRegistry::PoolStats ModelRuntimeRegistry::poolStats() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  PoolStats stats;
  stats.cachedSessions = m_cache.size();
  stats.maxSessions = m_maxSessions;
  stats.totalLoads = m_totalLoaded;
  stats.cacheHits = m_cacheHits;
  stats.cacheMisses = m_cacheMisses;
  stats.evictions = m_evictions;
  return stats;
}

void ModelRuntimeRegistry::registerProvider( const std::string &framework, ModelRuntimeFactory factory,
                                             const ProviderTraits &traits )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_providers[framework] = ProviderEntry{ std::move( factory ), traits };
}

bool ModelRuntimeRegistry::hasProvider( const std::string &framework ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_providers.find( framework ) != m_providers.end();
}

std::optional<ProviderTraits> ModelRuntimeRegistry::providerTraits( const std::string &framework ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_providers.find( framework );
  if ( it == m_providers.end() )
    return std::nullopt;
  return it->second.traits;
}

ModelHardwareCapabilities ModelRuntimeRegistry::hardware() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_hardwareOverride ? *m_hardwareOverride : ModelHardwareCapabilities::detect();
}

void ModelRuntimeRegistry::setHardwareForTest( const std::optional<ModelHardwareCapabilities> &capabilities )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_hardwareOverride = capabilities;
}

// --- Platform 8.0 WP-B: policy + pressure observability ----------------------

void ModelRuntimeRegistry::setPlacementPolicy( DevicePlacementPolicy policy )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_placementPolicy = policy;
}

DevicePlacementPolicy ModelRuntimeRegistry::placementPolicy() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_placementPolicy;
}

std::vector<VramLedger::DeviceState> ModelRuntimeRegistry::deviceReport() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_ledger.snapshot();
}

} // namespace sicnu::operators::runtime
