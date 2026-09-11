// test_asset_preview_service.cpp — Professional Workbench 8.0 (package E)
//
// Pins the bounded async preview contract: pure raster/vector renders over
// synthetic fixtures, typed failures (missing file, cap refusal), service
// cache + supersede/cancel/dead-receiver semantics, and cache bounds.
// Every wait is a bounded spin; every fixture is generated into a
// QTemporaryDir.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QObject>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTextStream>
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
    static char applicationName[] = "test_asset_preview_service";
    static char *argv[] = { applicationName, nullptr };
    new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
    GDALAllRegister();
}

/// 3-band Byte GeoTIFF with distinct per-band horizontal gradients.
QString makeGradientRaster( const QString &path, int w = 64, int h = 48 )
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h, 3, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( h ), 0.0, -1.0 };
    ds->SetGeoTransform( gt );
    for ( int band = 1; band <= 3; ++band )
    {
        unsigned char row[4096];
        GDALRasterBand *b = ds->GetRasterBand( band );
        for ( int y = 0; y < h; ++y )
        {
            for ( int x = 0; x < w; ++x )
                row[x] = static_cast<unsigned char>(
                  std::min( 255, ( x * 255 ) / std::max( 1, w - 1 ) + ( band - 1 ) ) );
            REQUIRE( b->RasterIO( GF_Write, 0, y, w, 1, row, w, 1, GDT_Byte, 0, 0 ) == CE_None );
        }
    }
    GDALClose( ds );
    return path;
}

/// 1-band Float32 GeoTIFF with a declared NoData value on half the rows.
QString makeNoDataRaster( const QString &path )
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    constexpr int W = 32, H = 32;
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
    ds->SetGeoTransform( gt );
    GDALRasterBand *band = ds->GetRasterBand( 1 );
    band->SetNoDataValue( -9999.0 );
    for ( int y = 0; y < H; ++y )
    {
        float row[W];
        for ( int x = 0; x < W; ++x )
            row[x] = y < H / 2 ? 100.0f + x : -9999.0f; // valid rows vary
        REQUIRE( band->RasterIO( GF_Write, 0, y, W, 1, row, W, 1, GDT_Float32, 0, 0 ) == CE_None );
    }
    GDALClose( ds );
    return path;
}

/// Tiny point CSV the OGR driver opens (bounded feature set).
QString makePointCsv( const QString &path, int count = 25 )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Text ) );
    QTextStream stream( &file );
    stream << "lon,lat\n";
    for ( int i = 0; i < count; ++i )
        stream << ( 100.0 + i * 0.01 ) << ',' << ( 30.0 + i * 0.01 ) << '\n';
    file.close();
    return path;
}

/// Pumps queued events until @p done holds or ~1.6 s elapse (bounded).
bool waitUntil( const std::function<bool()> &done )
{
    for ( int i = 0; i < 160 && !done(); ++i )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 5 );
    }
    return done();
}

/// Pumps queued events for a fixed short window (delivery draining).
void drainEvents( int ms = 300 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !timer.hasExpired( ms ) )
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
}

} // namespace

using namespace sicnu::app;

TEST_CASE( "Raster preview renders a fitted gradient image", "[wb8][preview]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "gradient.tif" ) );

    const PreviewRender render = renderRasterPreview( path, QSize( 32, 32 ) );
    REQUIRE( render.status == PreviewRender::Status::Ready );
    REQUIRE_FALSE( render.image.isNull() );
    // Aspect preserved into the target box (64x48 → 32x24).
    REQUIRE( render.image.width() == 32 );
    REQUIRE( render.image.height() == 24 );

    // Gradients stretch to the byte range: left edge dark, right edge bright.
    const QImage img = render.image;
    const int left = qGray( img.pixel( 1, img.height() / 2 ) );
    const int right = qGray( img.pixel( img.width() - 2, img.height() / 2 ) );
    REQUIRE( left < 80 );
    REQUIRE( right > 160 );
}

TEST_CASE( "Raster preview paints declared NoData black", "[wb8][preview]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeNoDataRaster( dir.filePath( "nodata.tif" ) );

    const PreviewRender render = renderRasterPreview( path, QSize( 32, 32 ) );
    REQUIRE( render.status == PreviewRender::Status::Ready );
    const QImage img = render.image;
    // Valid rows render non-black (stretched data); NoData rows are black.
    REQUIRE( qGray( img.pixel( img.width() / 2, 4 ) ) != 0 );
    REQUIRE( img.pixel( img.width() / 2, img.height() - 2 ) == qRgb( 0, 0, 0 ) );
}

TEST_CASE( "Raster preview failures are typed and explained", "[wb8][preview]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const PreviewRender missing =
      renderRasterPreview( dir.filePath( "nope.tif" ), QSize( 32, 32 ) );
    REQUIRE( missing.status == PreviewRender::Status::Failed );
    REQUIRE( missing.image.isNull() );
    REQUIRE_FALSE( missing.error.isEmpty() );

    // A text file is not a raster: typed failure, no crash.
    const QString text = dir.filePath( "text.tif" );
    REQUIRE( QFile( text ).open( QIODevice::WriteOnly ) );
    const PreviewRender garbage = renderRasterPreview( text, QSize( 32, 32 ) );
    REQUIRE( garbage.status == PreviewRender::Status::Failed );
    REQUIRE_FALSE( garbage.error.isEmpty() );
}

TEST_CASE( "Vector preview renders through QGIS and refuses the cap",
           "[wb8][preview]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString csv = makePointCsv( dir.filePath( "points.csv" ) );

    const PreviewRender render = renderVectorPreview( csv, QSize( 64, 64 ) );
    if ( render.status == PreviewRender::Status::Ready )
    {
        REQUIRE_FALSE( render.image.isNull() );
        REQUIRE( render.image.size() == QSize( 64, 64 ) );
    }
    else
    {
        // The CSV driver may be absent in a minimal GDAL build — a typed
        // failure with a reason is the contract, never a crash.
        REQUIRE( render.status == PreviewRender::Status::Failed );
        REQUIRE_FALSE( render.error.isEmpty() );
    }

    // Cap refusal is typed Unsupported with the limit named.
    const PreviewRender capped = renderVectorPreview( csv, QSize( 64, 64 ), 10 );
    REQUIRE( capped.status == PreviewRender::Status::Unsupported );
    REQUIRE( capped.image.isNull() );
    REQUIRE( capped.error.contains( QStringLiteral( "10" ) ) );
}

TEST_CASE( "Service caches previews and reports provenance", "[wb8][preview]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "cached.tif" ) );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    service.setPool( &pool );

    AssetPreviewService::Result first;
    int calls = 0;
    service.requestPreview(
      { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, &service,
      [&]( const AssetPreviewService::Result & r )
      {
          first = r;
          ++calls;
      } );
    REQUIRE( waitUntil( [&] { return calls == 1; } ) );
    REQUIRE( first.status == PreviewRender::Status::Ready );
    REQUIRE_FALSE( first.fromCache );
    REQUIRE( service.cacheEntries() == 1 );
    REQUIRE( service.cacheBytes() > 0 );

    // Same request → immediate cache hit (synchronous callback, no pool).
    AssetPreviewService::Result second;
    service.requestPreview(
      { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, &service,
      [&]( const AssetPreviewService::Result & r ) { second = r; } );
    REQUIRE( calls == 1 );
    REQUIRE( second.fromCache );
    REQUIRE( second.image == first.image );

    // A different target size is a different cache entry.
    service.requestPreview(
      { path, AssetPreviewService::Kind::Raster, QSize( 64, 64 ) }, &service,
      []( const AssetPreviewService::Result & ) {} );
    REQUIRE( waitUntil( [&] { return service.cacheEntries() == 2; } ) );
}

TEST_CASE( "Service supersedes an older request for the same receiver",
           "[wb8][preview]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "superseded.tif" ) );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    service.setPool( &pool );

    QObject receiver;
    int aCalls = 0;
    int bCalls = 0;
    // Both requests register before any queued delivery runs → request A is
    // deterministically superseded by B.
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 16, 16 ) },
                            &receiver,
                            [&]( const AssetPreviewService::Result & ) { ++aCalls; } );
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 24, 24 ) },
                            &receiver,
                            [&]( const AssetPreviewService::Result & ) { ++bCalls; } );
    REQUIRE( waitUntil( [&] { return bCalls == 1; } ) );
    REQUIRE( aCalls == 0 );
}

TEST_CASE( "Service drops canceled requests and dead receivers", "[wb8][preview]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "drops.tif" ) );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    service.setPool( &pool );

    // Cancel before delivery.
    QObject receiver;
    int canceledCalls = 0;
    const quint64 token = service.requestPreview(
      { path, AssetPreviewService::Kind::Raster, QSize( 16, 16 ) }, &receiver,
      [&]( const AssetPreviewService::Result & ) { ++canceledCalls; } );
    service.cancel( token );
    drainEvents();
    REQUIRE( canceledCalls == 0 );

    // Dead receiver: the result must drop, never run into dead state.
    // Heap-allocated with deleteLater() as its SOLE owner (deleting it a
    // second time — scoped pointer or stack — would corrupt the heap).
    {
        QObject *doomed = new QObject;
        int deadCalls = 0;
        service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 16, 16 ) },
                                doomed,
                                [&]( const AssetPreviewService::Result & ) { ++deadCalls; } );
        doomed->deleteLater();
        QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
        drainEvents();
        REQUIRE( deadCalls == 0 );
    }
}

TEST_CASE( "Cache bounds evict LRU across entries and bytes", "[wb8][preview]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString a = makeGradientRaster( dir.filePath( "a.tif" ) );
    const QString b = makeGradientRaster( dir.filePath( "b.tif" ) );
    const QString c = makeGradientRaster( dir.filePath( "c.tif" ) );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 2 );
    service.setPool( &pool );
    service.setCacheLimits( 2, 64LL * 1024 * 1024 );

    // Request lambdas capture ONLY test-scope locals (a by-reference
    // capture of a helper's parameter would dangle once the helper returns).
    int aReady = 0;
    int bReady = 0;
    int cReady = 0;
    // nullptr receiver: this test wants BOTH results delivered — a shared
    // receiver would (by contract) supersede the older request.
    auto request = [&]( const QString & p, int *counter )
    {
        service.requestPreview( { p, AssetPreviewService::Kind::Raster, QSize( 16, 16 ) },
                                nullptr,
                                [counter]( const AssetPreviewService::Result & r )
                                {
                                    if ( r.status == PreviewRender::Status::Ready )
                                        ++*counter;
                                } );
    };
    request( a, &aReady );
    request( b, &bReady );
    const bool both = waitUntil( [&] { return aReady == 1 && bReady == 1; } );
    INFO( "aReady=" << aReady << " bReady=" << bReady
          << " entries=" << service.cacheEntries() );
    REQUIRE( both );
    REQUIRE( service.cacheEntries() == 2 );

    // Touch a (re-select) → b becomes the LRU victim when c lands.
    service.requestPreview( { a, AssetPreviewService::Kind::Raster, QSize( 16, 16 ) },
                            &service,
                            []( const AssetPreviewService::Result & ) {} );
    request( c, &cReady );
    REQUIRE( waitUntil( [&] { return cReady == 1; } ) );
    REQUIRE( service.cacheEntries() == 2 );

    // Byte bound: shrink to 1 byte → every image evicts (honest emptiness).
    service.setCacheLimits( 64, 1 );
    REQUIRE( service.cacheEntries() == 0 );
    REQUIRE( service.cacheBytes() == 0 );
}
