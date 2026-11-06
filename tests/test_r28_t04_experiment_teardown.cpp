// test_r28_t04_experiment_teardown.cpp — #1805 F18: the experiment studio
// dock destructor must not block the GUI thread on the live study worker.
//
// ~ExperimentStudioDock used to join m_liveRunThread (and every retired
// reaper) synchronously — up to 60 s of frozen UI while one long study
// point drained, because the cooperative cancel flag is only observed
// BETWEEN study points. The fix detaches a supervisor and delivers the
// worker's result through a GUI-thread-constructed QPointer. This suite
// pins destructor promptness while a REAL long study point is mid-flight
// (the old code blocked for the op's full duration).
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include "app/experiment_studio/experiment_studio_dock.h"
#include "experiment/experiment_store.h"

#include <gdal.h>

#include <sqlite3.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

using sicnu::app::ExperimentStudioDock;

namespace
{

void ensureQgisApplication()
{
    if ( QApplication::instance() )
        return;
    qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
    static int argc = 1;
    static char appName[] = "test_r28_t04_experiment_teardown";
    static char *argv[] = { appName, nullptr };
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
}

qlonglong countRows( const QString &storePath, const char *sql )
{
    sqlite3 *db = nullptr;
    if ( sqlite3_open_v2( storePath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY,
                          nullptr ) != SQLITE_OK )
        return -1;
    sqlite3_busy_timeout( db, 250 );
    sqlite3_stmt *stmt = nullptr;
    qlonglong count = -1;
    if ( sqlite3_prepare_v2( db, sql, -1, &stmt, nullptr ) == SQLITE_OK
         && sqlite3_step( stmt ) == SQLITE_ROW )
        count = sqlite3_column_int64( stmt, 0 );
    sqlite3_finalize( stmt );
    sqlite3_close( db );
    return count;
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

TEST_CASE( "dock destructor returns promptly while a long study point is mid-flight (F18)",
           "[r28t04][experiment_studio][f18][teardown]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString store = dir.filePath( QStringLiteral( "store.sqlite" ) );
    sicnu::experiment::ExperimentStore opener;
    REQUIRE( opener.open( store ) );

    // One long mid-flight point: a big single-band raster means the
    // threshold op itself runs for seconds, and the cooperative cancel flag
    // is only observed BETWEEN study points — exactly the window where the
    // old destructor froze the GUI for the op's full duration.
    const int kSize = 14000;
    const QString big = writeBigRaster( dir.filePath( QStringLiteral( "big.tif" ) ), kSize );

    auto dock = std::make_unique<ExperimentStudioDock>();
    REQUIRE( dock->openLiveStoreAtPath( store ) );

    bool inputFound = false;
    const auto edits = dock->findChildren<QLineEdit *>();
    for ( QLineEdit *edit : edits )
    {
        if ( edit->placeholderText().contains( QStringLiteral( "input" ),
                                               Qt::CaseInsensitive ) )
        {
            edit->setText( big );
            inputFound = true;
        }
    }
    REQUIRE( inputFound );

    QPushButton *run = dock->findChild<QPushButton *>( QStringLiteral( "rsStudioRunLiveBtn" ) );
    REQUIRE( run != nullptr );
    run->click();
    REQUIRE( waitFor( [ & ] { return !run->isEnabled(); }, 5000 ) );

    // Settle into the op: the study's first point is now executing on the
    // worker thread (the button disable confirms the run started).
    QThread::msleep( 500 );

    QElapsedTimer teardown;
    teardown.start();
    dock.reset(); // F18: must NOT block on the worker's join
    const qint64 elapsedMs = teardown.elapsed();

    // 1.5 s is >3x the post-fix cost (a detach + flag stores, well under
    // 50 ms) and far under the old behavior (the full op duration — many
    // seconds at 14000^2).
    CHECK( elapsedMs < 1500 );
    WARN( "F18 teardown took " << elapsedMs << " ms" );

    // The detached supervisor keeps the worker alive past the destructor
    // (that IS the fix). Before the binary exits, drain it: every opened
    // run reaching a terminal state in the store keeps QgsApplication
    // teardown away from a live study thread (the sibling suite's
    // discipline).
    REQUIRE( waitFor(
        [ & ] {
            const qlonglong opened = countRows( store, "SELECT COUNT(*) FROM experiment_runs" );
            const qlonglong terminal = countRows(
                store,
                "SELECT COUNT(*) FROM experiment_runs WHERE status IN"
                " ('completed','failed','cancelled')" );
            return opened > 0 && opened == terminal;
        },
        180000 ) );
}
