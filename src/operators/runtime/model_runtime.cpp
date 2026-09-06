// src/operators/runtime/model_runtime.cpp
#include "operators/runtime/model_runtime.h"

#include "operators/framework/artifact_digest.h"
#include "operators/runtime/opencv_dnn_runtime.h"
#include "operators/runtime/onnxruntime_provider.h"

#include <QDateTime>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <opencv2/dnn.hpp>

#include <algorithm>
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
std::string contentDigestFor( const ModelInfo &model )
{
  if ( !model.contentDigest.empty() )
    return model.contentDigest;
  if ( model.resolvedArtifactPath.empty() )
    return std::string();

  const QString path = QString::fromStdString( model.resolvedArtifactPath );
  const QFileInfo info( path );
  if ( !info.exists() || !info.isFile() )
    return std::string();
  const unsigned long long sizeBytes = static_cast<unsigned long long>( info.size() );
  const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();

  std::lock_guard<std::mutex> lock( digestMemoMutex() );
  auto &memo = digestMemo();
  for ( const auto &entry : memo )
  {
    if ( entry.path == model.resolvedArtifactPath && entry.sizeBytes == sizeBytes
         && entry.mtimeMs == mtimeMs )
      return entry.digest;
  }
  const std::string digest = sicnu::operators::artifactSha256Hex( model.resolvedArtifactPath );
  if ( digest.empty() )
    return std::string(); // unreadable artifact — caller falls back
  if ( memo.size() > 64 )
    memo.clear();
  memo.push_back( DigestMemoEntry{ model.resolvedArtifactPath, sizeBytes, mtimeMs, digest } );
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
  // exactly one device through this backend. Multi-GPU hosts declare a
  // larger count via env for runtimes that can address them (onnxruntime).
  caps.cudaDeviceCount = caps.cudaAvailable ? 1 : 0;

  // Explicit overrides (tests and constrained deployments).
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QString gpu = env.value( QStringLiteral( "SICNU_MODEL_GPU" ) );
  if ( gpu == QStringLiteral( "1" ) || gpu.compare( QStringLiteral( "true" ), Qt::CaseInsensitive ) == 0 )
    caps.cudaAvailable = true;
  else if ( gpu == QStringLiteral( "0" ) || gpu.compare( QStringLiteral( "false" ), Qt::CaseInsensitive ) == 0 )
    caps.cudaAvailable = false;
  bool vramOk = false;
  const int vramMb = env.value( QStringLiteral( "SICNU_MODEL_VRAM_MB" ) ).toInt( &vramOk );
  if ( vramOk && vramMb > 0 )
    caps.vramBudgetMb = vramMb;
  bool devicesOk = false;
  const int devices = env.value( QStringLiteral( "SICNU_MODEL_CUDA_DEVICES" ) ).toInt( &devicesOk );
  if ( devicesOk && devices >= 0 )
    caps.cudaDeviceCount = devices;
  if ( caps.cudaAvailable && caps.cudaDeviceCount < 1 )
    caps.cudaDeviceCount = 1;
  else if ( !caps.cudaAvailable )
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
  const auto fitsBudget = [ & ]() {
    return hw.vramBudgetMb <= 0 || estimatedVramMb <= hw.vramBudgetMb;
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
      if ( !hw.cudaAvailable )
        return refuse( "CUDA is unavailable on this host" );
      if ( !cudaAvailable( request.cudaIndex ) )
        return refuse( "cuda:" + std::to_string( request.cudaIndex )
                       + " is not addressable by this backend/host (addressable indices: 0.."
                         + std::to_string( std::min( hw.cudaDeviceCount - 1, maxAddressableCudaIndex ) )
                         + ")" );
      if ( fitsBudget() )
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
      // Deterministic: lowest addressable index that fits, else cpu. Auto
      // means "best available" — cpu is always a valid answer unless the
      // model itself forbids fallback (a readiness/contract error).
      if ( modelWantsGpu && cudaAvailable( 0 ) && fitsBudget() )
      {
        ResolvedDevice device;
        device.gpu = true;
        device.cudaIndex = 0;
        return answer( device );
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
  if ( contains( "out of memory" ) || contains( "oom" ) || contains( "bad_alloc" )
       || contains( "cuda_error_out_of_memory" ) || contains( "cudamalloc" )
       || contains( "alloc failed" ) || contains( "allocation failure" ) )
    return InferenceFailureKind::OutOfMemory;
  if ( contains( "not loaded" ) )
    return InferenceFailureKind::NotLoaded;
  if ( contains( "shape" ) || contains( "dimension" ) || contains( "size mismatch" )
       || contains( "wrong input" ) || contains( "channels" ) )
    return InferenceFailureKind::ShapeMismatch;
  if ( contains( "failed to load" ) || contains( "parse" ) || contains( "protobuf" )
       || contains( "proto:" ) || contains( "corrupt" ) )
    return InferenceFailureKind::CorruptModel;
  return InferenceFailureKind::Unknown;
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
  if ( model.runtime.device.empty() )
  {
    if ( model.runtime.gpu && !model.runtime.cpuFallback )
    {
      if ( !hw.cudaAvailable )
        return fail( ModelReadiness::IncompatibleHardware,
                     "model requires GPU execution but CUDA is unavailable and CPU fallback is disabled" );
      if ( hw.vramBudgetMb > 0 && model.runtime.estimatedVramMb > hw.vramBudgetMb )
        return fail( ModelReadiness::IncompatibleHardware,
                     "model requires " + std::to_string( model.runtime.estimatedVramMb )
                       + " MiB VRAM but the host budget is " + std::to_string( hw.vramBudgetMb ) + " MiB" );
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
  if ( !resolveDevice( request, hw, model.runtime.gpu, model.runtime.estimatedVramMb,
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

ModelRuntimeRegistry::ModelRuntimeRegistry()
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
  registerOnnxRuntimeProvider();
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
  const auto provider = m_providers.find( framework );
  if ( provider == m_providers.end() )
  {
    lock.unlock();
    if ( errorMessage )
      *errorMessage = "no runtime provider available for framework '" + framework + "' in this build";
    return nullptr;
  }

  // Platform 4.0 device resolution (deterministic pure function). The
  // resolved device is part of the cache key, so a fallback switch can never
  // serve a stale-GPU session. An unhonorable explicit request fails loudly.
  ResolvedDevice device;
  std::string deviceWhy;
  if ( !resolveDevice( request, hw, model.runtime.gpu, model.runtime.estimatedVramMb,
                       provider->second.traits.maxAddressableCudaIndex, model.runtime.cpuFallback,
                       &device, &deviceWhy ) )
  {
    lock.unlock();
    if ( errorMessage )
      *errorMessage = "cannot honor device request '" + request.toString() + "' for model '"
                        + model.name + "': " + ( deviceWhy.empty() ? std::string( "unresolvable" )
                                                                   : deviceWhy );
    return nullptr;
  }
  const std::string key = framework + "|" + device.toString() + "|" + identityComponent;

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
  // runtime.gpu — the single source both paths share).
  ModelInfo effective = model;
  effective.runtime.gpu = device.gpu;
  std::string error;
  ModelRuntimePtr session = provider->second.factory( effective, hw, &error );
  if ( !session )
  {
    if ( errorMessage )
      *errorMessage = error.empty() ? "failed to load model session" : error;
    return nullptr;
  }

  lock.lock();
  // Idle eviction runs once per acquire attempt (cheap when disabled).
  evictExpiredLocked( QDateTime::currentMSecsSinceEpoch() );
  // Another thread may have loaded the same key meanwhile; prefer theirs and
  // let ours be released, keeping the cache size invariant simple.
  const auto raced = m_cache.find( key );
  if ( raced != m_cache.end() )
  {
    raced->second.lastUsed = ++m_useCounter;
    raced->second.lastUsedMs = QDateTime::currentMSecsSinceEpoch();
    return raced->second.session;
  }
  m_cache[key] = CacheEntry{ session, ++m_useCounter, QDateTime::currentMSecsSinceEpoch() };
  ++m_totalLoaded;
  while ( m_cache.size() > m_maxSessions )
  {
    auto lru = m_cache.begin();
    for ( auto it = m_cache.begin(); it != m_cache.end(); ++it )
    {
      if ( it->second.lastUsed < lru->second.lastUsed )
        lru = it;
    }
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
      it = m_cache.erase( it );
      ++m_evictions;
    }
    else
      ++it;
  }
}

void ModelRuntimeRegistry::releaseAll()
{
  std::lock_guard<std::mutex> lock( m_mutex );
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
    if ( key.rfind( prefix, 0 ) == 0 && key.size() > prefix.size() && identity.size() <= key.size()
         && key.compare( key.size() - identity.size(), identity.size(), identity ) == 0 )
      it = m_cache.erase( it );
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

} // namespace sicnu::operators::runtime
