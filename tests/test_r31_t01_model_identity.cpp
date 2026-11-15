#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/model_runtime.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>

#include <opencv2/dnn.hpp>

namespace
{

using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;
using sicnu::operators::runtime::ModelRuntimePtr;
using sicnu::operators::runtime::ModelRuntimeRegistry;

QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_r31_t01_model_identity";
  static char *argv[] = { name, nullptr };
  static QCoreApplication *app = new QCoreApplication( argc, argv );
  return *app;
}

class BiasedLoopbackServer
{
  public:
    explicit BiasedLoopbackServer( float bias ) : m_bias( bias )
    {
      m_server = std::make_unique<QTcpServer>();
      REQUIRE( m_server->listen( QHostAddress::LocalHost ) );
      m_port = m_server->serverPort();
      QObject::connect( m_server.get(), &QTcpServer::newConnection, [this] {
        QTcpSocket *socket = m_server->nextPendingConnection();
        QObject::connect( socket, &QTcpSocket::readyRead, [this, socket] {
          m_buffer[socket] += socket->readAll();
          const QByteArray &buffer = m_buffer[socket];
          const int headerEnd = buffer.indexOf( "\r\n\r\n" );
          if ( headerEnd < 0 )
            return;
          qint64 contentLength = 0;
          for ( const QByteArray &line : buffer.split( '\n' ) )
          {
            const QByteArray lowered = line.trimmed().toLower();
            if ( lowered.startsWith( "content-length:" ) )
              contentLength = lowered.mid( 15 ).trimmed().toLongLong();
          }
          if ( buffer.size() - headerEnd - 4 < contentLength )
            return;
          const QByteArray body = buffer.mid( headerEnd + 4, static_cast<int>( contentLength ) );
          m_buffer.remove( socket );
          const QJsonObject request = QJsonDocument::fromJson( body ).object();
          double total = 0.0;
          for ( const auto &entry : request.value( QStringLiteral( "inputs" ) ).toArray() )
          {
            const QJsonObject tensor = entry.toObject();
            const QByteArray raw = QByteArray::fromBase64(
              tensor.value( QStringLiteral( "data_base64" ) ).toString().toLatin1() );
            if ( tensor.value( QStringLiteral( "dtype" ) ).toString()
                   == QStringLiteral( "float32" )
                 && raw.size() >= 4 )
              total += *reinterpret_cast<const float *>( raw.constData() + raw.size() - 4 );
          }
          const float answer = static_cast<float>( total ) + m_bias;
          QJsonObject out;
          out.insert( QStringLiteral( "name" ), QStringLiteral( "sum" ) );
          out.insert( QStringLiteral( "shape" ), QJsonArray{ 1, 1, 1, 1 } );
          out.insert( QStringLiteral( "dtype" ), QStringLiteral( "float32" ) );
          out.insert( QStringLiteral( "data_base64" ),
                      QString::fromLatin1( QByteArray( reinterpret_cast<const char *>( &answer ), 4 )
                                             .toBase64() ) );
          QJsonObject response;
          response.insert( QStringLiteral( "outputs" ), QJsonArray{ out } );
          const QByteArray responseBody = QJsonDocument( response ).toJson( QJsonDocument::Compact );
          QByteArray http = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
          http += "Content-Length: " + QByteArray::number( responseBody.size() ) + "\r\n";
          http += "Connection: close\r\n\r\n" + responseBody;
          socket->write( http );
          socket->disconnectFromHost();
        } );
        QObject::connect( socket, &QTcpSocket::disconnected, socket,
                          [socket] { socket->deleteLater(); } );
      } );
    }

    ~BiasedLoopbackServer() { m_server->close(); }

    std::string url() const
    {
      return "http://127.0.0.1:" + std::to_string( m_port ) + "/infer";
    }

  private:
    float m_bias = 0.0f;
    std::unique_ptr<QTcpServer> m_server;
    quint16 m_port = 0;
    QMap<QTcpSocket *, QByteArray> m_buffer;
};

ModelInfo httpModel( const std::string &url, const std::string &identity )
{
  ModelInfo model;
  model.name = "r31-http";
  model.framework = "http";
  model.readiness = ModelReadiness::Ready;
  model.contentDigest = identity.substr( 0, 64 );
  model.packageDigest = identity.substr( 64 + 5 );
  model.runtime.provider.url = url;
  model.runtime.provider.timeoutMs = 5000;
  model.runtime.provider.maxBodyMb = 8;
  return model;
}

cv::Mat scalarBlob( float value )
{
  cv::Mat hwc( 2, 2, CV_32FC1, cv::Scalar( 0.0f ) );
  hwc.at<float>( 1, 1 ) = value;
  return cv::dnn::blobFromImage( hwc );
}

float inferScalar( const ModelRuntimePtr &session, float value )
{
  const cv::Mat out = session->infer( scalarBlob( value ) );
  REQUIRE( out.total() >= 1 );
  return out.ptr<float>()[0];
}

} // namespace

TEST_CASE( "R31-T01 WP15: directed unload understands package and load identities",
           "[r31][t01][model_identity][release]" )
{
  ensureApp();
  const std::string identity( 64, 'a' );
  const std::string package( 64, 'b' );
  const std::string artifactIdentity = identity + "|pkg:" + package;
  BiasedLoopbackServer serverA( 10.0f );
  BiasedLoopbackServer serverB( 20.0f );
  // Same endpoint, same limits, DIFFERENT credentials in the userinfo: the
  // loading-contract identity strips userinfo (the ledger must never see
  // credentials), so this is the same contract as serverB.url() — the two
  // sessions stay distinct through their content/package identity alone.
  const std::string credentialUrl =
    "http://user:secret@" + serverA.url().substr( std::string( "http://" ).size() );
  const ModelInfo modelA = httpModel( credentialUrl, artifactIdentity );
  const ModelInfo modelB = httpModel( serverB.url(), artifactIdentity );
  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();

  std::string error;
  const auto sessionA = registry.acquire( modelA, &error );
  const auto sessionB = registry.acquire( modelB, &error );
  REQUIRE( sessionA );
  REQUIRE( sessionB );
  CHECK( sessionA.get() != sessionB.get() );
  CHECK( registry.cachedSessionCount() == 2 );

  registry.release( "http", artifactIdentity );
  CHECK( registry.cachedSessionCount() == 0 );
  CHECK( inferScalar( sessionA, 1.0f ) == Catch::Approx( 11.0f ) );
  CHECK( inferScalar( sessionB, 1.0f ) == Catch::Approx( 21.0f ) );

  const auto reloadA = registry.acquire( modelA, &error );
  const auto reloadB = registry.acquire( modelB, &error );
  REQUIRE( reloadA );
  REQUIRE( reloadB );
  CHECK( reloadA.get() != sessionA.get() );
  CHECK( reloadB.get() != sessionB.get() );
  CHECK( registry.cachedSessionCount() == 2 );

  const std::string loadIdentityA =
    artifactIdentity + "|load:" + serverA.url() + "|t:5000|b:8";
  registry.release( "http", loadIdentityA );
  CHECK( registry.cachedSessionCount() == 1 );
  const auto directedReloadA = registry.acquire( modelA, &error );
  const auto stillB = registry.acquire( modelB, &error );
  CHECK( directedReloadA.get() != reloadA.get() );
  CHECK( stillB.get() == reloadB.get() );

  registry.release( "http", artifactIdentity );
  CHECK( registry.cachedSessionCount() == 0 );
}
