// test_r28_t04_cartography_watchdog.cpp — #1805 F14: the cartography dock
// per-job lifetime and the terminal-event watchdog.
//
// F14 (cartography dock): a per-job lifetime QObject used to be released
// ONLY by the terminal taskUpdated event; when TaskCenter loses that event
// (the cancellation cluster #1812 left red on master) the connections lived
// forever and the dock stayed busy with unexplainably disabled buttons.
// The fix captures the task id per job, reaps the previous lifetime on a
// new submit, and adds a watchdog that finishes the job from the
// TaskCenter's own state (terminal-but-unseen or gone). This suite pins:
//   * the terminal/gone decision over synthetic task lists,
//   * exactly-once completion when the watchdog and the natural event race,
//   * the dock un-sticking (a new submit succeeds after a lost event).
//
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>
#include <qgsproject.h>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include "agent/cartography/cartography_operators.h"
#include "app/cartography/cartography_dock.h"
#include "processing/framework/task_center.h"

#include <gdal.h>

#include <algorithm>
#include <functional>
#include <vector>

using sicnu::app::CartographyDock;

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

int main( int argc, char *argv[] )
{
    qputenv( "SICNU_CARTOGRAPHY_DIR", SICNU_CARTOGRAPHY_DATA_DIR );
    QgsApplication application( argc, argv, true );
    QgsApplication::initQgis();
    // The dock submits REAL cartography:* operators through TaskCenter —
    // the family registers explicitly (no static init), like the app and
    // CLI entries do.
    sicnu::agent::cartography::initCartographyOperators();
    const int result = Catch::Session().run( argc, argv );
    QgsProject::instance()->clear();
    QgsApplication::exitQgis();
    return result;
}

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char appName[] = "test_r28_t04_dock_lifetimes";
    static char *argv[] = { appName, nullptr };
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
}

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

struct TaskCenterGuard
{
    TaskCenterGuard() { sicnu::TaskCenter::instance().shutdownForTests(); }
    ~TaskCenterGuard() { sicnu::TaskCenter::instance().shutdownForTests(); }
};

QString writeBigRaster( const QString &path, int size )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    const char *options[] = { "TILED=YES", "BLOCKXSIZE=512", "BLOCKYSIZE=512",
                              nullptr };
    GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), size, size,
                                       1, GDT_Float32, const_cast<char **>( options ) );
    REQUIRE( dataset != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
    // Write in 512-row strips: one 40k x 40k malloc would be 6.4 GB.
    const int rows = 512;
    std::vector<float> line( static_cast<size_t>( size ) * rows, 0.25f );
    for ( int y = 0; y < size; y += rows )
    {
        const int h = std::min( rows, size - y );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, y, size, h, line.data(), size, h,
                               GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( dataset );
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// F14 — cartography dock per-job lifetime + watchdog
// ---------------------------------------------------------------------------

TEST_CASE( "task finished-or-gone decision over synthetic task lists",
           "[r28t04][cartography][f14][decision]" )
{
    ensureQgisApplication();
    auto running = []( long id ) {
        sicnu::AlgorithmTaskInfo info;
        info.taskId = id;
        info.status = sicnu::TaskStatus::Running;
        return info;
    };
    auto done = []( long id ) {
        sicnu::AlgorithmTaskInfo info;
        info.taskId = id;
        info.status = sicnu::TaskStatus::Completed;
        return info;
    };

    // The watched task still Running → keep waiting.
    QList<sicnu::AlgorithmTaskInfo> tasks;
    tasks << running( 7 ) << done( 8 );
    CHECK_FALSE( CartographyDock::taskFinishedOrGone( 7, tasks ) );

    // Terminal state present but its event was lost → reap.
    tasks.clear();
    tasks << running( 9 ) << done( 7 );
    CHECK( CartographyDock::taskFinishedOrGone( 7, tasks ) );

    // Task vanished from the center entirely → reap.
    tasks.clear();
    tasks << running( 9 );
    CHECK( CartographyDock::taskFinishedOrGone( 7, tasks ) );

    // Failed / Canceled are just as terminal for this decision.
    sicnu::AlgorithmTaskInfo failed;
    failed.taskId = 11;
    failed.status = sicnu::TaskStatus::Failed;
    tasks.clear();
    tasks << failed;
    CHECK( CartographyDock::taskFinishedOrGone( 11, tasks ) );
}

TEST_CASE( "watchdog reaping a finished-but-unseen task completes the job exactly once",
           "[r28t04][cartography][f14][exactly_once]" )
{
    ensureQgisApplication();
    TaskCenterGuard guard;

    QStringList layers{ QStringLiteral( "/tmp/fixture-a.tif" ) };
    CartographyDock dock( [&layers] { return layers; },
                          [] { return QStringLiteral( "/tmp" ); } );

    int completions = 0;
    QString lastError;
    // cartography:preflight on an empty spec fails fast but typed — a real
    // TaskCenter round trip with a genuine terminal state.
    Json::Value params( Json::objectValue );
    params["mapspec"] = Json::Value( Json::objectValue );
    REQUIRE( dock.submitOperatorJob(
      QStringLiteral( "cartography:preflight" ), params,
      [&]( const Json::Value &, const QString &error ) {
          ++completions;
          lastError = error;
      } ) );

    // Let the job reach its terminal state inside TaskCenter WITHOUT
    // delivering the queued taskUpdated event (no event processing here).
    QThread::msleep( 800 );

    // The watchdog consults TaskCenter's own state and finishes the job.
    REQUIRE( dock.reapOrphanedTask() );
    CHECK( completions == 1 );
    CHECK_FALSE( lastError.isEmpty() ); // typed failure surfaced, not swallowed

    // Now the lost event finally arrives: it must be a no-op (exactly-once).
    QElapsedTimer timer;
    timer.start();
    while ( timer.elapsed() < 3000 )
    {
        QEventLoop loop;
        QTimer::singleShot( 50, &loop, &QEventLoop::quit );
        loop.exec();
    }
    CHECK( completions == 1 );

    // The dock un-stuck: a second submit is accepted (busy state cleared).
    REQUIRE( dock.submitOperatorJob(
      QStringLiteral( "cartography:preflight" ), params,
      []( const Json::Value &, const QString & ) {} ) );
    // Drain the second job so the guard shuts down cleanly.
    QThread::msleep( 800 );
    QEventLoop loop;
    QTimer::singleShot( 200, &loop, &QEventLoop::quit );
    loop.exec();
}

TEST_CASE( "reap with no running task is a typed no-op",
           "[r28t04][cartography][f14][idle]" )
{
    ensureQgisApplication();
    TaskCenterGuard guard;
    QStringList layers{ QStringLiteral( "/tmp/fixture-a.tif" ) };
    CartographyDock dock( [&layers] { return layers; },
                          [] { return QStringLiteral( "/tmp" ); } );
    CHECK_FALSE( dock.reapOrphanedTask() );
}
