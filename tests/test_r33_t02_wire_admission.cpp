// tests/test_r33_t02_wire_admission.cpp — R33-T02 WP09/WP10: the provider
// wire contract distinguishes OMITTED fields (pre-versioned compat) from
// WRONG-TYPED ones (typed refusal), refuses duplicate named heads, and
// admits output payloads BEFORE the base64 decode allocates (rank bound,
// canonical length consistency, unrepresentable byte counts). Verified at
// three layers: the shared codec directly, a real HTTP loopback consumer
// through the ModelRuntimeRegistry seam, and a real out-of-process Python
// worker through the QProcess pipe protocol.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "operators/framework/model_catalog.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/provider_wire.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>

#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QFile>
#include <QFileInfo>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "support/offline_probe.h"
#include "support/qt_lifecycle.h"

// Track 2 R4: ordered teardown via the shared listener (same contract as
// test_provider_http — QCoreApplication is deleted before glibc exit()).
CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace {

using namespace sicnu::operators::runtime;
using sicnu::operators::ModelInfo;
using sicnu::operators::ModelReadiness;

QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_r33_t02_wire_admission";
  static char *argv[] = { name, nullptr };
  static QCoreApplication *app = new QCoreApplication( argc, argv );
  return *app;
}

QJsonObject tensorObject( const std::string &name, const QJsonArray &shape,
                          const std::string &dtype, const QByteArray &raw )
{
  QJsonObject obj;
  obj.insert( QStringLiteral( "name" ), QString::fromStdString( name ) );
  obj.insert( QStringLiteral( "shape" ), shape );
  obj.insert( QStringLiteral( "dtype" ), QString::fromStdString( dtype ) );
  obj.insert( QStringLiteral( "data_base64" ),
              QString::fromLatin1( raw.toBase64() ) );
  return obj;
}

QByteArray float32Bytes( float value )
{
  QByteArray bytes( 4, Qt::Uninitialized );
  std::memcpy( bytes.data(), &value, 4 );
  return bytes;
}

} // namespace

// ---------------------------------------------------------------------------
// Layer 1: the shared codec, isolated.
// ---------------------------------------------------------------------------

TEST_CASE( "wire protocol typing distinguishes omitted from mistyped", "[models][wire][r33t02]" )
{
  QJsonObject ok;
  CHECK_NOTHROW( checkWireProtocol( ok ) ); // absent → pre-versioned

  QJsonObject nullProtocol;
  nullProtocol.insert( QStringLiteral( "protocol" ), QJsonValue() );
  CHECK_NOTHROW( checkWireProtocol( nullProtocol ) ); // explicit null → pre-versioned

  QJsonObject legacy;
  legacy.insert( QStringLiteral( "protocol" ), QString() );
  CHECK_NOTHROW( checkWireProtocol( legacy ) ); // legacy empty token

  QJsonObject current;
  current.insert( QStringLiteral( "protocol" ),
                  QString::fromLatin1( kProviderWireProtocol ) );
  CHECK_NOTHROW( checkWireProtocol( current ) );

  // A PRESENT field of the wrong type used to collapse to "" and silently
  // read as pre-versioned. Each must refuse typed now.
  const auto mistyped = []( QJsonValue value ) {
    QJsonObject doc;
    doc.insert( QStringLiteral( "protocol" ), value );
    try
    {
      checkWireProtocol( doc );
      return std::string();
    }
    catch ( const std::exception &e )
    {
      return std::string( e.what() );
    }
  };
  for ( const std::string &what :
        { mistyped( QJsonValue( 2 ) ), mistyped( QJsonValue( false ) ),
          mistyped( QJsonValue( 3.5 ) ),
          mistyped( QJsonValue( QJsonObject{ { "v", 1 } } ) ),
          mistyped( QJsonValue( QJsonArray{ 1, 2 } ) ) } )
  {
    INFO( "message: " << what );
    REQUIRE_FALSE( what.empty() );
    CHECK( what.find( "wire protocol" ) != std::string::npos );
    CHECK( classifyInferenceError( what ) == InferenceFailureKind::IncompatibleSchema );
  }
}

TEST_CASE( "wire tensor decode refuses impossible payloads before allocating",
           "[models][wire][r33t02]" )
{
  // Canonical round-trip first: a real float32 2x2 with real values.
  const float values[ 4 ] = { 1.0f, -2.5f, 3.25f, 4.0f };
  QByteArray raw( reinterpret_cast<const char *>( values ), 16 );
  const QJsonObject good = tensorObject( "head", QJsonArray { 1, 1, 2, 2 }, "float32", raw );
  const TensorBlob blob = decodeWireTensor( good, "test" );
  REQUIRE( blob.shape == std::vector<std::int64_t>( { 1, 1, 2, 2 } ) );
  CHECK( blob.dataFloat32()[ 3 ] == Catch::Approx( 4.0f ) );

  // Rank bound fires before the payload is even considered.
  QJsonObject overRanked = tensorObject( "head", QJsonArray { 1, 1, 1, 1, 1, 1, 1 },
                                         "float32", raw );
  REQUIRE_THROWS_WITH( decodeWireTensor( overRanked, "test" ),
                       Catch::Matchers::ContainsSubstring( "maximum rank" ) );

  // A 2 KB base64 payload against a 16-byte declaration: the alphabet-count
  // admission refuses BEFORE QByteArray::fromBase64 allocates the buffer.
  QByteArray hugePayload( 2048, 'A' ); // 2048 alphabet chars, needs 4*ceil(16/3)=24
  QJsonObject mismatch = tensorObject( "head", QJsonArray { 1, 1, 2, 2 }, "float32",
                                       QByteArray() );
  mismatch.insert( QStringLiteral( "data_base64" ), QString::fromLatin1( hugePayload ) );
  REQUIRE_THROWS_WITH( decodeWireTensor( mismatch, "test" ),
                       Catch::Matchers::ContainsSubstring( "declared shape" ) );

  // Non-string payload (number) — typed refusal, not silent "".
  QJsonObject numericPayload = tensorObject( "head", QJsonArray { 1 }, "float32", raw );
  numericPayload.insert( QStringLiteral( "data_base64" ), QJsonValue( 12345 ) );
  REQUIRE_THROWS_WITH( decodeWireTensor( numericPayload, "test" ),
                       Catch::Matchers::ContainsSubstring( "declared shape" ) );

  // Unrepresentable byte volume: shape product * dtype size overflows size_t
  // (each dim fits int64; the PRODUCT does not fit the byte domain).
  QJsonObject huge = tensorObject( "head",
                                   QJsonArray { static_cast<qint64>( 3037000500 ),
                                                static_cast<qint64>( 3037000500 ) },
                                   "float64", QByteArray() );
  REQUIRE_THROWS_WITH( decodeWireTensor( huge, "test" ),
                       Catch::Matchers::ContainsSubstring( "admissible range" ) );

  // Int64/Float64 real values still round-trip (dtypes the wire carries).
  const std::int64_t int64Value = -1234567890123456789LL;
  const QJsonObject int64Tensor =
    tensorObject( "i64", QJsonArray { 2 },
                  "int64",
                  QByteArray( reinterpret_cast<const char *>( &int64Value ), 8 )
                    + QByteArray( 8, '\0' ) );
  const TensorBlob decoded64 = decodeWireTensor( int64Tensor, "test" );
  REQUIRE( decoded64.bytes.size() == 16 );
  std::int64_t roundTripped = 0;
  std::memcpy( &roundTripped, decoded64.bytes.data(), 8 );
  CHECK( roundTripped == int64Value );
}

TEST_CASE( "wire tensor decode keeps Qt's unpadded and wrapped base64 compat",
           "[models][wire][r33t02]" )
{
  // Qt's lenient QByteArray::fromBase64 decodes unpadded and whitespace-
  // wrapped payloads to the exact declared byte count; the admission must
  // not turn those legal responders into refusals (verified against the
  // local Qt6 behavior before this gate was written).
  const float values[ 4 ] = { 1.5f, -2.5f, 3.25f, 4.0f };
  const QByteArray raw( reinterpret_cast<const char *>( values ), 16 );
  const QByteArray canonical = raw.toBase64(); // 24 chars, '=='-padded

  QByteArray unpadded = canonical;
  while ( unpadded.endsWith( '=' ) )
    unpadded.chop( 1 );
  REQUIRE( unpadded.size() == 22 );

  QByteArray wrapped = canonical;
  wrapped.insert( 12, "\r\n" );

  const auto decodeRoundTrip = [ & ]( const QByteArray &payload ) {
    QJsonObject obj;
    obj.insert( QStringLiteral( "name" ), QStringLiteral( "head" ) );
    obj.insert( QStringLiteral( "shape" ), QJsonArray { 1, 1, 2, 2 } );
    obj.insert( QStringLiteral( "dtype" ), QStringLiteral( "float32" ) );
    obj.insert( QStringLiteral( "data_base64" ), QString::fromLatin1( payload ) );
    const TensorBlob blob = decodeWireTensor( obj, "test" );
    REQUIRE( blob.bytes.size() == 16 );
    return blob.dataFloat32()[ 3 ];
  };
  CHECK( decodeRoundTrip( canonical ) == Catch::Approx( 4.0f ) );
  CHECK( decodeRoundTrip( unpadded ) == Catch::Approx( 4.0f ) );
  CHECK( decodeRoundTrip( wrapped ) == Catch::Approx( 4.0f ) );

  // Canonical acceptance for every padding class: 3 bytes (no padding),
  // 2 bytes (one '='), 1 byte (two '='). Without these, the pad==1/pad==2
  // geometry branches would only ever be reached through refusals.
  const auto decodeUint8 = []( const QByteArray &base64Text, int dim ) {
    QJsonObject obj;
    obj.insert( QStringLiteral( "name" ), QStringLiteral( "head" ) );
    obj.insert( QStringLiteral( "shape" ), QJsonArray { dim } );
    obj.insert( QStringLiteral( "dtype" ), QStringLiteral( "uint8" ) );
    obj.insert( QStringLiteral( "data_base64" ), QString::fromLatin1( base64Text ) );
    return decodeWireTensor( obj, "test" );
  };
  const TensorBlob threeBytes = decodeUint8( QByteArray( "AQID" ), 3 );
  REQUIRE( threeBytes.bytes.size() == 3 );
  CHECK( threeBytes.bytes[ 1 ] == 0x02 );
  const TensorBlob twoBytes = decodeUint8( QByteArray( "AQI=" ), 2 );
  REQUIRE( twoBytes.bytes.size() == 2 );
  const TensorBlob oneByte = decodeUint8( QByteArray( "AQ==" ), 1 );
  REQUIRE( oneByte.bytes.size() == 1 );
  // Same tensors, padding omitted (still canonical-equivalent).
  CHECK( decodeUint8( QByteArray( "AQID" ), 3 ).bytes.size() == 3 );
  CHECK( decodeUint8( QByteArray( "AQI" ), 2 ).bytes.size() == 2 );
  CHECK( decodeUint8( QByteArray( "AQ" ), 1 ).bytes.size() == 1 );

  // The gate is deliberately STRICTER than Qt's lenient decoder: a payload
  // with a surplus data character (or surplus '=') can still decode to the
  // declared byte count under accumulate-and-discard-tail, and is refused
  // here. Pinned so the strictness stays intentional: no conforming encoder
  // produces these forms, and the refusal happens before any allocation.
  // (The payload is placed as raw base64 TEXT — tensorObject would re-encode
  // the bytes back to canonical form.)
  const auto rawBase64Object = []( const QByteArray &base64Text ) {
    QJsonObject obj;
    obj.insert( QStringLiteral( "name" ), QStringLiteral( "head" ) );
    obj.insert( QStringLiteral( "shape" ), QJsonArray { 3 } );
    obj.insert( QStringLiteral( "dtype" ), QStringLiteral( "uint8" ) );
    obj.insert( QStringLiteral( "data_base64" ), QString::fromLatin1( base64Text ) );
    return obj;
  };
  REQUIRE_THROWS_WITH(
    decodeWireTensor( rawBase64Object( QByteArray( "jCK9A" ) ), "test" ),
    Catch::Matchers::ContainsSubstring( "declared shape" ) );
  REQUIRE_THROWS_WITH(
    decodeWireTensor( rawBase64Object( QByteArray( "jCK9=" ) ), "test" ),
    Catch::Matchers::ContainsSubstring( "declared shape" ) );
}

TEST_CASE( "wire outputs reject mistyped and duplicate head names",
           "[models][wire][r33t02]" )
{
  const auto outputs = []( const std::vector<QJsonObject> &tensors ) {
    QJsonArray array;
    for ( const QJsonObject &t : tensors )
      array.append( t );
    return array;
  };
  const QJsonObject good = tensorObject( "sum", QJsonArray { 1, 1, 1, 1 }, "float32",
                                         float32Bytes( 8.0f ) );

  // Two entries answering to the same NON-empty name = ambiguous head.
  REQUIRE_THROWS_WITH( decodeInferOutputs( outputs( { good, good } ) ),
                       Catch::Matchers::ContainsSubstring( "more than once" ) );

  // Repeated '' stays legal (positional responders with unnamed heads).
  QJsonObject unnamedA = good;
  unnamedA.remove( QStringLiteral( "name" ) );
  QJsonObject unnamedB = good;
  unnamedB.remove( QStringLiteral( "name" ) );
  REQUIRE( decodeInferOutputs( outputs( { unnamedA, unnamedB } ) ).size() == 2 );

  // An explicit null name is the same "no name" spelling checkWireProtocol
  // accepts for `protocol`: it binds as the unnamed head, not a type error.
  QJsonObject nullName = good;
  nullName.insert( QStringLiteral( "name" ), QJsonValue() );
  REQUIRE( decodeInferOutputs( outputs( { nullName } ) ).size() == 1 );
  CHECK( decodeInferOutputs( outputs( { nullName } ) ).front().first.empty() );

  // A present-but-mistyped name (number) used to collapse to ''.
  QJsonObject numericName = good;
  numericName.insert( QStringLiteral( "name" ), QJsonValue( 7 ) );
  REQUIRE_THROWS_WITH( decodeInferOutputs( outputs( { numericName } ) ),
                       Catch::Matchers::ContainsSubstring( "non-string 'name'" ) );

  // A mistyped name in a MULTI-entry answer is refused too (the first entry's
  // validity does not launder a later entry's type error).
  REQUIRE_THROWS_WITH( decodeInferOutputs( outputs( { good, numericName } ) ),
                       Catch::Matchers::ContainsSubstring( "non-string 'name'" ) );
}

// ---------------------------------------------------------------------------
// Layer 2: real HTTP consumer through the registry seam.
// ---------------------------------------------------------------------------

namespace {

/// Minimal loopback server answering every /infer POST with a CANNED JSON
/// body (switchable per request) so failure modes need no real service.
class CannedResponseServer
{
  public:
    void setBodies( const std::vector<QString> &bodies ) { m_bodies = bodies; }

    quint16 port() const { return m_port; }

    explicit CannedResponseServer()
    {
      m_server = std::make_unique<QTcpServer>();
      m_server->listen( QHostAddress::LocalHost );
      m_port = m_server->serverPort();
      QObject::connect( m_server.get(), &QTcpServer::newConnection, [this] {
        QTcpSocket *socket = m_server->nextPendingConnection();
        QObject::connect( socket, &QTcpSocket::readyRead, socket, [this, socket] {
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
            return; // body not complete yet — answer only a full request
          m_buffer.remove( socket );
          const QString canned = m_bodies.empty() ? QString() : m_bodies.front();
          if ( !m_bodies.empty() )
            m_bodies.erase( m_bodies.begin() );
          const QByteArray responseBody = canned.toUtf8();
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
                          [socket] { socket->deleteLater(); } );
      } );
    }

    ~CannedResponseServer() { m_server->close(); }

  private:
    std::unique_ptr<QTcpServer> m_server;
    quint16 m_port = 0;
    QMap<QTcpSocket *, QByteArray> m_buffer;
    std::vector<QString> m_bodies;
};

ModelInfo makeHttpModel( const QTemporaryDir &dir, const std::string &endpoint,
                         const std::string &tag )
{
  const QString weights = dir.filePath( QStringLiteral( "weights.bin" ) );
  QFile file( weights );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  file.write( QByteArray( "r33-t02-wire:" ) + QByteArray::fromStdString( tag ) );
  file.close();

  ModelInfo model;
  model.name = "m-r33-t02-wire";
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

std::vector<NamedTensor> oneInput()
{
  std::vector<float> a = { 1.0f };
  return { NamedTensor{ "x", TensorBlob::fromFloat32( { 1, 1, 1, 1 }, a.data(), 1 ) } };
}

// Raw-string delimiter `json` (not the default ")") because the payload text
// itself contains `)"` sequences that would terminate a plain raw literal.
const QString kGoodBody = QStringLiteral( R"json({"outputs":[{"name":"sum","shape":[1,1,1,1],"dtype":"float32","data_base64":")json" )
  + QString::fromLatin1( float32Bytes( 8.0f ).toBase64() )
  + QStringLiteral( R"json("}]})json" );

} // namespace

SICNU_OFFLINE_GUARD()

TEST_CASE( "http consumer refuses a mistyped protocol then recovers (real transport)",
           "[models][http][r33t02]" )
{
  ( void )ensureApp();
  CannedResponseServer server;
  server.setBodies( {
    QStringLiteral( R"json({"protocol":2,"outputs":[{"name":"sum","shape":[1,1,1,1],"dtype":"float32","data_base64":")json" )
      + QString::fromLatin1( float32Bytes( 8.0f ).toBase64() )
      + QStringLiteral( R"json("}]})json" ),
    kGoodBody,
  } );
  QTemporaryDir dir;
  const auto session = acquireModel(
    makeHttpModel( dir, "http://127.0.0.1:" + std::to_string( server.port() ) + "/infer", "p" ) );

  try
  {
    session->inferNamed( oneInput(), {} );
    FAIL( "expected the mistyped protocol to be refused" );
  }
  catch ( const std::exception &e )
  {
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "wire protocol" ) );
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::IncompatibleSchema );
  }
  CHECK( session->health().failures == 1 );

  // The session survives; the next (well-formed) answer is served.
  const auto outputs = session->inferNamed( oneInput(), {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs.front().second.dataFloat32()[ 0 ] == Catch::Approx( 8.0f ) );
  CHECK( session->health().forwardsCompleted == 1 );
}

TEST_CASE( "http consumer refuses duplicate named heads then recovers (real transport)",
           "[models][http][r33t02]" )
{
  ( void )ensureApp();
  const QString head = QStringLiteral( R"json({"name":"sum","shape":[1,1,1,1],"dtype":"float32","data_base64":")json" )
    + QString::fromLatin1( float32Bytes( 8.0f ).toBase64() ) + QStringLiteral( R"json("})json" );
  const QString duplicateBody = QStringLiteral( R"json({"outputs":[)json" ) + head + QLatin1Char( ',' )
                                + head + QStringLiteral( R"json(]})json" );
  CannedResponseServer server;
  server.setBodies( { duplicateBody, kGoodBody } );
  QTemporaryDir dir;
  const auto session = acquireModel(
    makeHttpModel( dir, "http://127.0.0.1:" + std::to_string( server.port() ) + "/infer", "d" ) );

  try
  {
    session->inferNamed( oneInput(), {} );
    FAIL( "expected the duplicate head answer to be refused" );
  }
  catch ( const std::exception &e )
  {
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "more than once" ) );
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::OutputInvalid );
  }
  CHECK( session->health().failures == 1 );

  const auto outputs = session->inferNamed( oneInput(), {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs.front().second.dataFloat32()[ 0 ] == Catch::Approx( 8.0f ) );
}

TEST_CASE( "http consumer refuses a base64/shape mismatch before decoding (real transport)",
           "[models][http][r33t02]" )
{
  ( void )ensureApp();
  // Declares 1x1x1x1 float32 (16 bytes) but ships a ~100 KB payload; the old
  // path decoded the whole buffer first, the admission now refuses on the
  // length inconsistency alone.
  const QByteArray big( 100 * 1024, 'A' );
  const QString mismatchBody = QStringLiteral(
    R"({"outputs":[{"name":"sum","shape":[1,1,1,1],"dtype":"float32","data_base64":")" )
    + QString::fromLatin1( big ) + QStringLiteral( R"("}]})" );
  CannedResponseServer server;
  server.setBodies( { mismatchBody, kGoodBody } );
  QTemporaryDir dir;
  const auto session = acquireModel(
    makeHttpModel( dir, "http://127.0.0.1:" + std::to_string( server.port() ) + "/infer", "m" ) );

  try
  {
    session->inferNamed( oneInput(), {} );
    FAIL( "expected the length-inconsistent payload to be refused" );
  }
  catch ( const std::exception &e )
  {
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "declared shape" ) );
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::ShapeMismatch );
  }
  CHECK( session->health().failures == 1 );

  const auto outputs = session->inferNamed( oneInput(), {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs.front().second.dataFloat32()[ 0 ] == Catch::Approx( 8.0f ) );
}

// ---------------------------------------------------------------------------
// Layer 3: real Python worker consumer through the QProcess pipe protocol.
// ---------------------------------------------------------------------------

namespace {

/// Writes @p script as `worker.py` next to a dummy manifest in @p dir and
/// returns the model whose sourceManifest anchors it (the worker-script
/// containment rule requires the script inside the manifest directory).
ModelInfo makeWorkerModel( const QTemporaryDir &dir, const QString &script,
                           const std::string &tag )
{
  const QString manifest = dir.filePath( QStringLiteral( "manifest.json" ) );
  QFile manifestFile( manifest );
  REQUIRE( manifestFile.open( QIODevice::WriteOnly ) );
  manifestFile.write( QByteArray( R"({"name":")" ) + QByteArray::fromStdString( tag )
                      + QByteArray( R"("})" ) );
  manifestFile.close();

  const QString worker = dir.filePath( QStringLiteral( "worker.py" ) );
  QFile workerFile( worker );
  REQUIRE( workerFile.open( QIODevice::WriteOnly ) );
  workerFile.write( script.toUtf8() );
  workerFile.close();

  const QString weights = dir.filePath( QStringLiteral( "weights.bin" ) );
  QFile weightsFile( weights );
  REQUIRE( weightsFile.open( QIODevice::WriteOnly ) );
  weightsFile.write( QByteArray( "r33-t02-worker:" ) + QByteArray::fromStdString( tag ) );
  weightsFile.close();

  ModelInfo model;
  model.name = "m-r33-t02-worker";
  model.framework = "python";
  model.readiness = ModelReadiness::Ready;
  model.resolvedArtifactPath = weights.toStdString();
  model.runtime.provider.workerScript = worker.toStdString();
  model.sourceManifest = manifest.toStdString();
  model.runtime.provider.timeoutMs = 15000;
  return model;
}

} // namespace

TEST_CASE( "python worker handshake refuses a mistyped protocol (real process)",
           "[models][python][r33t02]" )
{
  ( void )ensureApp();
  QTemporaryDir dir;
  const QString badReady = QStringLiteral(
    "import json, sys\n"
    "# Deliberately mistyped wire protocol: a NUMBER where the contract\n"
    "# demands a string token. The C++ handshake must refuse this typed.\n"
    "print(json.dumps({\"event\": \"ready\", \"protocol\": 2}))\n"
    "sys.stdout.flush()\n" );
  const ModelInfo model = makeWorkerModel( dir, badReady, "bad-ready" );

  std::string error;
  const auto session =
    ModelRuntimeRegistry::instance().acquire( model, RequestedDevice::cpu(), &error );
  INFO( "acquire error: " << error );
  REQUIRE_FALSE( session );
  CHECK( error.find( "wire protocol" ) != std::string::npos );
  CHECK( classifyInferenceError( error ) == InferenceFailureKind::IncompatibleSchema );
}

TEST_CASE( "python worker answers duplicate heads once then recovers (real process)",
           "[models][python][r33t02]" )
{
  ( void )ensureApp();
  QTemporaryDir dir;
  // First forward answers two heads both named "sum" (ambiguous), every
  // later forward answers the known-good sum head.
  const QString moodyWorker = QStringLiteral(
    "import json, sys, base64, struct\n"
    "print(json.dumps({'event': 'ready'}))\n"
    "sys.stdout.flush()\n"
    "served = 0\n"
    "for line in sys.stdin:\n"
    "    doc = json.loads(line)\n"
    "    served += 1\n"
    "    if served == 1:\n"
    "        out = base64.b64encode(struct.pack('<f', 1.0)).decode()\n"
    "        head = {'name': 'sum', 'shape': [1,1,1,1], 'dtype': 'float32', 'data_base64': out}\n"
    "        print(json.dumps({'outputs': [head, dict(head)]}))\n"
    "    else:\n"
    "        total = 0.0\n"
    "        for t in doc.get('inputs', []):\n"
    "            raw = base64.b64decode(t['data_base64'])\n"
    "            if t.get('dtype') == 'float32' and len(raw) >= 4:\n"
    "                total += struct.unpack('<f', raw[-4:])[0]\n"
    "        out = base64.b64encode(struct.pack('<f', total)).decode()\n"
    "        head = {'name': 'sum', 'shape': [1,1,1,1], 'dtype': 'float32', 'data_base64': out}\n"
    "        print(json.dumps({'outputs': [head]}))\n"
    "    sys.stdout.flush()\n" );
  const ModelInfo model = makeWorkerModel( dir, moodyWorker, "moody" );
  const auto session = acquireModel( model );

  try
  {
    session->inferNamed( oneInput(), {} );
    FAIL( "expected the duplicate-head answer to be refused" );
  }
  catch ( const std::exception &e )
  {
    CHECK_THAT( e.what(), Catch::Matchers::ContainsSubstring( "more than once" ) );
    CHECK( classifyInferenceError( e.what() ) == InferenceFailureKind::OutputInvalid );
  }
  CHECK( session->health().failures == 1 );

  const auto outputs = session->inferNamed( oneInput(), {} );
  REQUIRE( outputs.size() == 1 );
  CHECK( outputs.front().second.dataFloat32()[ 0 ] == Catch::Approx( 1.0f ) );
  CHECK( session->health().forwardsCompleted == 1 );
}
