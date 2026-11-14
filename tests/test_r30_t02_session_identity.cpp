// test_r30_t02_session_identity.cpp — R30-T02 WP03/WP04: a cached session is
// keyed by EVERYTHING the factory pins into it, and an ad-hoc digest memo
// cannot be fooled by a same-length/same-mtime swap.
//   * same artifact bytes at TWO loopback endpoints with different scripted
//     answers: two sessions, each answering with ITS endpoint's result, and
//     re-acquisition reuses the session bound to that endpoint's contract;
//   * same endpoint, different timeout: distinct sessions;
//   * dead endpoint then live endpoint on the same bytes: the crashed
//     acquisition leaves no session the live one gets served;
//   * digest memo: same path, same LENGTH, mtime restored, different bytes —
//     a fresh digest must be computed (new session), while equal bytes at
//     different paths still share one session.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "operators/runtime/model_runtime.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <QNetworkAccessManager>

#include <opencv2/dnn.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace
{

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;

/// The provider stack (QNetworkAccessManager) needs a QCoreApplication for
/// event dispatch; Catch2 owns main, so create it lazily per test case.
QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_r30_t02_session_identity";
  static char *argv[] = { name, nullptr };
  static QCoreApplication *app = new QCoreApplication( argc, argv );
  return *app;
}

/// Loopback infer service: outputs = sum of every input tensor's last
/// element PLUS a per-server bias, so two endpoints over the same weights
/// give observably different answers (the identity oracle for "which
/// endpoint does this session talk to").
class BiasedLoopbackServer
{
  public:
    explicit BiasedLoopbackServer( float bias ) : m_bias( bias )
    {
      m_server = std::make_unique<QTcpServer>();
      m_server->listen( QHostAddress::LocalHost );
      m_port = m_server->serverPort();
      QObject::connect( m_server.get(), &QTcpServer::newConnection, [this] {
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
            return;
          const QByteArray body = buf.mid( headerEnd + 4, static_cast<int>( contentLength ) );
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
          QJsonObject out;
          out.insert( QStringLiteral( "name" ), QStringLiteral( "sum" ) );
          QJsonArray shape = { 1, 1, 1, 1 };
          out.insert( QStringLiteral( "shape" ), shape );
          out.insert( QStringLiteral( "dtype" ), QStringLiteral( "float32" ) );
          const float answer = static_cast<float>( total ) + m_bias;
          out.insert( QStringLiteral( "data_base64" ),
                      QString::fromLatin1( QByteArray( reinterpret_cast<const char *>( &answer ), 4 )
                                             .toBase64() ) );
          QJsonObject response;
          QJsonArray outputs;
          outputs.append( out );
          response.insert( QStringLiteral( "outputs" ), outputs );
          const QByteArray responseBody =
            QJsonDocument( response ).toJson( QJsonDocument::Compact );

          QByteArray http;
          http += "HTTP/1.1 200 OK\r\n";
          http += "Content-Type: application/json\r\n";
          http += "Content-Length: " + QByteArray::number( responseBody.size() ) + "\r\n";
          http += "Connection: close\r\n\r\n";
          http += responseBody;
          socket->write( http );
          socket->disconnectFromHost();
        } );
        QObject::connect( socket, &QTcpSocket::disconnected, socket,
                          [ socket ] { socket->deleteLater(); } );
      } );
    }

    ~BiasedLoopbackServer() { m_server->close(); }

    std::string url() const { return "http://127.0.0.1:" + std::to_string( m_port ) + "/infer"; }
    int requestsServed = 0;

  private:
    float m_bias = 0.0f;
    std::unique_ptr<QTcpServer> m_server;
    quint16 m_port = 0;
    QMap<QTcpSocket *, QByteArray> m_buffer;
};

/// One (1,1,2,2) float32 input whose last element is the payload.
cv::Mat scalarBlob( float v )
{
  cv::Mat hwc( 2, 2, CV_32FC1, cv::Scalar( 0.0f ) );
  hwc.at<float>( 1, 1 ) = v;
  return cv::dnn::blobFromImage( hwc );
}

float inferScalar( const ModelRuntimePtr &session, float v )
{
  const cv::Mat out = session->infer( scalarBlob( v ) );
  REQUIRE( out.total() >= 1 );
  return out.ptr<float>()[0];
}

/// A ready http model with a local artifact; bytes are caller-controlled so
/// two models can share ONE artifact file (the identity oracle's premise).
ModelInfo httpModel( const QString &artifact, const std::string &url, int timeoutMs = 5000 )
{
  ModelInfo model;
  model.name = "r30-http";
  model.framework = "http";
  model.readiness = ModelReadiness::Ready;
  model.resolvedArtifactPath = artifact.toStdString();
  model.runtime.provider.url = url;
  model.runtime.provider.timeoutMs = timeoutMs;
  return model;
}

} // namespace

// ---------------------------------------------------------------------------

TEST_CASE( "R30-T02: same bytes at two endpoints never share a session",
           "[models][r30t02][identity]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  // ONE artifact file, byte-identical for both models: any session mixup is
  // provably the key's fault, not the bytes'.
  const QString artifact = dir.filePath( QStringLiteral( "weights.bin" ) );
  {
    QFile f( artifact );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( 64, 'W' ) );
  }

  BiasedLoopbackServer serverA( 10.0f );
  BiasedLoopbackServer serverB( 20.0f );
  REQUIRE( serverA.url() != serverB.url() );

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();

  const ModelInfo modelA = httpModel( artifact, serverA.url() );
  const ModelInfo modelB = httpModel( artifact, serverB.url() );

  std::string error;
  const auto sessionA1 = registry.acquire( modelA, &error );
  REQUIRE( sessionA1 );
  const auto sessionB = registry.acquire( modelB, &error );
  REQUIRE( sessionB );
  CHECK( sessionA1.get() != sessionB.get() );

  // Each session answers with ITS endpoint's bias (input last element = 1).
  CHECK( inferScalar( sessionA1, 1.0f ) == Catch::Approx( 11.0f ) );
  CHECK( inferScalar( sessionB, 1.0f ) == Catch::Approx( 21.0f ) );

  // Re-acquisition of A reuses A's session (same loading contract) and still
  // talks to A — not to whichever endpoint ran last.
  const auto sessionA2 = registry.acquire( modelA, &error );
  REQUIRE( sessionA2 );
  CHECK( sessionA2.get() == sessionA1.get() );
  CHECK( inferScalar( sessionA2, 1.0f ) == Catch::Approx( 11.0f ) );
  CHECK( serverA.requestsServed == 2 );
  CHECK( serverB.requestsServed == 1 );
}

TEST_CASE( "R30-T02: a different timeout is a different loading contract",
           "[models][r30t02][identity]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString artifact = dir.filePath( QStringLiteral( "weights.bin" ) );
  {
    QFile f( artifact );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( 64, 'W' ) );
  }

  BiasedLoopbackServer server( 0.0f );
  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();

  const ModelInfo fast = httpModel( artifact, server.url(), 5000 );
  const ModelInfo slow = httpModel( artifact, server.url(), 60000 );

  std::string error;
  const auto sessionFast = registry.acquire( fast, &error );
  REQUIRE( sessionFast );
  const auto sessionSlow = registry.acquire( slow, &error );
  REQUIRE( sessionSlow );
  CHECK( sessionFast.get() != sessionSlow.get() );
}

TEST_CASE( "R30-T02: a dead endpoint leaves no session the live one reuses",
           "[models][r30t02][identity]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString artifact = dir.filePath( QStringLiteral( "weights.bin" ) );
  {
    QFile f( artifact );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( 64, 'W' ) );
  }

  // Port with no listener: the transport refuses to connect.
  QTcpServer probe;
  probe.listen( QHostAddress::LocalHost );
  const quint16 deadPort = probe.serverPort();
  probe.close();

  BiasedLoopbackServer server( 0.0f );

  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();

  const ModelInfo dead = httpModel( artifact, "http://127.0.0.1:" + std::to_string( deadPort ) + "/infer" );
  std::string error;
  const auto deadSession = registry.acquire( dead, &error );
  // Acquisition may succeed (the session is lazy); the forward must fail.
  if ( deadSession )
    CHECK_THROWS( deadSession->infer( scalarBlob( 1.0f ) ) );

  const ModelInfo live = httpModel( artifact, server.url() );
  const auto liveSession = registry.acquire( live, &error );
  REQUIRE( liveSession );
  CHECK( inferScalar( liveSession, 2.0f ) == Catch::Approx( 2.0f ) );
}

// ---------------------------------------------------------------------------

namespace
{

/// Counting fake provider: session identity is observable through loadCount.
struct FakeLoadCounter
{
  std::atomic<int> loads{ 0 };
};

std::shared_ptr<FakeLoadCounter> g_counter;

} // namespace

TEST_CASE( "R30-T02: same-length same-mtime swap defeats the digest memo's "
           "staleness window",
           "[models][r30t02][identity]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString artifact = dir.filePath( QStringLiteral( "weights.bin" ) );

  const QDateTime originalStamp = QDateTime::currentDateTime().addSecs( -3600 );
  const auto writeWeights = [ & ]( const QByteArray &bytes ) {
    QFile f( artifact );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( bytes );
    f.close();
    // Restore the ORIGINAL mtime: the memo's (path, size, mtime) key now
    // matches the previous state exactly — only the CONTENT probe (WP04)
    // can tell the states apart.
    QFile stamp( artifact );
    REQUIRE( stamp.open( QIODevice::ReadWrite ) );
    REQUIRE( stamp.setFileTime( originalStamp, QFileDevice::FileModificationTime ) );
    stamp.close();
  };

  QByteArray oldBytes( 4096, 'A' );
  QByteArray newBytes( 4096, 'B' );
  newBytes[0] = 'Z'; // the sampled head differs — the probe must catch it
  writeWeights( oldBytes );

  auto counter = std::make_shared<FakeLoadCounter>();
  g_counter = counter;
  ModelRuntimeRegistry::instance().releaseAll();
  ModelRuntimeRegistry::instance().registerProvider(
    "r30-counter",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string *errorMessage ) {
      if ( !g_counter )
      {
        if ( errorMessage )
          *errorMessage = "no counter installed";
        return ModelRuntimePtr{};
      }
      g_counter->loads += 1;
      struct CounterRuntime final : public IModelRuntime
      {
        std::string framework() const override { return "r30-counter"; }
        std::string backendName() const override { return "counter"; }
        std::string deviceName() const override { return "cpu"; }
        std::string artifactPath() const override { return "fake://counter"; }
        cv::Mat infer( const cv::Mat &nchw ) override { return nchw.clone(); }
      };
      return ModelRuntimePtr( std::make_shared<CounterRuntime>() );
    },
    ProviderTraits{} );

  const auto model = [ & ] {
    ModelInfo m;
    m.name = "r30-counter-model";
    m.framework = "r30-counter";
    m.readiness = ModelReadiness::Ready;
    m.resolvedArtifactPath = artifact.toStdString();
    return m;
  }();

  std::string error;
  const auto first = ModelRuntimeRegistry::instance().acquire( model, &error );
  REQUIRE( first );
  REQUIRE( counter->loads.load() == 1 );

  // Same length, same mtime, DIFFERENT bytes: the sampled-content probe must
  // invalidate the memo → a new digest → a new session (load #2).
  writeWeights( newBytes );
  const auto second = ModelRuntimeRegistry::instance().acquire( model, &error );
  REQUIRE( second );
  CHECK( second.get() != first.get() );
  CHECK( counter->loads.load() == 2 );

  // Unchanged state (same bytes, same mtime): memo hit, no reload.
  const auto third = ModelRuntimeRegistry::instance().acquire( model, &error );
  REQUIRE( third );
  CHECK( third.get() == second.get() );
  CHECK( counter->loads.load() == 2 );

  ModelRuntimeRegistry::instance().releaseAll();
  g_counter.reset();
}

TEST_CASE( "R30-T02: equal bytes at different paths still share one session",
           "[models][r30t02][identity]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString copyA = dir.filePath( QStringLiteral( "a.onnx" ) );
  const QString copyB = dir.filePath( QStringLiteral( "b.onnx" ) );
  {
    QFile f( copyA );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( 4096, 'Q' ) );
  }
  REQUIRE( QFile::copy( copyA, copyB ) );

  auto counter = std::make_shared<FakeLoadCounter>();
  g_counter = counter;
  auto &registry = ModelRuntimeRegistry::instance();
  registry.releaseAll();
  // The provider from the previous case may not be registered in this
  // process anymore (per-case processes) — install it again.
  registry.registerProvider(
    "r30-counter",
    []( const ModelInfo &, const ModelHardwareCapabilities &, std::string *errorMessage ) {
      if ( !g_counter )
      {
        if ( errorMessage )
          *errorMessage = "no counter installed";
        return ModelRuntimePtr{};
      }
      g_counter->loads += 1;
      struct CounterRuntime final : public IModelRuntime
      {
        std::string framework() const override { return "r30-counter"; }
        std::string backendName() const override { return "counter"; }
        std::string deviceName() const override { return "cpu"; }
        std::string artifactPath() const override { return "fake://counter"; }
        cv::Mat infer( const cv::Mat &nchw ) override { return nchw.clone(); }
      };
      return ModelRuntimePtr( std::make_shared<CounterRuntime>() );
    },
    ProviderTraits{} );

  const auto modelAt = [ & ]( const QString &path ) {
    ModelInfo m;
    m.name = "r30-counter-model";
    m.framework = "r30-counter";
    m.readiness = ModelReadiness::Ready;
    m.resolvedArtifactPath = path.toStdString();
    return m;
  };

  std::string error;
  const auto sessionA = registry.acquire( modelAt( copyA ), &error );
  REQUIRE( sessionA );
  const auto sessionB = registry.acquire( modelAt( copyB ), &error );
  REQUIRE( sessionB );
  CHECK( sessionA.get() == sessionB.get() );
  CHECK( counter->loads.load() == 1 );

  registry.releaseAll();
  g_counter.reset();
}
