// test_parity_async_late_arrival_r4.cpp — async late-arrival race oracles
// (ui-backend-state-parity-r4 / WP-C)
//
// Six race classes from PARITY_MAP.md section M, each one a "the answer comes
// from the landing policy's own contract" oracle:
//
//   as1  out-of-order completion: the OLDER reply must not overwrite the
//        newer search's results (generation stamp, PARITY_MAP AS-1);
//   as2  timeout expiry: a timed-out query's late ERROR must not reach the
//        user as a fresh failure for an already-superseded search (AS-2);
//   as3  host closed: closing the dialog invalidates its in-flight search —
//        the hidden dialog must not mutate, the drop must be traced (AS-3);
//   as4  session switched: a task's layer auto-load requested for session A
//        must not land after the shell moved to project B (AS-4);
//   as5  save-as re-home: same, across saveProjectAsTo (AS-5);
//   pd2/sp2  cancel still lands: a cancelled ProgressDialog must not
//        auto-accept on a late max progress; a cancelled (not superseded)
//        RsScanPool generation must stay stale even after the cancel set
//        overflows (AS-6 / PARITY_MAP PD-2, SP-2).
//
// The STAC faces are driven against a local loopback HTTP stub
// (SICNU_STAC_ALLOW_PRIVATE=1, set before any request) so completion order
// is scriptable. The shell faces use the #1312 full-shell fixture pattern.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "main_window.h"
#include "processing/framework/task_center.h"
#include "widgets/progress_dialog.h"
#include "widgets/rs_scan_pool.h"
#include "dialogs/stac_browser_dialog.h"
#include "stac_client.h"

#include <QApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <QSignalSpy>
#include <qgsmapcanvas.h>
#include <qgsproject.h>

#include <gdal.h>

#include <memory>

namespace
{
int fake_argc = 1;
char fake_argv0[] = "test_parity_async_late_arrival_r4";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
    // Loopback STAC stub: the SSRF policy gates private hosts behind this
    // flag (stac_client.cpp validateUrlPolicy). Test-only opt-in.
    qputenv( "SICNU_STAC_ALLOW_PRIVATE", "1" );
    QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-selftest" ) );
    QCoreApplication::setApplicationName( QStringLiteral( "parity-async-r4" ) );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QApplication *app = new QApplication( fake_argc, fake_argv );
    QSettings().clear();
    return app;
  }
  return static_cast<QApplication *>( QCoreApplication::instance() );
}

class FastExitListener : public Catch::EventListenerBase
{
  public:
    using Catch::EventListenerBase::EventListenerBase;
    void testRunEnded( const Catch::TestRunStats &stats ) override
    {
      const bool ok = !stats.aborting && stats.totals.testCases.failed == 0;
      std::fprintf( stderr, "\n%s: %u/%u assertions, %u/%u test cases\n",
                    ok ? "ALL TESTS PASSED" : "TESTS FAILED",
                    static_cast<unsigned>( stats.totals.assertions.passed ),
                    static_cast<unsigned>( stats.totals.assertions.passed
                                           + stats.totals.assertions.failed ),
                    static_cast<unsigned>( stats.totals.testCases.passed ),
                    static_cast<unsigned>( stats.totals.testCases.passed
                                           + stats.totals.testCases.failed ) );
      std::fflush( stderr );
      std::_Exit( ok ? 0 : 1 );
    }
};
CATCH_REGISTER_LISTENER( FastExitListener )

// ---------------------------------------------------------------------------
// Loopback STAC stub: records arriving requests, lets the test release
// responses in any order (the injection point for out-of-order finishes).
// ---------------------------------------------------------------------------
class StacHttpStub : public QTcpServer
{
  public:
    bool start()
    {
      if ( !listen( QHostAddress::LocalHost ) )
        return false;
      connect( this, &QTcpServer::newConnection, this, [this] {
        while ( QTcpSocket *socket = nextPendingConnection() )
        {
          connect( socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater );
          connect( socket, &QTcpSocket::readyRead, this, [this, socket] {
            m_requests[socket].append( socket->readAll() );
            if ( !m_requests[socket].contains( "\r\n\r\n" ) )
              return;
            // A full request header arrived: park the socket for scripted
            // delivery. POST bodies are not used by StacClient search.
            if ( !m_order.contains( socket ) )
              m_order.append( socket );
          } );
        }
      } );
      return true;
    }

    QUrl endpoint() const
    {
      return QUrl( QStringLiteral( "http://127.0.0.1:%1" ).arg( serverPort() ) );
    }

    int parkedCount() const { return m_order.size(); }

    /// Deliver a STAC FeatureCollection body as the response for the
    /// @p index-th parked request (insertion order). -1 = the last one.
    bool respond( int index, const QVariantList &features )
    {
      QTcpSocket *socket = pickSocket( index );
      if ( !socket )
        return false;
      QJsonObject root;
      root.insert( QStringLiteral( "features" ), QJsonArray::fromVariantList( features ) );
      root.insert( QStringLiteral( "links" ), QJsonArray() );
      const QByteArray body = QJsonDocument( root ).toJson( QJsonDocument::Compact );
      const QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/geo+json\r\n"
        "Content-Length: " + QByteArray::number( body.size() ) + "\r\n"
        "Connection: close\r\n\r\n" + body;
      socket->write( response );
      return true;
    }

  private:
    QTcpSocket *pickSocket( int index ) const
    {
      if ( m_order.isEmpty() )
        return nullptr;
      if ( index < 0 || index >= m_order.size() )
        index = m_order.size() - 1;
      return m_order.at( index );
    }

    QHash<QTcpSocket *, QByteArray> m_requests;
    QList<QTcpSocket *> m_order;
};

QVariantList featureList( const QString &idPrefix, int count )
{
  QVariantList features;
  for ( int i = 0; i < count; ++i )
  {
    QVariantMap feature;
    feature.insert( QStringLiteral( "id" ), QStringLiteral( "%1-%2" ).arg( idPrefix ).arg( i ) );
    features.append( feature );
  }
  return features;
}

/// Runs the dialog's private searchCatalog slot against the stub endpoint.
void triggerDialogSearch( StacBrowserDialog &dialog, const QUrl &endpoint )
{
  QLineEdit *endpointEdit = dialog.findChild<QLineEdit *>();
  REQUIRE( endpointEdit != nullptr );
  endpointEdit->setText( endpoint.toString() );
  REQUIRE( QMetaObject::invokeMethod( &dialog, "searchCatalog" ) );
}

QTableWidget *resultsTable( StacBrowserDialog &dialog )
{
  return dialog.findChild<QTableWidget *>();
}

// ---------------------------------------------------------------------------
// Full-shell fixture (the #1312 pattern) for the autoload session faces.
// ---------------------------------------------------------------------------
struct ShellFixture
{
  QgisDesktopWindow window;
  ShellFixture()
  {
    window.show();
    QTest::qWaitForWindowExposed( &window );
  }
};

bool writeMiniGeoTiff( const QString &path )
{
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  if ( !driver )
    return false;
  GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 4, 4, 1, GDT_Float32, nullptr );
  if ( !ds )
    return false;
  double geo[6] = { 116.0, 0.01, 0.0, 40.0, 0.0, -0.01 };
  GDALSetGeoTransform( ds, geo );
  GDALSetProjection( ds, "EPSG:4326" );
  float line[4] = { 0.5f, 1.0f, 1.5f, 2.0f };
  GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
  const bool ok = GDALRasterIO( band, GF_Write, 0, 0, 4, 4, line, 4, 4, GDT_Float32, 0, 0 ) == CE_None;
  GDALClose( ds );
  return ok;
}

QgsMapCanvas *mainCanvas( QgisDesktopWindow &window )
{
  return window.findChild<QgsMapCanvas *>();
}

bool writeValidProject( const QString &path )
{
  QgsProject probe;
  return probe.write( path );
}

struct ProjectPair
{
  QTemporaryDir tmp;
  QString pathA;
  QString pathB;

  ProjectPair()
  {
    REQUIRE( tmp.isValid() );
    const QString dirA = tmp.filePath( QStringLiteral( "工程甲" ) );
    const QString dirB = tmp.filePath( QStringLiteral( "工程乙" ) );
    REQUIRE( QDir().mkpath( dirA ) );
    REQUIRE( QDir().mkpath( dirB ) );
    pathA = QDir( dirA ).filePath( QStringLiteral( "工程A.qgs" ) );
    pathB = QDir( dirB ).filePath( QStringLiteral( "工程B.qgs" ) );
    REQUIRE( writeValidProject( pathA ) );
    REQUIRE( writeValidProject( pathB ) );
  }
};

/// Push a taskAdded announcement for an auto-load task through the real
/// TaskCenter signal (the window's recording seam) without executing a job.
/// Returns the invoked flag so a stale signature fails loudly.
bool announceAutoLoadTask( long taskId, const QString &outputPath )
{
  sicnu::AlgorithmTaskInfo info;
  info.taskId = taskId;
  info.status = sicnu::TaskStatus::Queued;
  info.autoLoadLayer = true;
  info.outputLayerPath = outputPath;
  return QMetaObject::invokeMethod( &sicnu::TaskCenter::instance(),
                                    "taskAdded",
                                    Q_ARG( sicnu::AlgorithmTaskInfo, info ) );
}

/// Deliver a layerAutoLoadRequested through the real TaskCenter signal,
/// paired with the announcing task id (the F-05 pairing contract).
bool deliverAutoLoadRequest( const QString &path, long taskId )
{
  return QMetaObject::invokeMethod( &sicnu::TaskCenter::instance(),
                                    "layerAutoLoadRequested",
                                    Q_ARG( QString, path ),
                                    Q_ARG( long, taskId ) );
}

} // namespace

// ===========================================================================
// AS-1 (race class 4): out-of-order completion — the older reply must not
// overwrite the newer search's results.
// ===========================================================================
TEST_CASE( "AS-1: stale STAC reply finishing late cannot overwrite the newer search",
           "[parity][async][stac][parity-as1]" )
{
  ensureApp();
  StacHttpStub stub;
  REQUIRE( stub.start() );

  StacBrowserDialog dialog( nullptr );
  dialog.show();
  QTest::qWaitForWindowExposed( &dialog );

  // Query A parks on the server; query B parks behind it. Release B (the
  // NEWER search) first, then let A finish late.
  triggerDialogSearch( dialog, stub.endpoint() );
  triggerDialogSearch( dialog, stub.endpoint() );
  // QNetworkAccessManager opens one TCP connection per in-flight reply; let
  // both connects complete before scripting the deliveries (event-loop
  // plumbing, not race timing).
  for ( int i = 0; i < 50 && stub.parkedCount() < 2; ++i )
    QTest::qWait( 50 );
  REQUIRE( stub.parkedCount() == 2 );

  QEventLoop loop;
  QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
  int completions = 0;
  auto conn = QObject::connect( dialog.findChild<StacClient *>(), &StacClient::searchCompleted,
                       [&completions, &loop]( const QVariantList &, const QString &, const QUrl & ) {
                         if ( ++completions == 2 )
                           loop.quit();
                       } );
  REQUIRE( conn );
  REQUIRE( stub.respond( 1, featureList( "newer", 3 ) ) ); // B first
  REQUIRE( stub.respond( 0, featureList( "older", 7 ) ) ); // A finishes late
  loop.exec();
  QObject::disconnect( conn );
  QApplication::processEvents();

  // The dialog must show the NEWER search's three features. Before the
  // generation stamp this is exactly the overwrite window: the late A reply
  // repopulates the table with its seven stale rows.
  QTableWidget *table = resultsTable( dialog );
  REQUIRE( table != nullptr );
  INFO( "row count after out-of-order finishes: " << table->rowCount() );
  CHECK( table->rowCount() == 3 );
  if ( table->rowCount() == 3 )
  {
    CHECK( table->item( 0, 0 )->text() == QStringLiteral( "newer-0" ) );
  }
}

// ===========================================================================
// AS-2 (race class 6): a query that already timed out must not resurface as
// a fresh failure for an already-superseded search. The contract lives at
// the client boundary: after the winner's results are on screen, the timed
// out query's late ERROR must not reach the completion channel again.
// (The wall-clock wait IS the class-6 window: stac_client.cpp sets
// setTransferTimeout(10000); no injection shortcut.)
// ===========================================================================
TEST_CASE( "AS-2: superseded query's timeout error never reaches the completion channel",
           "[parity][async][stac][timeout][slow][parity-as2]" )
{
  ensureApp();
  StacHttpStub stub;
  REQUIRE( stub.start() );

  StacClient client;
  QSignalSpy done( &client, &StacClient::searchCompleted );
  REQUIRE( done.isValid() );
  QSignalSpy dropped( &client, &StacClient::searchDropped );
  REQUIRE( dropped.isValid() );

  client.search( stub.endpoint().toString(), QString(), QString(), {}, 10 );
  client.search( stub.endpoint().toString(), QString(), QString(), {}, 10 );
  for ( int i = 0; i < 50 && stub.parkedCount() < 2; ++i )
    QTest::qWait( 50 );
  REQUIRE( stub.parkedCount() == 2 );

  // The NEWER query (index 1) answers first and wins the completion channel.
  REQUIRE( stub.respond( 1, featureList( "winner", 2 ) ) );
  for ( int i = 0; i < 50 && done.count() < 1; ++i )
    QTest::qWait( 50 );
  REQUIRE( done.count() == 1 );

  // The older query now hits its real 10 s transfer timeout. Its late error
  // must be recognized as expired: the completion channel stays at one
  // delivery (the winner's). Pre-generation-stamp this fires a second
  // searchCompleted carrying the stale failure.
  for ( int i = 0; i < 60 && done.count() < 2; ++i )
    QTest::qWait( 250 ); // bounded: real 10 s timeout or 15 s cap
  QApplication::processEvents();

  INFO( "completion deliveries: " << done.count() );
  CHECK( done.count() == 1 );
  // The drop must be traceable, never silent (landing-policy contract).
  INFO( "drop traces: " << dropped.count() );
  CHECK( dropped.count() == 1 );
}

// ===========================================================================
// AS-3 (race class 2): closing the host dialog invalidates its in-flight
// search — the hidden dialog must not mutate.
// ===========================================================================
TEST_CASE( "AS-3: results arriving after the dialog closed do not mutate the hidden state",
           "[parity][async][stac][parity-as3]" )
{
  ensureApp();
  StacHttpStub stub;
  REQUIRE( stub.start() );

  QPointer<StacBrowserDialog> dialog( new StacBrowserDialog( nullptr ) );
  dialog->show();
  QTest::qWaitForWindowExposed( dialog );
  QSignalSpy dropped( dialog->findChild<StacClient *>(), &StacClient::searchDropped );
  REQUIRE( dropped.isValid() );

  triggerDialogSearch( *dialog, stub.endpoint() );
  for ( int i = 0; i < 50 && stub.parkedCount() < 1; ++i )
    QTest::qWait( 50 );
  REQUIRE( stub.parkedCount() == 1 );

  QTableWidget *table = resultsTable( *dialog );
  REQUIRE( table != nullptr );
  REQUIRE( table->rowCount() == 0 );

  // Close (hide, object alive): the logical host is gone.
  dialog->close();
  QApplication::processEvents();
  REQUIRE( dialog != nullptr );

  // The reply arrives afterwards — it must be dropped, not land in the
  // hidden dialog's table.
  REQUIRE( stub.respond( 0, featureList( "post-close", 5 ) ) );
  QTest::qWait( 1200 );
  QApplication::processEvents();

  INFO( "hidden dialog rows after late delivery: " << table->rowCount() );
  CHECK( table->rowCount() == 0 );
  // The host-close invalidation must be traceable too (AS-3 drop contract).
  INFO( "drop traces: " << dropped.count() );
  CHECK( dropped.count() == 1 );
  dialog->deleteLater();
}

// ===========================================================================
// AS-4 (race class 1): an auto-load requested for session A must not land
// after the shell moved to project B.
// ===========================================================================
TEST_CASE( "AS-4: layer auto-load from a superseded session does not land in the new project",
           "[parity][async][autoload][slow][parity-as4]" )
{
  ensureApp();
  ShellFixture fx;
  ProjectPair projects;
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString staleTif = tmp.filePath( QStringLiteral( "stale-session.tif" ) );
  const QString liveTif = tmp.filePath( QStringLiteral( "live-session.tif" ) );
  REQUIRE( writeMiniGeoTiff( staleTif ) );
  REQUIRE( writeMiniGeoTiff( liveTif ) );

  REQUIRE( fx.window.openProjectFrom( projects.pathA ) );
  QgsMapCanvas *canvas = mainCanvas( fx.window );
  REQUIRE( canvas != nullptr );
  const int canvasLayersBefore = canvas->layers().size();

  // A task with an auto-load output is announced in session A...
  REQUIRE( announceAutoLoadTask( 900001, staleTif ) );
  // ...then the user moves the shell to project B before completion...
  REQUIRE( fx.window.openProjectFrom( projects.pathB ) );
  REQUIRE( QgsProject::instance()->fileName() == projects.pathB );
  // ...and the completion request arrives afterwards.
  REQUIRE( deliverAutoLoadRequest( staleTif, 900001 ) );
  QApplication::processEvents();

  // The late request must be dropped: the display view must NOT grow by the
  // dead session's layer (pre-policy this loads right into the new session).
  INFO( "canvas layers after stale delivery: " << canvas->layers().size()
        << " (before: " << canvasLayersBefore << ")" );
  CHECK( canvas->layers().size() == canvasLayersBefore );

  // Control: a request announced AND delivered in the current session still
  // lands (the recording is per-session, not a blanket ban).
  REQUIRE( announceAutoLoadTask( 900002, liveTif ) );
  REQUIRE( deliverAutoLoadRequest( liveTif, 900002 ) );
  QApplication::processEvents();
  INFO( "canvas layers after live delivery: " << canvas->layers().size() );
  CHECK( canvas->layers().size() == canvasLayersBefore + 1 );
}

// ===========================================================================
// AS-5 (race class 3): same, across Save As — the re-homed session must not
// adopt the pre-SaveAs output.
// ===========================================================================
TEST_CASE( "AS-5: layer auto-load announced before Save As does not land after it",
           "[parity][async][autoload][slow][parity-as5]" )
{
  ensureApp();
  ShellFixture fx;
  ProjectPair projects;
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString preSaveTif = tmp.filePath( QStringLiteral( "pre-saveas.tif" ) );
  REQUIRE( writeMiniGeoTiff( preSaveTif ) );

  REQUIRE( fx.window.openProjectFrom( projects.pathA ) );
  QgsMapCanvas *canvas = mainCanvas( fx.window );
  REQUIRE( canvas != nullptr );
  const int canvasLayersBefore = canvas->layers().size();
  const QString saveTarget = projects.tmp.filePath( QStringLiteral( "重定位.qgs" ) );

  REQUIRE( announceAutoLoadTask( 900003, preSaveTif ) );
  REQUIRE( fx.window.saveProjectAsTo( saveTarget ) );
  REQUIRE( deliverAutoLoadRequest( preSaveTif, 900003 ) );
  QApplication::processEvents();

  // Save As re-homes the session: the stale auto-load is dropped, not
  // loaded into the re-homed identity's view.
  CHECK( QgsProject::instance()->fileName() == saveTarget );
  INFO( "canvas layers after stale post-saveas delivery: " << canvas->layers().size() );
  CHECK( canvas->layers().size() == canvasLayersBefore );
}

// ===========================================================================
// PD-2 (race class 5, dialog leg): a cancelled dialog must not auto-accept
// on a late max progress — cancel wins, always.
// ===========================================================================
TEST_CASE( "PD-2: cancelled progress dialog ignores a late max progress update",
           "[parity][async][progress][parity-pd2]" )
{
  ensureApp();
  ProgressDialog dialog;
  dialog.setAutoClose( true );
  dialog.show();
  QTest::qWaitForWindowExposed( &dialog );

  dialog.cancel();
  REQUIRE( dialog.isCancelled() );

  // A queued progress update that still reports maximum lands late. The
  // auto-close-on-max policy must not turn the cancellation into an
  // Accepted (success) result.
  dialog.setValue( dialog.maximum() );
  QTest::qWait( 900 );
  QApplication::processEvents();

  INFO( "dialog result after late max progress on cancelled dialog: " << dialog.result() );
  CHECK( dialog.result() != QDialog::Accepted );
  CHECK( dialog.isCancelled() );
}

// ===========================================================================
// PD-3: a reset() inside the auto-close window cancels the pending accept —
// the restarted operation must not be closed as if it had succeeded.
// ===========================================================================
TEST_CASE( "PD-3: reset inside the auto-close window cancels the pending accept",
           "[parity][async][progress][parity-pd3]" )
{
  ensureApp();
  ProgressDialog dialog;
  dialog.setAutoClose( true );
  dialog.show();
  QTest::qWaitForWindowExposed( &dialog );

  dialog.setValue( dialog.maximum() ); // schedules the 500 ms accept
  dialog.reset();                      // restarts the operation inside the window
  QTest::qWait( 900 );
  QApplication::processEvents();

  INFO( "dialog result after reset inside auto-close window: " << dialog.result() );
  CHECK( dialog.result() != QDialog::Accepted );
  CHECK( dialog.value() == 0 );
}

// ===========================================================================
// SP-2 (race class 5, pool leg): cancel(g) of a still-current generation
// must stay stale even when unrelated cancellations overflow the set.
// ===========================================================================
TEST_CASE( "SP-2: cancelled generation remains stale across cancel-set overflow",
           "[parity][async][scanpool][parity-sp2]" )
{
  ensureApp();
  auto &pool = sicnu::app::RsScanPool::instance();

  // Owner W opens its generation and cancels it WITHOUT superseding — the
  // worker keeps consulting the canceled set for exactly this case.
  const char ownerTag = 'W';
  const void *owner = &ownerTag;
  const quint64 victim = pool.nextGeneration( owner );
  pool.cancel( victim, owner );
  REQUIRE( pool.isStale( victim, owner ) );

  // Unrelated churn: 1100 other widgets cancel their generations, pushing
  // the canceled set past its 1024 wholesale-clear threshold.
  const char otherTag = 'X';
  const void *other = &otherTag;
  for ( int i = 0; i < 1100; ++i )
  {
    const quint64 g = pool.nextGeneration( other );
    pool.cancel( g, other );
  }

  // The victim's worker must still observe stale: the wholesale clear used
  // to wipe exactly this entry and let the expired scan land.
  CHECK( pool.isStale( victim, owner ) );
}
