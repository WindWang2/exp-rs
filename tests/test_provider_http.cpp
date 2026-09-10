// tests/test_provider_http.cpp — Platform 7.0 HTTP inference provider over a
// QTcpServer loopback fake implementing the exp-rs-infer/1 wire contract:
// known-answer inference, HTTP error mapping, transport failure mapping and
// protocol-mismatch refusal. All through the SAME ModelRuntimeRegistry seam.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provider_wire.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;

/// The provider stack (QNetworkAccessManager) needs a QCoreApplication for
/// event dispatch; Catch2 owns main, so create it lazily per test case.
QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_provider_http";
  static char *argv[] = { name, nullptr };
  static QCoreApplication app( argc, argv );
  return app;
}

/// Loopback HTTP service implementing the wire contract: outputs = sum of
/// every input tensor's last element as a 1x1x1x1 float32. Configurable
/// failure modes cover the error mappings.
class LoopbackInferServer
{
  public:
    explicit LoopbackInferServer( int httpStatus = 200 )
    {
      m_server = std::make_unique<QTcpServer>();
      m_server->listen( QHostAddress::LocalHost );
      m_port = m_server->serverPort();
      m_httpStatus = httpStatus;
      QObject::connect(
        m_server.get(), &QTcpServer::newConnection, [this] {
          QTcpSocket *socket = m_server->nextPendingConnection();
          requestsServed++;
          QObject::connect( socket, &QTcpSocket::readyRead, [this, socket] {
            m_buffer[socket] += socket->readAll();
            const QByteArray &buf = m_buffer[socket];
            const int headerEnd = buf.indexOf( "\r\n\r\n" );
            if ( headerEnd < 0 )
              return;
            qint64 contentLength = 0;
            for ( const QByteArray &line : buf.split( '\n' ) )
            {
              const QByteArray lowered = line.trimmed().toLower();
              if ( lowered.startsWith( "content-length:" ) )
                contentLength = lowered.mid( 15 ).trimmed().toLongLong();
            }
            if ( buf.size() - headerEnd - 4 < contentLength )
              return; // body not complete yet
            const QByteArray body = buf.mid( headerEnd + 4, static_cast<int>( contentLength ) );
            m_buffer.remove( socket );

            QByteArray responseBody;
            if ( m_httpStatus != 200 )
            {
              responseBody = "{\"error\":\"injected server failure\"}";
            }
            else
            {
              const QJsonDocument doc = QJsonDocument::fromJson( body );
              const QJsonObject request = doc.object();
              double total = 0.0;
              for ( const auto &entry : request.value( QStringLiteral( "inputs" ) ).toArray() )
              {
                const QJsonObject tensor = entry.toObject();
                total += lastElement( tensor );
              }
              QJsonObject out;
              out.insert( QStringLiteral( "name" ), QStringLiteral( "sum" ) );
              QJsonArray shape;
              shape.append( 1 );
              shape.append( 1 );
              shape.append( 1 );
              shape.append( 1 );
              out.insert( QStringLiteral( "shape" ), shape );
              out.insert( QStringLiteral( "dtype" ), QStringLiteral( "float32" ) );
              out.insert( QStringLiteral( "data_base64" ), encodeFloat( static_cast<float>( total ) ) );
              QJsonArray outputs;
              outputs.append( out );
              QJsonObject response;
              if ( m_protocolMismatch )
                response.insert( QStringLiteral( "protocol" ), QStringLiteral( "bogus/9" ) );
              response.insert( QStringLiteral( "outputs" ), outputs );
              responseBody = QJsonDocument( response ).toJson( QJsonDocument::Compact );
            }

            QByteArray http;
            http += "HTTP/1.1 " + QByteArray::number( m_httpStatus ) + " FAKE\r\n";
            http += "Content-Type: application/json\r\n";
            http += "Content-Length: " + QByteArray::number( responseBody.size() ) + "\r\n";
            http += "Connection: close\r\n\r\n";
            http += responseBody;
            socket->write( http );
            socket->disconnectFromHost();
          } );
          QObject::connect( socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater );
        } );
    }

    ~LoopbackInferServer() { m_server->close(); }

    quint16 port() const { return m_port; }
    int requestsServed = 0;
    bool m_protocolMismatch = false;

  private:
    static QString encodeFloat( float value )
    {
      return QString::fromLatin1(
        QByteArray( reinterpret_cast<const char *>( &value ), 4 ).toBase64() );
    }

    static double lastElement( const QJsonObject &tensor )
    {
      const QByteArray raw =
        QByteArray::fromBase64( tensor.value( QStringLiteral( "data_base64" ) ).toString().toLatin1() );
      float value = 0.0f;
      if ( tensor.value( QStringLiteral( "dtype" ) ).toString() == QStringLiteral( "float32" )
           && raw.size() >= 4 )
        value = *reinterpret_cast<const float *>( raw.constData() + raw.size() - 4 );
      return value;
    }

    std::unique_ptr<QTcpServer> m_server;
    quint16 m_port = 0;
    int m_httpStatus = 200;
    QMap<QTcpSocket *, QByteArray> m_buffer;
};

/// Builds a ready http model bound to @p endpoint with a local artifact.
/// @p tag makes the artifact bytes unique per test case: the session pool
/// shares sessions by content digest, so equal bytes would silently reuse
/// another case's endpoint.
ModelInfo makeHttpModel( const QTemporaryDir &dir, const std::string &endpoint,
                         const std::string &tag )
{
  const QString weights = dir.filePath( QStringLiteral( "weights.bin" ) );
  QFile file( weights );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  file.write( QByteArray( "http-fake-weights:" ) + QByteArray::fromStdString( tag ) );
  file.close();

  ModelInfo model;
  model.name = "m5-http";
  model.framework = "http";
  model.readiness = ModelReadiness::Ready;
  model.resolvedArtifactPath = weights.toStdString();
  model.runtime.provider.url = endpoint;
  model.runtime.provider.timeoutMs = 5000;
  return model;
}

const ModelRuntimePtr acquireModel( const ModelInfo &model )
{
  std::string error;
  auto session = ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  INFO( "acquire error: " << error );
  REQUIRE( session );
  return session;
}

} // namespace

TEST_CASE( "http provider known answer through the registry seam", "[models][http]" )
{
  ( void )ensureApp();
  LoopbackInferServer server;
  QTemporaryDir dir;
  const ModelInfo model =
    makeHttpModel( dir, "http://127.0.0.1:" + std::to_string( server.port() ) + "/infer",
                  std::to_string( server.port() ) );
  const auto session = acquireModel( model );

  CHECK( session->framework() == "http" );
  CHECK( session->backendName() == "http" );
  CHECK( session->deviceName() == "remote" );
  const auto caps = session->capabilities();
  CHECK( caps.multiInput );
  CHECK( caps.namedBind );

  std::vector<float> a = { 5.0f };
  std::vector<float> b = { 3.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "before", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );
  inputs.push_back( NamedTensor{ "after", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, b.data(), 1 ) } );
  const auto outputs = session->inferNamed( inputs, {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs[0].second.rank() == 4 );
  CHECK( outputs[0].second.dataFloat32()[0] == Catch::Approx( 8.0f ) );
  CHECK( server.requestsServed >= 1 );
  CHECK( session->health().ok );
  CHECK( session->health().forwardsCompleted == 1 );
}

TEST_CASE( "http provider maps HTTP errors to ProviderCrash", "[models][http]" )
{
  ( void )ensureApp();
  LoopbackInferServer server( /*httpStatus*/ 500 );
  QTemporaryDir dir;
  const ModelInfo model =
    makeHttpModel( dir, "http://127.0.0.1:" + std::to_string( server.port() ) + "/infer",
                  std::to_string( server.port() ) );
  const auto session = acquireModel( model );

  std::vector<float> a = { 1.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );
  REQUIRE_THROWS_AS( session->inferNamed( inputs, {} ), std::runtime_error );
  try
  {
    session->inferNamed( inputs, {} );
  }
  catch ( const std::exception &e )
  {
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::ProviderCrash );
    CHECK( errorCodeForInferenceFailure( InferenceFailureKind::ProviderCrash )
           == sicnu::operators::ErrorCode::RuntimeProviderFailed );
  }
}

TEST_CASE( "http provider maps transport failure to ProviderCrash", "[models][http]" )
{
  ( void )ensureApp();
  // A port with no listener: connect refused.
  QTcpServer probe;
  REQUIRE( probe.listen( QHostAddress::LocalHost ) );
  const quint16 deadPort = probe.serverPort();
  probe.close();

  QTemporaryDir dir;
  const ModelInfo model =
    makeHttpModel( dir, "http://127.0.0.1:" + std::to_string( deadPort ) + "/infer", "dead" );
  const auto session = acquireModel( model );

  std::vector<float> a = { 1.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );
  try
  {
    session->inferNamed( inputs, {} );
    FAIL( "expected a transport refusal" );
  }
  catch ( const std::exception &e )
  {
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::ProviderCrash );
  }
}

TEST_CASE( "http provider refuses foreign wire protocols", "[models][http]" )
{
  ( void )ensureApp();
  LoopbackInferServer server;
  server.m_protocolMismatch = true;
  QTemporaryDir dir;
  const ModelInfo model =
    makeHttpModel( dir, "http://127.0.0.1:" + std::to_string( server.port() ) + "/infer",
                  std::to_string( server.port() ) );
  const auto session = acquireModel( model );

  std::vector<float> a = { 1.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );
  try
  {
    session->inferNamed( inputs, {} );
    FAIL( "expected a protocol refusal" );
  }
  catch ( const std::exception &e )
  {
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::IncompatibleSchema );
  }
}
