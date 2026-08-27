// src/operators/runtime/model_runtime.cpp
#include "operators/runtime/model_runtime.h"

#include "operators/runtime/opencv_dnn_runtime.h"

#include <QProcessEnvironment>

#include <opencv2/dnn.hpp>

#include <algorithm>
#include <stdexcept>

namespace sicnu::operators::runtime {

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

  return caps;
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
}

ModelRuntimePtr ModelRuntimeRegistry::acquire( const ModelInfo &model, std::string *errorMessage )
{
  const ModelHardwareCapabilities hw = hardware();
  const std::string framework = model.framework;
  const std::string artifact = model.resolvedArtifactPath;

  std::unique_lock<std::mutex> lock( m_mutex );
  const auto provider = m_providers.find( framework );
  if ( provider == m_providers.end() )
  {
    lock.unlock();
    if ( errorMessage )
      *errorMessage = "no runtime provider available for framework '" + framework + "' in this build";
    return nullptr;
  }

  // The cache key includes the device the session would run on so a fallback
  // switch (cuda unavailable → cpu) can never serve a stale-GPU session.
  const bool wantGpu = model.runtime.gpu && hw.cudaAvailable;
  const std::string key = framework + "|" + artifact + "|" + ( wantGpu ? "cuda" : "cpu" );

  const auto cached = m_cache.find( key );
  if ( cached != m_cache.end() )
  {
    cached->second.lastUsed = ++m_useCounter;
    return cached->second.session;
  }
  lock.unlock();

  // Load outside the registry lock: weight parsing is slow and must not
  // block concurrent acquires of other models.
  std::string error;
  ModelRuntimePtr session = provider->second( model, hw, &error );
  if ( !session )
  {
    if ( errorMessage )
      *errorMessage = error.empty() ? "failed to load model session" : error;
    return nullptr;
  }

  lock.lock();
  // Another thread may have loaded the same key meanwhile; prefer theirs and
  // let ours be released, keeping the cache size invariant simple.
  const auto raced = m_cache.find( key );
  if ( raced != m_cache.end() )
  {
    raced->second.lastUsed = ++m_useCounter;
    return raced->second.session;
  }
  m_cache[key] = CacheEntry{ session, ++m_useCounter };
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
  }
  return session;
}

void ModelRuntimeRegistry::releaseAll()
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_cache.clear();
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

void ModelRuntimeRegistry::registerProvider( const std::string &framework, ModelRuntimeFactory factory )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_providers[framework] = std::move( factory );
}

bool ModelRuntimeRegistry::hasProvider( const std::string &framework ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  return m_providers.find( framework ) != m_providers.end();
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
