// test_r35_t06_preview_lifecycle.cpp — R35-T06 WP02/WP03
//
// The R33 delivery proved that a canceled/superseded/destroyed request never
// DELIVERS. It could not prove it never READS: the worker checked its abort
// flag, then entered the renderer, and only afterwards noticed that the
// receiver or the service was gone. These cases pin the real-work side with a
// shared render counter and a deterministic pool barrier:
//
//   * a destroyed receiver stops the queued read (the destroyed() handler now
//     aborts the token instead of only dropping the delivery);
//   * destroying the service stops the queued read (the dtor flags every
//     in-flight request before the QPointer can go null);
//   * a 500-request supersede storm performs exactly ONE render, not 500;
//   * the counter counts a render whose result is then discarded, because that
//     is what "real work happened" means.
//
// The barrier is a single-thread pool occupied by a blocker task: every
// preview submitted while the blocker holds the thread is guaranteed to be
// still queued, so "before it ran" is exact rather than a sleep.
#include <catch2/catch_test_macros.hpp>

#include <QAtomicInt>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <gdal_priv.h>
#include <qgsapplication.h>

#include <algorithm>
#include <functional>

#include "app/preview/asset_preview_service.h"

namespace
{

void ensureQgisApplication()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char applicationName[] = "test_r35_t06_preview_lifecycle";
    static char *argv[] = { applicationName, nullptr };
    new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
    GDALAllRegister();
}

/// 3-band Byte GeoTIFF with a per-band horizontal gradient.
QString makeGradientRaster( const QString &path, int w = 64, int h = 48 )
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h, 3, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( h ), 0.0, -1.0 };
    ds->SetGeoTransform( gt );
    std::vector<unsigned char> row( static_cast<std::size_t>( w ) );
    for ( int band = 1; band <= 3; ++band )
    {
        GDALRasterBand *b = ds->GetRasterBand( band );
        for ( int y = 0; y < h; ++y )
        {
            for ( int x = 0; x < w; ++x )
                row[static_cast<std::size_t>( x )] = static_cast<unsigned char>(
                  std::min( 255, ( x * 255 ) / std::max( 1, w - 1 ) + ( band - 1 ) ) );
            REQUIRE( b->RasterIO( GF_Write, 0, y, w, 1, row.data(), w, 1, GDT_Byte, 0, 0 )
                     == CE_None );
        }
    }
    GDALClose( ds );
    return path;
}

bool waitUntil( const std::function<bool()> &done )
{
    for ( int i = 0; i < 300 && !done(); ++i )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 5 );
    }
    return done();
}

void drainEvents( int ms = 300 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !timer.hasExpired( ms ) )
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
}

/// Occupies the pool's single worker so everything submitted afterwards is
/// still queued. `release()` unblocks it and waits for the queue to drain.
class PoolBarrier
{
  public:
    explicit PoolBarrier( QThreadPool &pool )
      : m_pool( pool )
    {
        m_pool.setMaxThreadCount( 1 );
        m_pool.start( [this]()
        {
            while ( m_gate.loadAcquire() == 0 )
                QThread::msleep( 1 );
        } );
        // Wait until the blocker owns the thread, so the next task cannot slip
        // in front of it.
        QElapsedTimer t;
        t.start();
        while ( m_pool.activeThreadCount() < 1 && !t.hasExpired( 5000 ) )
            QThread::msleep( 1 );
    }

    void release()
    {
        m_gate.storeRelease( 1 );
        m_pool.waitForDone();
    }

  private:
    QThreadPool &m_pool;
    QAtomicInt m_gate{ 0 };
};

} // namespace

using namespace sicnu::app;

TEST_CASE( "R35-T06 WP02: a destroyed receiver stops the queued read",
           "[r35][t06][preview][wp02]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "receiver.tif" ) );

    AssetPreviewService service;
    QThreadPool pool;
    PoolBarrier barrier( pool );
    service.setPool( &pool );

    int calls = 0;
    auto *receiver = new QObject;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, receiver,
                            [&]( const AssetPreviewService::Result & ) { ++calls; } );
    // Queued behind the barrier: registered, nothing rendered yet.
    REQUIRE( service.pendingRequests() == 1 );
    REQUIRE( service.rendersStarted() == 0 );
    REQUIRE( calls == 0 );

    delete receiver; // destroyed() → the token must be aborted, not just dropped
    REQUIRE( service.pendingRequests() == 0 );

    barrier.release();
    drainEvents();
    REQUIRE( calls == 0 );
    // The real point: the queued worker never entered the renderer.
    REQUIRE( service.rendersStarted() == 0 );
    REQUIRE( service.cacheEntries() == 0 );
}

TEST_CASE( "R35-T06 WP02: destroying the service stops the queued read",
           "[r35][t06][preview][wp02]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "teardown.tif" ) );

    QThreadPool pool;
    PoolBarrier barrier( pool );
    {
        auto *service = new AssetPreviewService;
        service->setPool( &pool );
        int calls = 0;
        service->requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) },
                                 nullptr, [&]( const AssetPreviewService::Result & ) { ++calls; } );
        REQUIRE( service->pendingRequests() == 1 );
        REQUIRE( service->rendersStarted() == 0 );
        delete service; // dtor flags the queued request before the QPointer nulls
    }

    barrier.release(); // the queued worker must return without reading
    drainEvents();

    // Control: the pool is still usable and a live service does render —
    // without this the "0 renders" above would be indistinguishable from a
    // pipeline that never worked.
    AssetPreviewService fresh;
    fresh.setPool( &pool );
    int freshCalls = 0;
    fresh.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
                          [&]( const AssetPreviewService::Result &r )
                          {
                              if ( r.status == PreviewRender::Status::Ready )
                                  ++freshCalls;
                          } );
    REQUIRE( waitUntil( [&] { return freshCalls == 1; } ) );
    REQUIRE( fresh.rendersStarted() == 1 );
}

TEST_CASE( "R35-T06 WP03: a supersede storm renders exactly once",
           "[r35][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "storm.tif" ) );

    AssetPreviewService service;
    QThreadPool pool;
    PoolBarrier barrier( pool );
    service.setPool( &pool );

    QObject receiver;
    int calls = 0;
    for ( int i = 0; i < 500; ++i )
    {
        service.requestPreview(
          { path, AssetPreviewService::Kind::Raster, QSize( 16 + ( i % 8 ), 16 ) }, &receiver,
          [&]( const AssetPreviewService::Result & ) { ++calls; } );
        REQUIRE( service.pendingRequests() <= 1 );
    }
    REQUIRE( service.pendingRequests() == 1 );
    REQUIRE( service.rendersStarted() == 0 );

    barrier.release();
    REQUIRE( waitUntil( [&] { return calls == 1; } ) );
    drainEvents( 100 );

    REQUIRE( calls == 1 );
    // 500 clicks, 500 queued runnables, ONE real raster read.
    REQUIRE( service.rendersStarted() == 1 );
}

TEST_CASE( "R35-T06 WP03: rendersStarted counts work whose result is discarded",
           "[r35][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "discarded.tif" ) );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    service.setPool( &pool );

    int calls = 0;
    auto *receiver = new QObject;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, receiver,
                            [&]( const AssetPreviewService::Result & ) { ++calls; } );
    // Let the worker render and marshal, but do not consume the delivery yet.
    QElapsedTimer t;
    t.start();
    while ( service.rendersStarted() == 0 && !t.hasExpired( 5000 ) )
        QThread::msleep( 5 );
    REQUIRE( service.rendersStarted() == 1 );

    // Receiver dies after the read: the delivery is dropped, the work counted.
    delete receiver;
    drainEvents();
    REQUIRE( calls == 0 );
    REQUIRE( service.rendersStarted() == 1 );
    REQUIRE( service.cacheEntries() == 0 );
}

TEST_CASE( "R35-T06 WP02: cancel before the worker runs costs no read",
           "[r35][t06][preview][wp02]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "cancel.tif" ) );

    AssetPreviewService service;
    QThreadPool pool;
    PoolBarrier barrier( pool );
    service.setPool( &pool );

    int calls = 0;
    const quint64 token = service.requestPreview(
      { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
      [&]( const AssetPreviewService::Result & ) { ++calls; } );
    REQUIRE( service.pendingRequests() == 1 );
    service.cancel( token );
    REQUIRE( service.pendingRequests() == 0 );

    barrier.release();
    drainEvents();
    REQUIRE( calls == 0 );
    REQUIRE( service.rendersStarted() == 0 ); // R33 only pinned "no callback"
}
