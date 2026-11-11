// test_orthorectification_dialog.cpp - orthorectification dialog param build
//
// Drives the dialog headlessly (no exec()): sets a synthetic raster layer and
// widget state, then asserts buildParams() assembles the gdal:orthorectification
// operator JSON (defaults omitted, explicit options surfaced).
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>

#include <cpl_string.h>
#include <cstdio>
#include <cpl_vsi.h>
#include <gdal.h>

#include <functional>
#include <atomic>
#include <chrono>
#include <semaphore>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsrasterlayer.h>

#include "app/dialogs/orthorectification_dialog.h"
#include "app/widgets/rs_scan_pool.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_orthorectification_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "OrthorectificationDialog buildParams assembles the operator JSON",
           "[ortho_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  const QString raster = dir.filePath( QStringLiteral( "input.tif" ) );
  std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 100.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( raster, 2, 2, bands, gt, QStringLiteral( "EPSG:32650" ), &err ) );

  QgsRasterLayer layer( raster, QStringLiteral( "input" ) );
  REQUIRE( layer.isValid() );

  OrthorectificationDialog dialog;
  dialog.setRasterLayer( &layer );

  auto *outputEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "rsDialogOutputEdit" ) );
  REQUIRE( outputEdit != nullptr );
  const QString output = dir.filePath( QStringLiteral( "ortho.tif" ) );
  outputEdit->setText( output );

  SECTION( "Defaults: bilinear, EPSG:4326, optional params omitted" )
  {
    const Json::Value params = dialog.buildParams();
    CHECK( params["input"].asString() == raster.toStdString() );
    CHECK( params["output"].asString() == output.toStdString() );
    CHECK( params["resampling"].asString() == "bilinear" );
    CHECK( params["dstCrs"].asString() == "EPSG:4326" );
    CHECK_FALSE( params.isMember( "dem" ) );
    CHECK_FALSE( params.isMember( "targetResolution" ) );
    CHECK_FALSE( params.isMember( "height" ) );
    CHECK_FALSE( params.isMember( "nodata" ) );
  }

  SECTION( "Explicit options surface in the JSON" )
  {
    auto *crsEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "orthoTargetCrsEdit" ) );
    auto *demEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "orthoDemEdit" ) );
    auto *resampling = dialog.findChild<QComboBox *>( QStringLiteral( "orthoResamplingCombo" ) );
    auto *resolution = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "orthoResolutionSpin" ) );
    auto *height = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "orthoHeightSpin" ) );
    auto *nodataCheck = dialog.findChild<QCheckBox *>( QStringLiteral( "orthoNodataCheck" ) );
    auto *nodataSpin = dialog.findChild<QDoubleSpinBox *>( QStringLiteral( "orthoNodataSpin" ) );
    REQUIRE( crsEdit != nullptr );
    REQUIRE( demEdit != nullptr );
    REQUIRE( resampling != nullptr );
    REQUIRE( resolution != nullptr );
    REQUIRE( height != nullptr );
    REQUIRE( nodataCheck != nullptr );
    REQUIRE( nodataSpin != nullptr );

    crsEdit->setText( QStringLiteral( "EPSG:32650" ) );
    demEdit->setText( QStringLiteral( "/data/dem.tif" ) );
    resampling->setCurrentIndex( resampling->findData( QStringLiteral( "cubic" ) ) );
    resolution->setValue( 15.0 );
    height->setValue( 500.0 );
    nodataCheck->setChecked( true );
    nodataSpin->setValue( -9999.0 );

    const Json::Value params = dialog.buildParams();
    CHECK( params["dstCrs"].asString() == "EPSG:32650" );
    CHECK( params["dem"].asString() == "/data/dem.tif" );
    CHECK( params["resampling"].asString() == "cubic" );
    CHECK( params["targetResolution"].asDouble() == 15.0 );
    CHECK( params["height"].asDouble() == 500.0 );
    CHECK( params["nodata"].asDouble() == -9999.0 );
  }
}

namespace
{

/// Serves a raster on a separate thread and lets a test hold its HTTP response
/// during GDALOpen, without blocking the thread that drives the dialog.
class GatedRasterServer : public QObject
{
  public:
    explicit GatedRasterServer( QByteArray payload )
    {
      m_worker.start();
      auto *relay = new QObject;
      relay->moveToThread( &m_worker );
      connect( &m_worker, &QThread::finished, relay, &QObject::deleteLater );
      QMetaObject::invokeMethod( relay, [this, relay, payload]() {
          auto *server = new QTcpServer( relay );
          QObject::connect( server, &QTcpServer::newConnection, server,
                            [this, server, payload]() {
                                while ( server->hasPendingConnections() )
                                  serveConnection( server->nextPendingConnection(), payload );
                            } );
          server->listen( QHostAddress::LocalHost );
          // Report via atomics, NOT a queued call: the main thread is
          // parked on the semaphore (no event loop) waiting for the port —
          // a queued report could never be delivered.
          m_port.store( server->serverPort() );
          m_ready.release();
      }, Qt::QueuedConnection );
      m_ready.acquire();
    }

    ~GatedRasterServer() override
    {
      releaseResponses();
      m_worker.quit();
      m_worker.wait();
    }

    QString source() const
    {
      return QStringLiteral( "/vsicurl/http://127.0.0.1:%1/slow.tif" )
        .arg( m_port.load() );
    }

    void holdResponses() { m_holdResponses.store( true ); }
    bool hasBlockedResponse() const { return m_waitingForRelease.load(); }

    void releaseResponses()
    {
      if ( m_holdResponses.exchange( false ) )
        m_responseGate.release();
    }

  private:
    void serveConnection( QTcpSocket *socket, const QByteArray &payload )
    {
      QObject::connect( socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater );
      QObject::connect( socket, &QAbstractSocket::readyRead, socket,
                        [this, socket, payload]() {
                            if ( !socket->peek( socket->bytesAvailable() ).contains( QByteArrayLiteral( "\r\n\r\n" ) ) )
                              return;
                            const QByteArray request = socket->readAll();
                            const bool headersOnly = request.startsWith( QByteArrayLiteral( "HEAD /slow.tif HTTP/" ) );
                            if ( !headersOnly && !request.startsWith( QByteArrayLiteral( "GET /slow.tif HTTP/" ) ) )
                            {
                              socket->write( QByteArrayLiteral( "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" ) );
                              socket->disconnectFromHost();
                              return;
                            }
                            if ( m_holdResponses.load() )
                            {
                              m_waitingForRelease.store( true );
                              // A synchronous probe regression must time out rather than deadlock.
                              ( void ) m_responseGate.try_acquire_for( std::chrono::seconds( 5 ) );
                              m_waitingForRelease.store( false );
                            }
                            qint64 from = 0;
                            qint64 to = payload.size() - 1;
                            if ( const int r = request.indexOf( QByteArrayLiteral( "Range: bytes=" ) );
                                 r >= 0 )
                            {
                              const QByteArray spec = request.mid(
                                r + 13,
                                request.indexOf( QByteArrayLiteral( "\r\n" ), r ) - r - 13 );
                              const int dash = spec.indexOf( '-' );
                              if ( dash > 0 )
                              {
                                from = spec.left( dash ).toLongLong();
                                const QByteArray upper = spec.mid( dash + 1 );
                                if ( !upper.isEmpty() )
                                  to = qMin( to, upper.toLongLong() );
                              }
                            }
                            to = qMin( to, static_cast<qint64>( payload.size() ) - 1 );
                            if ( to < from )
                            {
                              socket->write( QByteArrayLiteral(
                                "HTTP/1.1 416 Range Not Satisfiable\r\n"
                                "Content-Length: 0\r\nConnection: close\r\n\r\n" ) );
                            }
                            else
                            {
                              const QByteArray body = payload.mid(
                                static_cast<int>( from ), static_cast<int>( to - from + 1 ) );
                              const bool partial = from != 0 || to != payload.size() - 1;
                              QByteArray head = partial ? "HTTP/1.1 206 Partial Content\r\n"
                                                        : "HTTP/1.1 200 OK\r\n";
                              head += "Content-Type: application/octet-stream\r\n";
                              if ( partial )
                                head += QByteArrayLiteral( "Content-Range: bytes " )
                                        + QByteArray::number( from ) + "-"
                                        + QByteArray::number( to ) + "/"
                                        + QByteArray::number( payload.size() ) + "\r\n";
                              head += "Content-Length: " + QByteArray::number( body.size() ) + "\r\n";
                              head += "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n";
                              socket->write( head );
                              if ( !headersOnly )
                                socket->write( body );
                            }
                            socket->disconnectFromHost();
                        } );
    }

    QThread m_worker;
    std::atomic<quint16> m_port{ 0 };
    std::atomic<bool> m_holdResponses{ false };
    std::atomic<bool> m_waitingForRelease{ false };
    std::binary_semaphore m_responseGate{ 0 };
    std::binary_semaphore m_ready{ 0 };
};

/// A tiny GeoTIFF's bytes (written once for the HTTP fixture).
QByteArray tinyGeoTiffBytes()
{
    static QByteArray cached;
    if ( !cached.isEmpty() )
      return cached;
    QTemporaryFile file;
    file.setAutoRemove( false );
    if ( !file.open() )
      return cached;
    const QString path = file.fileName();
    file.close();
    std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 100.0f ) );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    if ( !writeGdalOutput( path, 2, 2, bands, gt, QStringLiteral( "EPSG:32650" ), &err ) )
      return cached;
    QFile f( path );
    if ( f.open( QIODevice::ReadOnly ) )
      cached = f.readAll();
    f.close();
    QFile::remove( path );
    return cached;
}

bool vsicurlAvailable()
{
    char **prefixes = VSIGetFileSystemsPrefixes();
    const bool available = CSLFindString( prefixes, "/vsicurl/" ) >= 0;
    CSLDestroy( prefixes );
    return available;
}

/// Pumps events until the predicate holds or the budget expires.
bool waitFor( const std::function<bool()> &predicate, int timeoutMs )
{
  QElapsedTimer timer;
  timer.start();
  while ( !predicate() )
  {
    if ( timer.elapsed() > timeoutMs )
      return false;
    QEventLoop loop;
    QTimer::singleShot( 20, &loop, &QEventLoop::quit );
    loop.exec();
  }
  return true;
}

QString modelStatusLabelText( OrthorectificationDialog &dialog )
{
    auto *label = dialog.findChild<QLabel *>( QStringLiteral( "rsOrthoModelStatus" ) );
    return label ? label->text() : QString();
}

} // namespace

TEST_CASE( "OrthorectificationDialog probes RPC/GCP without blocking the GUI thread (F7)",
           "[ortho_dialog][f7][async]" )
{
    ensureQgisApplication();
    if ( !vsicurlAvailable() )
        SKIP( "this GDAL build has no /vsicurl/ handler; async contract pinned by the "
              "local-raster cases below" );

    const QByteArray tif = tinyGeoTiffBytes();
    REQUIRE( !tif.isEmpty() );
    GatedRasterServer server( tif );
    QgsRasterLayer layer( server.source(), QStringLiteral( "remote" ) );
    REQUIRE( layer.isValid() );
    VSICurlClearCache();
    server.holdResponses();

    OrthorectificationDialog dialog;
    dialog.setRasterLayer( &layer );
    // setRasterLayer has returned while GDALOpen still awaits the response.
    REQUIRE( waitFor( [&] { return server.hasBlockedResponse(); }, 3000 ) );
    CHECK( modelStatusLabelText( dialog )
             .contains( QStringLiteral( "Checking" ), Qt::CaseInsensitive ) );

    server.releaseResponses();
    REQUIRE( waitFor( [&] {
        return !modelStatusLabelText( dialog )
                  .contains( QStringLiteral( "Checking" ), Qt::CaseInsensitive );
    }, 5000 ) );
    CHECK( modelStatusLabelText( dialog )
             .contains( QStringLiteral( "neither RPC metadata nor GCPs" ) ) );
}

TEST_CASE( "OrthorectificationDialog rapid layer switches keep only the freshest verdict (F7)",
           "[ortho_dialog][f7][staleness]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString raster = dir.filePath( QStringLiteral( "local.tif" ) );
    std::vector<std::vector<float>> bands( 1, std::vector<float>( 4, 100.0f ) );
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    REQUIRE( writeGdalOutput( raster, 2, 2, bands, gt, QStringLiteral( "EPSG:32650" ), &err ) );

    QgsRasterLayer first( raster, QStringLiteral( "first" ) );
    QgsRasterLayer second( raster, QStringLiteral( "second" ) );

    OrthorectificationDialog dialog;
    dialog.setRasterLayer( &first );
    dialog.setRasterLayer( &second ); // supersedes before any result lands

    REQUIRE( waitFor( [&] {
        const QString text = modelStatusLabelText( dialog );
        return !text.isEmpty()
               && !text.contains( QStringLiteral( "Checking" ), Qt::CaseInsensitive );
    }, 5000 ) );
    // Exactly one verdict applied — the freshest generation's. A stale
    // overwrite would still read "neither…", so also pin no double-write:
    // the generation guard means only one queued delivery exists.
    CHECK( modelStatusLabelText( dialog )
             .contains( QStringLiteral( "neither RPC metadata nor GCPs" ) ) );
}

TEST_CASE( "OrthorectificationDialog survives teardown while a probe is in flight (F7)",
           "[ortho_dialog][f7][teardown]" )
{
    ensureQgisApplication();
    if ( !vsicurlAvailable() )
        SKIP( "no /vsicurl/ handler in this GDAL build" );

    const QByteArray tif = tinyGeoTiffBytes();
    REQUIRE( !tif.isEmpty() );
    GatedRasterServer server( tif );
    QgsRasterLayer layer( server.source(), QStringLiteral( "remote" ) );
    REQUIRE( layer.isValid() );

    // The layer constructor has already fetched this URL. Clear its cache
    // before arming the gate so only a new request proves the probe started.
    VSICurlClearCache();
    server.holdResponses();

    auto dialog = std::make_unique<OrthorectificationDialog>();
    QPointer<OrthorectificationDialog> destroyed( dialog.get() );
    dialog->setRasterLayer( &layer );
    REQUIRE( waitFor( [&] { return server.hasBlockedResponse(); }, 3000 ) );
    dialog.reset();
    REQUIRE( destroyed.isNull() );

    server.releaseResponses();
    REQUIRE( sicnu::app::RsScanPool::instance().pool().waitForDone( 5000 ) );
    QCoreApplication::sendPostedEvents( qApp, QEvent::MetaCall );
    CHECK( destroyed.isNull() );
}
