// test_r33_t06_preview_identity.cpp — R33-T06 WP02/WP03/WP04/WP05
//
// Pins the three preview contracts this track changed:
//  * WP02 generation identity: the cache key is captured at REQUEST time and
//    a render is only admitted to the cache while that identity still
//    describes the file. Pixels read from generation A can therefore never be
//    served later under generation B (the barrier below replaces the file
//    after the worker has read it and before the result is delivered).
//  * WP03 non-finite display rule: ±Inf and NaN are display misses exactly
//    like NoData. They neither widen the per-band stretch (which used to
//    collapse every finite pixel to 0) nor reach the float→byte conversion
//    (which is UB for NaN).
//  * WP04 bounded pending work: a burst of selections leaves ONE registered
//    request, not one per click, because supersede/cancel abort the queued
//    worker before it reads pixels.
// Every fixture is a real GDAL raster generated into a QTemporaryDir; the
// barrier uses a single-thread pool plus a fence task, so no timing is
// involved.
#include <catch2/catch_test_macros.hpp>

#include <QAtomicInt>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <gdal_priv.h>
#include <qgsapplication.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

#include "app/preview/asset_preview_service.h"
#include "geospatial/raster/raster_reader.h"

namespace
{

void ensureQgisApplication()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char applicationName[] = "test_r33_t06_preview_identity";
    static char *argv[] = { applicationName, nullptr };
    new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
    GDALAllRegister();
}

/// One-band raster written from an explicit sample vector (row-major).
QString makeBandRaster( const QString &path, const std::vector<double> &values, int w, int h,
                        GDALDataType dtype, bool hasNoData = false, double noData = 0.0 )
{
    REQUIRE( static_cast<int>( values.size() ) == w * h );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), w, h, 1, dtype, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( h ), 0.0, -1.0 };
    ds->SetGeoTransform( gt );
    GDALRasterBand *band = ds->GetRasterBand( 1 );
    if ( hasNoData )
        band->SetNoDataValue( noData );
    std::vector<double> row( static_cast<std::size_t>( w ) );
    for ( int y = 0; y < h; ++y )
    {
        for ( int x = 0; x < w; ++x )
            row[static_cast<std::size_t>( x )] = values[static_cast<std::size_t>( y ) * w + x];
        REQUIRE( band->RasterIO( GF_Write, 0, y, w, 1, row.data(), w, 1, GDT_Float64, 0, 0 )
                 == CE_None );
    }
    GDALClose( ds );
    return path;
}

/// 3-band Byte GeoTIFF with a horizontal gradient per band (A-generation A/B
/// fixture). Distinct sizes make the two generations trivially separable.
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

/// 3-band Byte raster whose THIRD band declares NoData 255 and carries it in
/// column 0 only — the per-band NoData mask must blacken that pixel while the
/// neighbouring columns keep their colour.
QString makePerBandNoDataRaster( const QString &path )
{
    constexpr int W = 4, H = 1;
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), W, H, 3, GDT_Byte, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
    ds->SetGeoTransform( gt );
    const double band1[W] = { 10.0, 20.0, 30.0, 40.0 };
    const double band2[W] = { 10.0, 20.0, 30.0, 40.0 };
    const double band3[W] = { 255.0, 100.0, 100.0, 100.0 };
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, W, H, const_cast<double *>( band1 ), W, H,
                                     GDT_Float64, 0, 0 );
    ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, 0, W, H, const_cast<double *>( band2 ), W, H,
                                     GDT_Float64, 0, 0 );
    GDALRasterBand *third = ds->GetRasterBand( 3 );
    third->SetNoDataValue( 255.0 );
    third->RasterIO( GF_Write, 0, 0, W, H, const_cast<double *>( band3 ), W, H, GDT_Float64, 0, 0 );
    GDALClose( ds );
    return path;
}

/// Pumps queued events until @p done holds or ~3 s elapse (bounded).
bool waitUntil( const std::function<bool()> &done )
{
    for ( int i = 0; i < 300 && !done(); ++i )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 5 );
    }
    return done();
}

/// Pumps queued events for a fixed short window (delivery draining).
void drainEvents( int ms = 400 )
{
    QElapsedTimer timer;
    timer.start();
    while ( !timer.hasExpired( ms ) )
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
}

/// Runs @p work on the pool's single worker and blocks until it has, i.e. the
/// caller knows every task submitted BEFORE the fence has finished. Combined
/// with setMaxThreadCount(1) this is a deterministic ordering barrier: the
/// preview worker has posted its result but the service has not consumed it
/// (we never spin the event loop here).
void fence( QThreadPool &pool )
{
    QAtomicInt done( 0 );
    pool.start( [&done]() { done.storeRelease( 1 ); } );
    while ( done.loadAcquire() == 0 )
        QThread::msleep( 1 );
    pool.waitForDone( 50 );
}

} // namespace

using namespace sicnu::app;

// ---------------------------------------------------------------------------
// WP03 — non-finite samples are display misses, never range inputs
// ---------------------------------------------------------------------------

TEST_CASE( "R33-T06 WP03: +Inf/-Inf/NaN/NoData are display misses and do not "
           "widen the stretch",
           "[r33][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Columns 0..9 ramp 10..100 (the real data), then +Inf, -Inf, NaN,
    // declared NoData and two more finite samples.
    std::vector<double> values( 16, 0.0 );
    for ( int x = 0; x < 10; ++x )
        values[static_cast<std::size_t>( x )] = 10.0 * ( x + 1 );
    values[10] = std::numeric_limits<double>::infinity();
    values[11] = -std::numeric_limits<double>::infinity();
    values[12] = std::numeric_limits<double>::quiet_NaN();
    values[13] = -9999.0;
    values[14] = 55.0;
    values[15] = 100.0;
    const QString path = makeBandRaster( dir.filePath( "specials.tif" ), values, 16, 1,
                                         GDT_Float32, true, -9999.0 );

    const PreviewRender render = renderRasterPreview( path, QSize( 16, 16 ) );
    REQUIRE( render.status == PreviewRender::Status::Ready );
    REQUIRE( render.image.size() == QSize( 16, 1 ) );
    const QImage img = render.image;

    // The stretch is over the FINITE range [10,100]: the mid sample is the
    // neutral 128. With an infinity in the range (the pre-fix behaviour)
    // every finite sample collapses to the same endpoint value.
    REQUIRE( img.pixel( 14, 0 ) == qRgb( 128, 128, 128 ) );
    REQUIRE( qGray( img.pixel( 0, 0 ) ) == 0 );
    REQUIRE( qGray( img.pixel( 9, 0 ) ) == 255 );
    REQUIRE( qGray( img.pixel( 15, 0 ) ) == 255 );

    // Typed display rule: all four non-displayable samples are black.
    for ( int x : { 10, 11, 12, 13 } )
    {
        INFO( "column " << x );
        REQUIRE( img.pixel( x, 0 ) == qRgb( 0, 0, 0 ) );
    }
}

TEST_CASE( "R33-T06 WP03: Float64 -Inf does not poison the byte conversion",
           "[r33][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Pre-fix, a -Inf minimum made t = Inf/Inf = NaN and the float→quint8
    // cast was undefined behaviour; the finite samples now stretch normally.
    std::vector<double> values( 8, 0.0 );
    values[0] = -std::numeric_limits<double>::infinity();
    for ( int x = 1; x < 8; ++x )
        values[static_cast<std::size_t>( x )] = 10.0 * x;
    const QString path = makeBandRaster( dir.filePath( "dinf.tif" ), values, 8, 1, GDT_Float64 );

    const PreviewRender render = renderRasterPreview( path, QSize( 8, 8 ) );
    REQUIRE( render.status == PreviewRender::Status::Ready );
    const QImage img = render.image;
    REQUIRE( img.size() == QSize( 8, 1 ) );
    REQUIRE( img.pixel( 0, 0 ) == qRgb( 0, 0, 0 ) );   // -Inf → miss
    REQUIRE( qGray( img.pixel( 1, 0 ) ) == 0 );        // 10 == finite min
    REQUIRE( qGray( img.pixel( 7, 0 ) ) == 255 );      // 70 == finite max
    REQUIRE( img.pixel( 4, 0 ) == qRgb( 128, 128, 128 ) ); // 40 is the midpoint
}

TEST_CASE( "R33-T06 WP03: all-missing and flat rasters stay honest",
           "[r33][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // No sample is displayable: black everywhere, still a Ready image (a
    // preview, not a fabrication) and no crash.
    const std::vector<double> missing( 16, std::numeric_limits<double>::quiet_NaN() );
    const QString allMiss =
      makeBandRaster( dir.filePath( "allmiss.tif" ), missing, 4, 4, GDT_Float32 );
    const PreviewRender miss = renderRasterPreview( allMiss, QSize( 4, 4 ) );
    REQUIRE( miss.status == PreviewRender::Status::Ready );
    REQUIRE( miss.image.size() == QSize( 4, 4 ) );
    REQUIRE( miss.image.pixel( 2, 2 ) == qRgb( 0, 0, 0 ) );

    // Flat data has no range: neutral gray, never a black box.
    const std::vector<double> flat( 16, 42.0 );
    const QString flatPath =
      makeBandRaster( dir.filePath( "flat.tif" ), flat, 4, 4, GDT_Float32 );
    const PreviewRender flatRender = renderRasterPreview( flatPath, QSize( 4, 4 ) );
    REQUIRE( flatRender.status == PreviewRender::Status::Ready );
    REQUIRE( flatRender.image.pixel( 2, 2 ) == qRgb( 128, 128, 128 ) );
}

TEST_CASE( "R33-T06 WP03: a legitimate 1e308 span still stretches",
           "[r33][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Both samples are finite and legitimate Float64 data, but
    // max - min overflows to +Inf. The half-range form keeps the denominator
    // finite; the naive form makes every ratio NaN and the preview black.
    std::vector<double> values( 8, 0.0 );
    values[0] = -1e308;
    values[1] = -5e307;
    values[2] = -1.0;
    values[3] = 0.0;
    values[4] = 1.0;
    values[5] = 5e307;
    values[6] = 7.5e307;
    values[7] = 1e308;
    const QString path = makeBandRaster( dir.filePath( "wide.tif" ), values, 8, 1, GDT_Float64 );

    const PreviewRender render = renderRasterPreview( path, QSize( 8, 8 ) );
    REQUIRE( render.status == PreviewRender::Status::Ready );
    const QImage img = render.image;
    REQUIRE( img.size() == QSize( 8, 1 ) );
    REQUIRE( qGray( img.pixel( 0, 0 ) ) == 0 );
    REQUIRE( qGray( img.pixel( 7, 0 ) ) == 255 );
    // 0.0 sits at (0 - (-1e308)) / 2e308 = 0.5 of the span.
    REQUIRE( img.pixel( 3, 0 ) == qRgb( 128, 128, 128 ) );
}

TEST_CASE( "R33-T06 WP03: the NoData predicate narrows Float32 like the reader does",
           "[r33][t06][preview][wp03]" )
{
    // The preview used a raw `v == band.noDataValue` on the double value the
    // window read returned. For a Float32 band whose sentinel is not
    // representable (0.1), the stored pixel comes back as (double)0.1f, which
    // is NOT equal to the double 0.1 that GDAL reports for the band — so a
    // literal stored at the sentinel value was displayed as data and widened
    // the stretch.
    //
    // A GTiff fixture cannot demonstrate this: on open GDAL stores
    // m_dfNoDataValue already narrowed to float, so the old comparison matches
    // too (found in review R2). The predicate is the seam, and it is public —
    // sicnu::geo::bandSentinelMatches is documented as "the one NoData comparison
    // authority" — so the contract is pinned directly.
    sicnu::geo::BandInfo band;
    band.dtype = "Float32";
    band.hasNoData = true;
    band.noDataValue = 0.1;             // what a caller/GDAL reports as double
    const double stored = static_cast<double>( 0.1f ); // what a Float32 read returns

    REQUIRE( stored != 0.1 );                       // the trap this guards against
    REQUIRE( sicnu::geo::bandSentinelMatches( band, stored ) );

    // ... and a genuinely different Float32 sample is not a sentinel match.
    REQUIRE_FALSE( sicnu::geo::bandSentinelMatches( band, static_cast<double>( 0.2f ) ) );

    // Float64 keeps exact semantics.
    sicnu::geo::BandInfo wide;
    wide.dtype = "Float64";
    wide.hasNoData = true;
    wide.noDataValue = 0.1;
    REQUIRE_FALSE( sicnu::geo::bandSentinelMatches( wide, stored ) );
    REQUIRE( sicnu::geo::bandSentinelMatches( wide, 0.1 ) );

    // NaN sentinels match NaN and nothing else.
    sicnu::geo::BandInfo nanBand;
    nanBand.dtype = "Float32";
    nanBand.hasNoData = true;
    nanBand.noDataIsNaN = true;
    nanBand.noDataValue = std::numeric_limits<double>::quiet_NaN();
    REQUIRE( sicnu::geo::bandSentinelMatches( nanBand, std::numeric_limits<double>::quiet_NaN() ) );
    REQUIRE_FALSE( sicnu::geo::bandSentinelMatches( nanBand, 0.0 ) );
}

TEST_CASE( "R33-T06 WP03: a non-finite sample in one RGB channel blackens the pixel",
           "[r33][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    constexpr int W = 4, H = 1;
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    const QString path = dir.filePath( "rgb_inf.tif" );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), W, H, 3, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( H ), 0.0, -1.0 };
    ds->SetGeoTransform( gt );
    double band1[W] = { 10.0, 20.0, 30.0, 40.0 };
    double band2[W] = { std::numeric_limits<double>::infinity(), 20.0, 30.0, 40.0 };
    double band3[W] = { 50.0, 50.0, 50.0, 50.0 };
    ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, W, H, band1, W, H, GDT_Float64, 0, 0 );
    ds->GetRasterBand( 2 )->RasterIO( GF_Write, 0, 0, W, H, band2, W, H, GDT_Float64, 0, 0 );
    ds->GetRasterBand( 3 )->RasterIO( GF_Write, 0, 0, W, H, band3, W, H, GDT_Float64, 0, 0 );
    GDALClose( ds );

    const PreviewRender render = renderRasterPreview( path, QSize( W, W ) );
    REQUIRE( render.status == PreviewRender::Status::Ready );
    const QImage img = render.image;
    REQUIRE( img.size() == QSize( W, 1 ) );
    // Column 0 carries +Inf in the green band: the per-band mask blackens the
    // whole pixel, and the infinity does not widen band 2's range either —
    // column 3 (40) is still the channel maximum.
    REQUIRE( img.pixel( 0, 0 ) == qRgb( 0, 0, 0 ) );
    REQUIRE( qGreen( img.pixel( 3, 0 ) ) == 255 );
    REQUIRE( qRed( img.pixel( 3, 0 ) ) == 255 );
}

TEST_CASE( "R33-T06 WP03: a subnormal-only span still stretches",
           "[r33][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    // Regression guard for the overflow fix itself: halving first would round
    // both sides of a denormal span to zero and blank the image, so the direct
    // form must stay in charge whenever max - min is representable.
    std::vector<double> values( 4, 0.0 );
    values[0] = 0.0;
    values[1] = std::numeric_limits<double>::denorm_min();
    values[2] = 2.0 * std::numeric_limits<double>::denorm_min();
    values[3] = 4.0 * std::numeric_limits<double>::denorm_min();
    const QString path = makeBandRaster( dir.filePath( "denorm.tif" ), values, 4, 1,
                                         GDT_Float64 );
    const PreviewRender render = renderRasterPreview( path, QSize( 4, 4 ) );
    REQUIRE( render.status == PreviewRender::Status::Ready );
    const QImage img = render.image;
    REQUIRE( qGray( img.pixel( 0, 0 ) ) == 0 );
    REQUIRE( qGray( img.pixel( 3, 0 ) ) == 255 );
}

TEST_CASE( "R33-T06 WP03: each band masks with its own NoData",
           "[r33][t06][preview][wp03]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makePerBandNoDataRaster( dir.filePath( "perband.tif" ) );

    const PreviewRender render = renderRasterPreview( path, QSize( 4, 4 ) );
    REQUIRE( render.status == PreviewRender::Status::Ready );
    const QImage img = render.image;
    REQUIRE( img.size() == QSize( 4, 1 ) );
    // Column 0 carries band 3's declared NoData → the pixel blackens even
    // though bands 1/2 have data there.
    REQUIRE( img.pixel( 0, 0 ) == qRgb( 0, 0, 0 ) );
    // Column 2 has data in all three bands and keeps a non-black colour.
    REQUIRE( qRed( img.pixel( 2, 0 ) ) > 100 );
}

// ---------------------------------------------------------------------------
// WP02 — generation identity across a replaced file
// ---------------------------------------------------------------------------

TEST_CASE( "R33-T06 WP02: a file replaced mid-render is never cached under the "
           "new identity",
           "[r33][t06][preview][wp02]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "swap.tif" ), 64, 48 );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 ); // FIFO: the fence runs after the preview task
    service.setPool( &pool );

    int calls = 0;
    AssetPreviewService::Result first;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 64, 64 ) }, nullptr,
                            [&]( const AssetPreviewService::Result &r )
                            {
                                first = r;
                                ++calls;
                            } );
    REQUIRE( service.pendingRequests() == 1 );

    // Barrier: the worker has READ generation A and marshaled the result,
    // but the service has not consumed it yet (no event loop spin).
    fence( pool );
    REQUIRE( calls == 0 );

    // Generation B replaces A at the same path (different pixels AND size).
    // The size change is enough to move the identity; setFileTime() is avoided
    // on purpose — some filesystems refuse a timestamp write here, and the
    // fixture must not depend on that.
    const qint64 sizeBefore = QFileInfo( path ).size();
    makeGradientRaster( path, 32, 16 );
    REQUIRE( QFileInfo( path ).size() != sizeBefore );

    drainEvents();
    REQUIRE( calls == 1 );
    REQUIRE( first.status == PreviewRender::Status::Ready );
    // The delivered image is A's render, and it is flagged as such.
    REQUIRE( first.image.size() == QSize( 64, 48 ) );
    REQUIRE( first.identityChanged );
    // A's pixels must NOT have entered the cache — no slot at all, because
    // the captured identity no longer describes the file.
    REQUIRE( service.cacheEntries() == 0 );
    REQUIRE( service.cacheBytes() == 0 );

    // The next request reads generation B for real (no A pixels served).
    AssetPreviewService::Result second;
    int secondCalls = 0;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 64, 64 ) }, nullptr,
                            [&]( const AssetPreviewService::Result &r )
                            {
                                second = r;
                                ++secondCalls;
                            } );
    REQUIRE( waitUntil( [&] { return secondCalls == 1; } ) );
    REQUIRE( second.status == PreviewRender::Status::Ready );
    REQUIRE_FALSE( second.fromCache );
    REQUIRE_FALSE( second.identityChanged );
    REQUIRE( second.image.size() == QSize( 32, 16 ) );
    REQUIRE( service.cacheEntries() == 1 );
}

TEST_CASE( "R33-T06 WP02: an unchanged file still caches at request identity",
           "[r33][t06][preview][wp02]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "stable.tif" ), 64, 48 );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    service.setPool( &pool );

    int calls = 0;
    AssetPreviewService::Result first;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
                            [&]( const AssetPreviewService::Result &r )
                            {
                                first = r;
                                ++calls;
                            } );
    REQUIRE( waitUntil( [&] { return calls == 1; } ) );
    REQUIRE_FALSE( first.fromCache );
    REQUIRE_FALSE( first.identityChanged );
    REQUIRE( service.cacheEntries() == 1 );

    // Same identity → synchronous cache hit, no second render.
    AssetPreviewService::Result second;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
                            [&]( const AssetPreviewService::Result &r ) { second = r; } );
    REQUIRE( second.fromCache );
    REQUIRE( second.image == first.image );
    REQUIRE( service.pendingRequests() == 0 );
}

TEST_CASE( "R33-T06 WP02: a same-size rewrite is invalidated by its timestamp",
           "[r33][t06][preview][wp02]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "same_size.tif" ), 64, 48 );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    service.setPool( &pool );

    int calls = 0;
    AssetPreviewService::Result first;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
                            [&]( const AssetPreviewService::Result &r )
                            {
                                first = r;
                                ++calls;
                            } );
    REQUIRE( waitUntil( [&] { return calls == 1; } ) );
    REQUIRE( service.cacheEntries() == 1 );
    const qint64 sizeBefore = QFileInfo( path ).size();
    const QDateTime mtimeBefore = QFileInfo( path ).lastModified();

    // Rewrite in place at the same dimensions. Byte length stays identical for
    // an uncompressed GTiff, so only the mtime can invalidate the entry —
    // which is exactly the identity component this case pins.
    QThread::msleep( 25 ); // mtime resolution is milliseconds
    makeGradientRaster( path, 64, 48 );

    const QFileInfo after( path );
    if ( after.size() == sizeBefore )
    {
        INFO( "same byte length — the timestamp alone must move the identity" );
        REQUIRE( after.lastModified() != mtimeBefore );
    }

    AssetPreviewService::Result second;
    int secondCalls = 0;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
                            [&]( const AssetPreviewService::Result &r )
                            {
                                second = r;
                                ++secondCalls;
                            } );
    REQUIRE( waitUntil( [&] { return secondCalls == 1; } ) );
    REQUIRE( second.status == PreviewRender::Status::Ready );
    REQUIRE_FALSE( second.fromCache ); // the timestamp moved, so it re-read
}

TEST_CASE( "R33-T06 WP02: Unicode paths and a deleted file keep identity honest",
           "[r33][t06][preview][wp02]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString sub = dir.filePath( QString::fromUtf8( "中文目录" ) );
    REQUIRE( QDir().mkpath( sub ) );
    const QString path = makeGradientRaster( sub + QStringLiteral( "/图层.tif" ), 64, 48 );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    service.setPool( &pool );

    int calls = 0;
    AssetPreviewService::Result result;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
                            [&]( const AssetPreviewService::Result &r )
                            {
                                result = r;
                                ++calls;
                            } );
    fence( pool );
    REQUIRE( QFile::remove( path ) );
    drainEvents();
    REQUIRE( calls == 1 );
    REQUIRE( result.identityChanged );
    REQUIRE( service.cacheEntries() == 0 );
    // The caller's spelling is echoed back, not the normalized key.
    REQUIRE( result.path == path );

    // A request for the now-missing file is a typed failure, still uncached.
    int missingCalls = 0;
    AssetPreviewService::Result missing;
    service.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
                            [&]( const AssetPreviewService::Result &r )
                            {
                                missing = r;
                                ++missingCalls;
                            } );
    REQUIRE( waitUntil( [&] { return missingCalls == 1; } ) );
    REQUIRE( missing.status == PreviewRender::Status::Failed );
    REQUIRE_FALSE( missing.error.isEmpty() );
    REQUIRE( service.cacheEntries() == 0 );
}

// ---------------------------------------------------------------------------
// WP04 — bounded pending work under a selection burst
// ---------------------------------------------------------------------------

TEST_CASE( "R33-T06 WP04: a selection burst leaves one pending request and one "
           "delivery",
           "[r33][t06][preview][wp04]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "burst.tif" ), 64, 48 );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 2 );
    service.setPool( &pool );
    QObject receiver;

    int calls = 0;
    QSize lastSize;
    AssetPreviewService::Result delivered;
    for ( int i = 0; i < 1000; ++i )
    {
        const QSize size( 16 + ( i % 8 ), 16 );
        lastSize = size;
        service.requestPreview( { path, AssetPreviewService::Kind::Raster, size }, &receiver,
                                [&]( const AssetPreviewService::Result &r )
                                {
                                    delivered = r;
                                    ++calls;
                                } );
        // One receiver → at most one registered request. Deliberately checked
        // on every iteration: the registry must never grow with click rate.
        REQUIRE( service.pendingRequests() <= 1 );
    }
    REQUIRE( service.pendingRequests() == 1 );

    REQUIRE( waitUntil( [&] { return calls == 1; } ) );
    drainEvents( 100 );
    REQUIRE( calls == 1 ); // superseded requests never deliver
    REQUIRE( service.pendingRequests() == 0 );
    REQUIRE( service.cacheEntries() == 1 );
    // The delivery belongs to the LAST request, not to the first one that
    // happened to win a worker thread.
    REQUIRE( delivered.requestedSize == lastSize );
    REQUIRE( delivered.status == PreviewRender::Status::Ready );
}

TEST_CASE( "R33-T06 WP04: a canceled request neither delivers nor caches",
           "[r33][t06][preview][wp04]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "cancel.tif" ), 64, 48 );

    AssetPreviewService service;
    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    service.setPool( &pool );

    int calls = 0;
    const quint64 token = service.requestPreview(
      { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
      [&]( const AssetPreviewService::Result & ) { ++calls; } );
    REQUIRE( service.pendingRequests() == 1 );
    service.cancel( token );
    REQUIRE( service.pendingRequests() == 0 );
    drainEvents();
    REQUIRE( calls == 0 );
    REQUIRE( service.cacheEntries() == 0 );

    // Cancel of an unknown/已 delivered token is a no-op, not a crash.
    service.cancel( token );
    service.cancel( 999999 );
    REQUIRE( service.pendingRequests() == 0 );
}

// ---------------------------------------------------------------------------
// WP05 — teardown with work in flight
// ---------------------------------------------------------------------------

TEST_CASE( "R33-T06 WP05: destroying the service with work in flight is safe",
           "[r33][t06][preview][wp05]" )
{
    ensureQgisApplication();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = makeGradientRaster( dir.filePath( "teardown.tif" ), 64, 48 );

    QThreadPool pool;
    pool.setMaxThreadCount( 1 );
    {
        auto *service = new AssetPreviewService;
        service->setPool( &pool );
        service->requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) },
                                 nullptr, []( const AssetPreviewService::Result & ) {} );
        delete service; // dtor while the worker is queued/computing
    }
    pool.waitForDone();
    drainEvents( 100 );

    // The pool is still usable and a fresh service renders normally.
    AssetPreviewService fresh;
    fresh.setPool( &pool );
    int calls = 0;
    fresh.requestPreview( { path, AssetPreviewService::Kind::Raster, QSize( 32, 32 ) }, nullptr,
                          [&]( const AssetPreviewService::Result &r )
                          {
                              REQUIRE( r.status == PreviewRender::Status::Ready );
                              ++calls;
                          } );
    REQUIRE( waitUntil( [&] { return calls == 1; } ) );
}
