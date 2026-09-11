// src/operators/runtime/opencv_dnn_runtime.cpp
#include "operators/runtime/opencv_dnn_runtime.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace sicnu::operators::runtime {

OpenCvDnnRuntime::OpenCvDnnRuntime( std::string artifactPath, bool modelWantsGpu,
                                    const ModelHardwareCapabilities &hw )
    : m_artifactPath( std::move( artifactPath ) ), m_modelWantsGpu( modelWantsGpu ), m_hw( hw )
{
}

bool OpenCvDnnRuntime::load( std::string *errorMessage )
{
  auto fail = [errorMessage]( const std::string &why ) {
    if ( errorMessage )
      *errorMessage = why;
    return false;
  };

  if ( m_artifactPath.empty() )
    return fail( "no artifact path to load" );
  if ( !QFile::exists( QString::fromStdString( m_artifactPath ) ) )
    return fail( "model artifact not found: " + m_artifactPath );

  try
  {
    m_net = cv::dnn::readNetFromONNX( m_artifactPath );
  }
  catch ( const cv::Exception &e )
  {
    return fail( std::string( "failed to load ONNX model: " ) + e.what() );
  }
  if ( m_net.empty() )
    return fail( "loaded model is empty: " + m_artifactPath );

  // Backend/target selection - the honest version of the manifest's gpu flag:
  // CUDA only when the model wants it AND the OpenCV build offers it. The
  // VRAM budget demotion happens in ModelRuntimeRegistry::acquire (with
  // cpu_fallback enabled, readiness deliberately skips the VRAM check, so
  // the runtime owns the demotion - #646).
  const bool useCuda = m_modelWantsGpu && m_hw.cudaAvailable;
  if ( useCuda )
  {
    m_net.setPreferableBackend( cv::dnn::DNN_BACKEND_CUDA );
    m_net.setPreferableTarget( cv::dnn::DNN_TARGET_CUDA );
    m_deviceName = "cuda";
  }
  else
  {
    m_net.setPreferableBackend( cv::dnn::DNN_BACKEND_OPENCV );
    m_net.setPreferableTarget( cv::dnn::DNN_TARGET_CPU );
    m_deviceName = "cpu";
  }

  m_loaded = true;
  return true;
}

cv::Mat OpenCvDnnRuntime::infer( const cv::Mat &nchwBlob )
{
  return infer( nchwBlob, std::string() );
}

cv::Mat OpenCvDnnRuntime::infer( const cv::Mat &nchwBlob, const std::string &outputName )
{
  if ( !m_loaded )
    throw std::runtime_error( "runtime session is not loaded" );
  if ( nchwBlob.empty() || nchwBlob.dims != 4 )
    throw std::runtime_error( "inference input must be a 4-D NCHW blob" );
  if ( m_cancelRequested.load( std::memory_order_relaxed ) )
    throw std::runtime_error( "inference canceled before the forward pass" );

  const auto started = std::chrono::steady_clock::now();
  cv::Mat output;
  try
  {
    // cv::dnn::Net permits one forward pass at a time per instance; serialize
    // so a cached session is safe to share across TaskCenter workers.
    std::lock_guard<std::mutex> lock( m_inferMutex );
    // Re-check after acquiring the lock: a cancel may have landed while this
    // caller waited for an in-flight forward.
    if ( m_cancelRequested.load( std::memory_order_relaxed ) )
      throw std::runtime_error( "inference canceled before the forward pass" );
    m_net.setInput( nchwBlob );
    output = outputName.empty() ? m_net.forward() : m_net.forward( outputName );
  }
  catch ( const cv::Exception &e )
  {
    {
      std::lock_guard<std::mutex> healthLock( m_healthMutex );
      m_lastError = e.what();
    }
    m_failures.fetch_add( 1, std::memory_order_relaxed );
    throw std::runtime_error( std::string( "forward pass failed: " ) + e.what() );
  }
  if ( output.empty() )
  {
    {
      std::lock_guard<std::mutex> healthLock( m_healthMutex );
      m_lastError = "inference produced an empty output";
    }
    m_failures.fetch_add( 1, std::memory_order_relaxed );
    throw std::runtime_error( "inference produced an empty output" );
  }
  {
    std::lock_guard<std::mutex> healthLock( m_healthMutex );
    m_lastForwardMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started )
                        .count();
    m_lastError.clear();
  }
  m_forwards.fetch_add( 1, std::memory_order_relaxed );
  // forward() returns a header onto the net's internal output blob: the next
  // forward() overwrites it. TTA averaging and multi-head stacking hold the
  // result across further forwards, so detach (one tile-sized copy,
  // negligible next to the forward pass itself).
  return output.clone();
}

std::vector<cv::Mat> OpenCvDnnRuntime::inferMulti( const std::vector<NamedBlob> &namedBlobs )
{
  if ( !m_loaded )
    throw std::runtime_error( "runtime session is not loaded" );
  if ( namedBlobs.empty() )
    throw std::runtime_error( "multi-input inference needs at least one input blob" );
  if ( m_cancelRequested.load( std::memory_order_relaxed ) )
    throw std::runtime_error( "inference canceled before the forward pass" );
  for ( const NamedBlob &nb : namedBlobs )
  {
    if ( nb.second.empty() || nb.second.dims != 4 )
      throw std::runtime_error( "multi-input inference blobs must be 4-D NCHW (input '" +
                                nb.first + "')" );
  }

  std::lock_guard<std::mutex> lock( m_inferMutex );
  for ( const NamedBlob &nb : namedBlobs )
  {
    if ( nb.first.empty() )
      m_net.setInput( nb.second );
    else
      m_net.setInput( nb.second, nb.first );
  }
  std::vector<cv::Mat> outputs;
  try
  {
    m_net.forward( outputs, m_net.getUnconnectedOutLayersNames() );
  }
  catch ( const cv::Exception &e )
  {
    throw std::runtime_error( std::string( "multi-input forward pass failed: " ) + e.what() );
  }
  outputs.erase( std::remove_if( outputs.begin(), outputs.end(),
                                 []( const cv::Mat &m ) { return m.empty(); } ),
                 outputs.end() );
  if ( outputs.empty() )
    throw std::runtime_error( "multi-input inference produced no usable output" );
  return outputs;
}

std::vector<std::string> OpenCvDnnRuntime::outputTensorNames() const
{
  // cv::dnn::Net is not thread-safe: serialize against concurrent forward
  // passes on the shared cached session.
  std::lock_guard<std::mutex> lock( *const_cast<std::mutex *>( &m_inferMutex ) );
  std::vector<std::string> names;
  if ( !m_loaded )
    return names;
  try
  {
    for ( const auto &name : m_net.getUnconnectedOutLayersNames() )
      names.push_back( name ); // cv::String == std::string
  }
  catch ( const cv::Exception & )
  {
    // Enumeration is best-effort; an empty list reads as "unknown" upstream.
  }
  return names;
}

void OpenCvDnnRuntime::warmup()
{
  if ( !m_loaded )
    return;
  // Throwaway probe input. Graphs with a fixed input shape may reject it —
  // that is recorded, never fatal: warmup is an optimization, and correctness
  // must not depend on it.
  try
  {
    // 4-D NCHW probe blob (infer requires dims == 4).
    const int sizes[4] = { 1, 3, 64, 64 };
    const cv::Mat probe( 4, sizes, CV_32F, cv::Scalar( 0 ) );
    infer( probe );
  }
  catch ( const std::exception &e )
  {
    std::lock_guard<std::mutex> healthLock( m_healthMutex );
    m_lastError = std::string( "warmup skipped: " ) + e.what();
  }
}

SessionHealth OpenCvDnnRuntime::health() const
{
  SessionHealth health;
  health.ok = m_loaded && !m_cancelRequested.load( std::memory_order_relaxed );
  health.forwardsCompleted = m_forwards.load( std::memory_order_relaxed );
  health.failures = m_failures.load( std::memory_order_relaxed );
  std::lock_guard<std::mutex> healthLock( m_healthMutex );
  health.lastForwardMs = m_lastForwardMs;
  health.lastError = m_lastError;
  return health;
}

SessionMemoryEstimate OpenCvDnnRuntime::memoryEstimate() const
{
  SessionMemoryEstimate estimate;
  const QFileInfo info( QString::fromStdString( m_artifactPath ) );
  if ( info.exists() )
  {
    constexpr double kMiB = 1024.0 * 1024.0;
    estimate.weightsMb = static_cast<int>( std::ceil( info.size() / kMiB ) );
  }
  // workingSetMb stays 0: cv::dnn exposes no allocation introspection — the
  // tile engine's O(batch × patch) estimate is the documented working-set
  // model, we do not invent numbers here.
  return estimate;
}

ProviderRuntimeDetails OpenCvDnnRuntime::providerDetails() const
{
  ProviderRuntimeDetails details;
  details.executionProvider = m_deviceName == "cuda" ? "cuda" : "cpu";
  details.runtimeVersion = CV_VERSION;
  return details;
}

} // namespace sicnu::operators::runtime
