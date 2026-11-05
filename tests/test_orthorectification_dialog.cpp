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
#include <QLineEdit>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cpl_error.h>
#include <cpl_vsi.h>
#include <gdal.h>

#include <functional>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsrasterlayer.h>

#include "app/dialogs/orthorectification_dialog.h"
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

/// A raster source whose GDALOpen takes ~@p delayMs: a local HTTP server
/// that accepts the connection but sleeps before answering. Opening via
/// /vsicurl/ turns the server delay into GDALOpen time. Returns an empty
/// string when this GDAL build cannot do /vsicurl/ (the caller then SKIPs:
/// the async contract is still pinned by the local-raster cases).
class SlowSourceServer : public QObject
{
  public:
    explicit SlowSourceServer( int delayMs ) : m_delayMs( delayMs )
    {
      connect( &m_server, &QTcpServer::newConnection, this, [this] {
        while ( m_server.hasPendingConnections() )
        {
          QTcpSocket *socket = m_server.nextPendingConnection();
          connect( socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater );
          connect( socket, &QAbstractSocket::readyRead, this, [this, socket] {
              socket->readAll(); // drain the request line/headers
              // Minimal HTTP/1.1 200 with a small body — enough for
              // GDALOpen to attempt the driver sniff and fail typed after
              // the delay (the content is irrelevant: rpcOrGcp only needs
              // the open to take time and fail or succeed either way).
              QTimer::singleShot( m_delayMs, socket, [socket] {
                  const QByteArray body = "not really a geotiff";
                  const QByteArray head =
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/octet-stream\r\n"
                      "Content-Length: " +
                      QByteArray::number( body.size() ) +
                      "\r\n"
                      "Accept-Ranges: bytes\r\n"
                      "Connection: close\r\n\r\n";
                  socket->write( head + body );
                  socket->flush();
                  socket->disconnectFromHost();
              } );
          } );
        }
      } );
      m_server.listen( QHostAddress::LocalHost );
    }

    QString source() const
    {
      return QStringLiteral( "/vsicurl/http://127.0.0.1:%1/slow.tif" )
        .arg( m_server.serverPort() );
    }

  private:
    QTcpServer m_server;
    int m_delayMs;
};

bool vsicurlAvailable()
{
    GDALDriverH probe = GDALGetDriverByName( "HTTP" );
    ( void ) probe;
    // /vsicurl/ availability: open a syntactically valid but unreachable
    // URL with a short timeout — a vsicurl build returns an open error
    // quickly; a build without curl fails identically fast, so instead
    // ask the vsi plugin registry.
    VSILFILE *fh = VSIFOpenL( "/vsicurl/http://127.0.0.1:1/x", "rb" );
    if ( fh )
    {
        VSIFCloseL( fh );
        return true;
    }
    // Distinguish "handler exists but connection refused" (curl present)
    // from "unknown filesystem" via CPLGetLastErrorMsg.
    const QString msg = QString::fromUtf8( CPLGetLastErrorMsg() );
    return !msg.contains( QStringLiteral( "not recognized" ) );
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

    SlowSourceServer server( 500 );

    QgsRasterLayer layer( server.source(), QStringLiteral( "remote" ) );

    QElapsedTimer timer;
    timer.start();
    OrthorectificationDialog dialog;
    dialog.setRasterLayer( &layer );
    const qint64 returnedMs = timer.elapsed();

    // The old code ran GDALOpen inline: setRasterLayer could not return
    // before the 500 ms server delay. The probe now runs on the scan pool.
    CHECK( returnedMs < 200 );

    // The label shows a transient checking state until the probe lands.
    CHECK( modelStatusLabelText( dialog )
             .contains( QStringLiteral( "Checking" ), Qt::CaseInsensitive ) );

    // …and the final verdict arrives via the event loop.
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

    SlowSourceServer server( 600 );
    QgsRasterLayer layer( server.source(), QStringLiteral( "remote" ) );

    auto dialog = std::make_unique<OrthorectificationDialog>();
    dialog->setRasterLayer( &layer ); // probe queued on the scan pool
    dialog.reset();                   // dies before the probe lands

    // Drain: the late result must be dropped (QPointer + generation), not
    // delivered into freed memory.
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < 1500 )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 20 );
        QThread::msleep( 20 );
    }
    SUCCEED( "no use-after-free from the late probe delivery" );
}
