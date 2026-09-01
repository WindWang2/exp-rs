// src/operators/runtime/opencv_dnn_runtime.cpp
#include "operators/runtime/opencv_dnn_runtime.h"

#include <QFile>

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

  // cv::dnn::Net permits one forward pass at a time per instance; serialize
  // so a cached session is safe to share across TaskCenter workers.
  std::lock_guard<std::mutex> lock( m_inferMutex );
  m_net.setInput( nchwBlob );
  cv::Mat output = outputName.empty() ? m_net.forward() : m_net.forward( outputName );
  if ( output.empty() )
    throw std::runtime_error( "inference produced an empty output" );
  return output;
}

std::vector<std::string> OpenCvDnnRuntime::outputTensorNames() const
{
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

} // namespace sicnu::operators::runtime
