// src/operators/runtime/http_provider.cpp — HTTP provider implementation
// (Qt6::Network). The #ifdef keeps the no-Network build honest: framework
// "http" stays catalog-visible but surfaces runtime_unavailable.
#include "operators/runtime/http_provider.h"

#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provider_wire.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace sicnu::operators::runtime {

#ifdef SICNU_WITH_HTTP_PROVIDER

namespace {

/// One HTTP inference endpoint session. The remote service owns the weights;
/// the local artifact file (when any) exists for the content-digest session
/// identity and is announced to the service so it can pin its own weights.
class HttpRuntimeSession final : public IModelRuntime
{
  public:
    HttpRuntimeSession( std::string endpoint, std::string artifact, std::string digest,
                        int timeoutMs, long maxBodyMb )
        : m_endpoint( std::move( endpoint ) )
        , m_artifact( std::move( artifact ) )
        , m_digest( std::move( digest ) )
        , m_timeoutMs( timeoutMs > 0 ? timeoutMs : 30000 )
        , m_maxBodyBytes( maxBodyMb > 0
                            ? static_cast<qint64>( maxBodyMb ) * 1024 * 1024
                            : static_cast<qint64>( 256 ) * 1024 * 1024 )
    {
    }

    std::string framework() const override { return "http"; }
    std::string backendName() const override { return "http"; }
    std::string deviceName() const override { return "remote"; }
    std::string artifactPath() const override { return m_artifact; }

    std::vector<NamedTensor> inferNamed( const std::vector<NamedTensor> &inputs,
                                         const std::vector<std::string> &outputNames ) override
    {
      if ( m_cancelRequested.load( std::memory_order_relaxed ) )
        throw std::runtime_error( "inference canceled before the forward pass" );
      if ( inputs.empty() )
        throw std::runtime_error( "multi-input inference needs at least one input tensor" );

      const QJsonObject request = encodeInferRequest( inputs, outputNames, m_artifact, m_digest );
      const QByteArray payload = QJsonDocument( request ).toJson( QJsonDocument::Compact );

      QNetworkAccessManager manager;
      QNetworkRequest httpRequest( QUrl( QString::fromStdString( m_endpoint ) ) );
      httpRequest.setHeader( QNetworkRequest::ContentTypeHeader, QStringLiteral( "application/json" ) );
      httpRequest.setTransferTimeout( m_timeoutMs );

      QEventLoop loop;
      QTimer timeout;
      timeout.setSingleShot( true );
      QObject::connect( &timeout, &QTimer::timeout, &loop, &QEventLoop::quit );
      QNetworkReply *reply = manager.post( httpRequest, payload );
      QObject::connect( reply, &QNetworkReply::finished, &loop, &QEventLoop::quit );
      timeout.start( m_timeoutMs );
      loop.exec();
      timeout.stop();
      if ( !reply->isFinished() )
      {
        reply->abort();
        recordFailure( "no response from provider: request timed out" );
        reply->deleteLater();
        throw std::runtime_error( "no response from provider: request timed out" );
      }

      const QNetworkReply::NetworkError transportError = reply->error();
      const int statusCode =
        reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).isValid()
          ? reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt()
          : 0;
      const QByteArray body = reply->readAll();
      reply->deleteLater();

      if ( transportError != QNetworkReply::NoError && statusCode == 0 )
      {
        // Transport-level failure: the endpoint never answered.
        const std::string detail = transportErrorToString( transportError );
        recordFailure( "connection refused or reset: " + detail );
        throw std::runtime_error( "connection refused or reset: " + detail );
      }
      if ( statusCode < 200 || statusCode >= 300 )
      {
        std::string detail;
        const QJsonDocument doc = QJsonDocument::fromJson( body );
        if ( doc.isObject() )
          detail = doc.object().value( QStringLiteral( "error" ) ).toString().toStdString();
        recordFailure( "provider error HTTP " + std::to_string( statusCode )
                       + ( detail.empty() ? std::string() : ": " + detail ) );
        throw std::runtime_error( "provider error HTTP " + std::to_string( statusCode )
                                  + ( detail.empty() ? std::string() : ": " + detail ) );
      }
      if ( static_cast<qint64>( body.size() ) > m_maxBodyBytes )
      {
        recordFailure( "provider response exceeds the runtime.provider.max_body_mb guard" );
        throw std::runtime_error( "provider response exceeds the runtime.provider.max_body_mb "
                                  "guard (output invalid)" );
      }

      QJsonParseError parseError{};
      const QJsonDocument doc = QJsonDocument::fromJson( body, &parseError );
      if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
      {
        recordFailure( "provider response is not valid JSON (output invalid)" );
        throw std::runtime_error( "provider response is not valid JSON (output invalid)" );
      }
      const QJsonObject response = doc.object();
      try
      {
        checkWireProtocol( response );
        auto outputs = decodeInferOutputs( response.value( QStringLiteral( "outputs" ) ).toArray() );
        m_forwards.fetch_add( 1, std::memory_order_relaxed );
        return outputs;
      }
      catch ( const std::exception &e )
      {
        recordFailure( e.what() );
        throw;
      }
    }

    bool supportsMultiInput() const override { return true; }

    /// Historical cv::Mat fast path (bridges through the named surface).
    cv::Mat infer( const cv::Mat &nchwBlob ) override
    {
      auto outs =
        inferNamed( { NamedTensor{ std::string(), TensorBlob::fromMat( nchwBlob ) } }, {} );
      if ( outs.empty() || outs.front().second.rank() != 4 )
        throw std::runtime_error( "provider output is not rank-4 (the cv::Mat path carries "
                                  "rank-4; use inferNamed)" );
      return outs.front().second.toMat();
    }

    ProviderCapabilities capabilities() const override
    {
      ProviderCapabilities caps;
      caps.multiInput = true;
      caps.namedBind = true;
      caps.maxRank = 6;
      caps.batch = true;
      caps.cancelInForward = false; // only between requests
      caps.inputDtypes = { "float32", "float64", "int32", "int64", "uint8", "int8" };
      caps.outputDtypes = { "float32", "float64", "int32", "int64", "uint8", "int8" };
      return caps;
    }

    void requestCancel() override { m_cancelRequested.store( true, std::memory_order_relaxed ); }
    void clearCancel() override { m_cancelRequested.store( false, std::memory_order_relaxed ); }

    SessionHealth health() const override
    {
      SessionHealth health;
      health.ok = !m_cancelRequested.load( std::memory_order_relaxed );
      health.forwardsCompleted = m_forwards.load( std::memory_order_relaxed );
      health.failures = m_failures.load( std::memory_order_relaxed );
      std::lock_guard<std::mutex> lock( m_healthMutex );
      health.lastError = m_lastError;
      return health;
    }

    SessionMemoryEstimate memoryEstimate() const override
    {
      // The weights live on the service; a locally present artifact (digest
      // anchor) is the only honest number, the working set is unknown.
      SessionMemoryEstimate estimate;
      const QFileInfo info( QString::fromStdString( m_artifact ) );
      if ( info.exists() && info.isFile() )
        estimate.weightsMb = static_cast<int>( ( info.size() + ( 1 << 20 ) - 1 ) >> 20 );
      return estimate;
    }

  private:
    static const char *transportErrorToString( QNetworkReply::NetworkError error )
    {
      switch ( error )
      {
        case QNetworkReply::ConnectionRefusedError: return "connection refused";
        case QNetworkReply::RemoteHostClosedError: return "remote host closed the connection";
        case QNetworkReply::HostNotFoundError: return "host not found";
        case QNetworkReply::TimeoutError: return "request timed out";
        case QNetworkReply::OperationCanceledError: return "request aborted";
        default: return "transport failure";
      }
    }

    void recordFailure( const std::string &what )
    {
      m_failures.fetch_add( 1, std::memory_order_relaxed );
      std::lock_guard<std::mutex> lock( m_healthMutex );
      m_lastError = what;
    }

    std::string m_endpoint;
    std::string m_artifact;
    std::string m_digest;
    int m_timeoutMs = 30000;
    qint64 m_maxBodyBytes = 256 * 1024 * 1024;
    std::atomic<bool> m_cancelRequested{ false };
    std::atomic<std::uint64_t> m_forwards{ 0 };
    std::atomic<std::uint64_t> m_failures{ 0 };
    mutable std::mutex m_healthMutex;
    std::string m_lastError;
};

ModelRuntimePtr makeHttpRuntime( const ModelInfo &model, const ModelHardwareCapabilities &,
                                 std::string *errorMessage )
{
  if ( model.runtime.provider.url.empty() )
  {
    if ( errorMessage )
      *errorMessage = "framework 'http' requires runtime.provider.url";
    return nullptr;
  }
  return std::make_shared<HttpRuntimeSession>( model.runtime.provider.url,
                                               model.resolvedArtifactPath, model.contentDigest,
                                               model.runtime.provider.timeoutMs,
                                               model.runtime.provider.maxBodyMb );
}

const char *kHttpUnavailable = "";

} // namespace

bool httpProviderAvailable()
{
  return true;
}

void registerHttpProvider( ModelRuntimeRegistry &registry )
{
  registry.registerProvider( "http", makeHttpRuntime, ProviderTraits{} );
}

std::string httpProviderUnavailableReason()
{
  return kHttpUnavailable;
}

#else // !SICNU_WITH_HTTP_PROVIDER

bool httpProviderAvailable()
{
  return false;
}

void registerHttpProvider( ModelRuntimeRegistry & )
{
  // Graceful degradation: models declaring framework "http" surface
  // runtime_unavailable with the reason below.
}

std::string httpProviderUnavailableReason()
{
  return "this build was compiled without Qt6::Network — the HTTP inference "
         "provider is unavailable";
}

#endif

} // namespace sicnu::operators::runtime
