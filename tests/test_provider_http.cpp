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
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "support/offline_probe.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>

#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "support/qt_lifecycle.h"

// Track 2 R4 (PR #1335 exit-crash cluster, group 2): ordered teardown via the
// shared listener — drains deferred deletes, runs exitQgis()/invalidateCaches
// while guards are alive, deletes the app before glibc exit().
CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

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
  // Heap-owned (Track 2 R4 teardown contract): the shared TeardownListener
  // deletes QCoreApplication::instance() in ordered teardown; a value static
  // here would be atexit-registered AND heap-deleted — double ownership.
  static QCoreApplication *app = new QCoreApplication( argc, argv );
  return *app;
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

            if ( m_floodBytes > 0 )
            {
              // #1056 flood mode: advertise an enormous body, then stream it
              // chunk by chunk FROM THE EVENT LOOP so the client's cap guard
              // can abort mid-transfer. The socket records how much actually
              // reached the wire before the disconnect.
              QByteArray floodHead;
              floodHead += "HTTP/1.1 200 FLOOD\r\n";
              floodHead += "Content-Type: application/json\r\n";
              floodHead += "Content-Length: " + QByteArray::number( m_floodBytes ) + "\r\n";
              floodHead += "Connection: close\r\n\r\n";
              socket->write( floodHead );
              const QByteArray chunk( 256 * 1024, 'x' );
              auto *pump = new QTimer( socket );
              QObject::connect( pump, &QTimer::timeout, socket, [ socket, chunk, pump, this ] {
                if ( socket->state() != QAbstractSocket::ConnectedState )
                {
                  pump->stop();
                  return;
                }
                socket->write( chunk );
                m_floodQueued += chunk.size();
                if ( m_floodQueued >= m_floodBytes )
                  pump->stop();
              } );
              pump->start( 0 );
              m_buffer.remove( socket );
              return;
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
          // bytesWritten is a SIGNAL (bytes actually flushed to the OS
          // socket) — accumulate it to see how much of the flood reached
          // the wire before the client hung up.
          QObject::connect( socket, &QTcpSocket::bytesWritten, socket,
                            [ this ]( qint64 n ) { m_floodFlushed += n; } );
          QObject::connect( socket, &QTcpSocket::disconnected, socket, [ this, socket ] {
            m_floodDone = true;
            socket->deleteLater();
          } );
        } );
    }

    ~LoopbackInferServer() { m_server->close(); }

    quint16 port() const { return m_port; }
    int requestsServed = 0;
    bool m_protocolMismatch = false;
    /// #1056: when > 0, every response is a flood of this advertised size.
    qint64 m_floodBytes = 0;
    /// Bytes that reached the OS socket layer before the client hung up.
    qint64 floodFlushed() const { return m_floodFlushed.load(); }
    /// True once the client-induced disconnect was observed server-side.
    bool floodCompleted() const { return m_floodDone.load(); }

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
    std::atomic<qint64> m_floodQueued{ 0 };
    std::atomic<qint64> m_floodFlushed{ 0 };
    std::atomic<bool> m_floodDone{ false };
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

// ADR 0146: never die by timeout when the loopback transport is missing —
// report `sicnu-skip: <reason>` + exit 77 instead.
SICNU_OFFLINE_GUARD()

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

TEST_CASE( "http provider stops receiving at max_body_mb instead of buffering the flood (#1056)",
           "[models][http]" )
{
  ( void )ensureApp();
  // A hostile provider advertises (and streams) far more than the guard
  // allows. The provider must type-refuse at the threshold and abort the
  // transfer MID-STREAM: the old post-hoc readAll() check buffered the whole
  // body first, so the server always saw its full flood reach the wire.
  constexpr qint64 kFloodBytes = 64 * 1024 * 1024;
  LoopbackInferServer server;
  server.m_floodBytes = kFloodBytes;
  QTemporaryDir dir;
  ModelInfo model =
    makeHttpModel( dir, "http://127.0.0.1:" + std::to_string( server.port() ) + "/infer",
                   std::to_string( server.port() ) );
  model.runtime.provider.maxBodyMb = 1; // 1 MiB guard against a 64 MiB flood
  const auto session = acquireModel( model );

  std::vector<float> a = { 1.0f };
  std::vector<NamedTensor> inputs;
  inputs.push_back( NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } );

  bool typedGuard = false;
  try
  {
    session->inferNamed( inputs, {} );
    FAIL( "expected the max_body_mb guard to refuse the flood" );
  }
  catch ( const std::exception &e )
  {
    typedGuard = classifyInferenceError( e.what() ) == InferenceFailureKind::OutputInvalid
                   || std::string( e.what() ).find( "max_body_mb" ) != std::string::npos;
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::OutputInvalid );
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "max_body_mb" ) );
  }
  CHECK( typedGuard );

  // The provider must have hung up mid-transfer. Give the shared event loop
  // a moment to deliver the server-side disconnect, then require that far
  // less than the advertised body was flushed (a full 64 MiB flush is
  // exactly the buffering behavior this guard forbids).
  const QDeadlineTimer deadline( 5000 );
  while ( !server.floodCompleted() && !deadline.hasExpired() )
    QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
  CHECK( server.floodCompleted() );
  CHECK( server.floodFlushed() < kFloodBytes );
  CHECK( session->health().failures >= 1 );
}
