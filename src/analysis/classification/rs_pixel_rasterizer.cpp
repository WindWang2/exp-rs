// rs_pixel_rasterizer.cpp — see header for design notes.
#include "rs_pixel_rasterizer.h"

#include <gdal_alg.h>
#include <gdal_priv.h>
#include <ogr_api.h>

#include "cpl_error.h"

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <vector>

// Test-only failure injection hooks (#1056). Null by default in production.
bool ( *RsPixelRasterizer::sForceRasterizeGeometriesFailure )() = nullptr;
bool ( *RsPixelRasterizer::sForceRasterIOFailure )() = nullptr;

namespace
{

/// One burn window: columns [col0, col1] and rows [rowStart, rowEnd) of the
/// geometry's bbox window. A window of the whole bbox is the legacy
/// single-shot burn; smaller row chunks bound the MEM band.
struct BurnWindow
{
    int col0 = 0;
    int col1 = 0; // inclusive
    int rowStart = 0;
    int rowEnd = 0; // exclusive
};

/// R30-T01 (WP04): adapter that exposes the cancel hook as a GDAL progress
/// callback. Returning FALSE aborts the burn; GDAL then reports CE_Failure
/// with CPLE_UserInterrupt, which the caller reclassifies as cancellation so
/// a user abort never reads as an infrastructure error.
struct GDALProgressCancelHook
{
    const std::function<bool()> *isCanceled = nullptr;

    static int CPL_STDCALL call( double, const char *, void *self )
    {
        const auto *hook = static_cast<const GDALProgressCancelHook *>( self );
        return hook->isCanceled && ( *hook->isCanceled )() ? 0 : 1;
    }
};

/// Burn `ogrGeom` into a fresh MEM dataset covering `window` and scan the
/// band into `out`. Returns false on infrastructure failure or cancellation
/// (with *cancelled set) — handles are closed on every exit path.
bool burnAndCollectWindow( const OGRGeometryH ogrGeom,
                           const double gt[6],
                           int W,
                           int H,
                           const BurnWindow &window,
                           const std::function<bool()> &isCanceled,
                           QSet<quint64> &out,
                           bool *cancelled )
{
    (void)H;
    const int winW = window.col1 - window.col0 + 1;
    const int winH = window.rowEnd - window.rowStart;
    if ( winW <= 0 || winH <= 0 )
        return true; // nothing to burn in this window (exact empty)

    GDALAllRegister(); // idempotent; the MEM driver must exist in this process
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "MEM" );
    if ( !drv )
        return false;
    GDALDataset *ds = drv->Create( "", winW, winH, 1, GDT_Byte, nullptr );
    if ( !ds )
        return false;

    // Window geotransform: origin at the window's top-left corner in map
    // space; the affine terms are inherited unchanged (rotation/shear safe).
    const double winGt[6] = {
        gt[0] + gt[1] * window.col0 + gt[2] * window.rowStart,
        gt[1],
        gt[2],
        gt[3] + gt[4] * window.col0 + gt[5] * window.rowStart,
        gt[4],
        gt[5],
    };
    if ( ds->SetGeoTransform( const_cast<double *>( winGt ) ) != CE_None )
    {
        GDALClose( ds );
        return false;
    }
    // Fill is what makes "never burned" distinguishable from stale memory —
    // a failed Fill means the scan below would read undefined bytes.
    if ( ds->GetRasterBand( 1 )->Fill( 0 ) != CE_None )
    {
        GDALClose( ds );
        return false;
    }

    // An empty coverage and a failed rasterization are indistinguishable in
    // the burned band; only the API result tells them apart (#1056).
    if ( RsPixelRasterizer::sForceRasterizeGeometriesFailure
         && RsPixelRasterizer::sForceRasterizeGeometriesFailure() )
    {
        GDALClose( ds );
        return false;
    }
    int bands[1] = { 1 };
    double burnValues[1] = { 1.0 };
    OGRGeometryH geoms[1] = { ogrGeom };
    const GDALProgressCancelHook progressHook{ &isCanceled };
    GDALProgressFunc progressFn = nullptr;
    void *progressArg = nullptr;
    if ( isCanceled )
    {
        progressFn = &GDALProgressCancelHook::call;
        progressArg = const_cast<void *>( static_cast<const void *>( &progressHook ) );
    }
    const CPLErr rasterizeErr = GDALRasterizeGeometries( ds,
                         1, bands,
                         1, geoms,
                         nullptr, nullptr,
                         burnValues,
                         nullptr,
                         progressFn,
                         progressArg );
    if ( rasterizeErr == CE_Failure )
    {
        // CPLE_UserInterrupt is the documented marker for a progress-callback
        // abort — distinct from a real rasterization failure.
        if ( isCanceled && CPLGetLastErrorNo() == CPLE_UserInterrupt )
            *cancelled = true;
        GDALClose( ds );
        return false;
    }

    std::vector<uint8_t> buf( static_cast<size_t>( winW ) );
    GDALRasterBand *band = ds->GetRasterBand( 1 );
    int rowsSinceCancelCheck = 0;
    for ( int y = 0; y < winH; ++y )
    {
        if ( RsPixelRasterizer::sForceRasterIOFailure && RsPixelRasterizer::sForceRasterIOFailure() )
        {
            GDALClose( ds );
            return false;
        }
        const CPLErr readErr = band->RasterIO( GF_Read, 0, y, winW, 1,
                                               buf.data(), winW, 1, GDT_Byte, 0, 0 );
        if ( readErr == CE_Failure )
        {
            GDALClose( ds );
            return false;
        }
        const int globalRow = window.rowStart + y;
        for ( int x = 0; x < winW; ++x )
        {
            if ( !buf[static_cast<size_t>( x )] )
                continue;
            const int globalCol = window.col0 + x;
            out.insert( static_cast<quint64>( globalRow ) * static_cast<quint64>( W )
                        + static_cast<quint64>( globalCol ) );
        }
        // Bounded uninterruptible stretch: one QSet insert batch per row plus
        // a scanline read; the sink call is amortized over 64 rows.
        if ( isCanceled && ++rowsSinceCancelCheck >= 64 )
        {
            rowsSinceCancelCheck = 0;
            if ( isCanceled() )
            {
                *cancelled = true;
                GDALClose( ds );
                return false;
            }
        }
    }

    GDALClose( ds );
    return true;
}

} // namespace

QSet<quint64> RsPixelRasterizer::rasterize( const QgsGeometry &geom,
                                            const double gt[6],
                                            int W,
                                            int H,
                                            bool *ok )
{
    return rasterize( geom, gt, W, H, ok, std::function<bool()> {}, nullptr, 0 );
}

QSet<quint64> RsPixelRasterizer::rasterize( const QgsGeometry &geom,
                                            const double gt[6],
                                            int W,
                                            int H,
                                            bool *ok,
                                            const std::function<bool()> &isCanceled,
                                            bool *cancelled,
                                            qint64 maxWindowPixels )
{
    QSet<quint64> out;
    if ( cancelled )
        *cancelled = false;
    const auto succeed = [&ok]( QSet<quint64> set ) -> QSet<quint64> {
        if ( ok )
            *ok = true;
        return set;
    };
    const auto fail = [&ok, &cancelled]( bool cancel ) -> QSet<quint64> {
        if ( ok )
            *ok = false;
        if ( cancelled )
            *cancelled = cancel;
        return QSet<quint64>();
    };

    // A null/empty geometry over a valid raster footprint is exact empty
    // coverage, not a failure. W/H <= 0 leaves no pixels to burn, so the
    // (empty) result is also exact.
    if ( geom.isNull() || geom.isEmpty() || W <= 0 || H <= 0 )
        return succeed( out );

    // Cancellation before any work: nothing was computed, but the caller
    // asked to stop — report it as cancelled (never as empty success).
    if ( isCanceled && isCanceled() )
        return fail( true );

    // Inverse GT: map (x,y) → pixel (col,row). Fail closed if non-invertible —
    // coverage cannot be determined, which must not read as "no pixels".
    double inv[6];
    if ( !GDALInvGeoTransform( const_cast<double *>( gt ), inv ) )
        return fail( false );

    // Clip geometry to the raster footprint in map space, then rasterize only
    // the bounding-box window so we never allocate a full W×H mask.
    // Calculate map-space footprint over all 4 corners to support rotated/sheared GT (#417).
    const double cx[4] = {
        gt[0],
        gt[0] + gt[1] * W,
        gt[0] + gt[2] * H,
        gt[0] + gt[1] * W + gt[2] * H
    };
    const double cy[4] = {
        gt[3],
        gt[3] + gt[4] * W,
        gt[3] + gt[5] * H,
        gt[3] + gt[4] * W + gt[5] * H
    };
    const QgsRectangle rasterExtent(
        *std::min_element( cx, cx + 4 ),
        *std::min_element( cy, cy + 4 ),
        *std::max_element( cx, cx + 4 ),
        *std::max_element( cy, cy + 4 ) );

    QgsGeometry clipped = geom;
    if ( !rasterExtent.isEmpty() )
    {
        const QgsGeometry footprint = QgsGeometry::fromRect( rasterExtent );
        clipped = geom.intersection( footprint );
        if ( clipped.isEmpty() || clipped.isNull() )
            return succeed( out );
    }

    const QgsRectangle bbox = clipped.boundingBox();
    if ( bbox.isEmpty() )
        return succeed( out );

    auto mapToCol = [&]( double x, double y ) -> double {
        return inv[0] + inv[1] * x + inv[2] * y;
    };
    auto mapToRow = [&]( double x, double y ) -> double {
        return inv[3] + inv[4] * x + inv[5] * y;
    };

    // Sample bbox corners (handles rotation/shear GT terms).
    const double cols[4] = {
        mapToCol( bbox.xMinimum(), bbox.yMinimum() ),
        mapToCol( bbox.xMinimum(), bbox.yMaximum() ),
        mapToCol( bbox.xMaximum(), bbox.yMinimum() ),
        mapToCol( bbox.xMaximum(), bbox.yMaximum() ),
    };
    const double rows[4] = {
        mapToRow( bbox.xMinimum(), bbox.yMinimum() ),
        mapToRow( bbox.xMinimum(), bbox.yMaximum() ),
        mapToRow( bbox.xMaximum(), bbox.yMinimum() ),
        mapToRow( bbox.xMaximum(), bbox.yMaximum() ),
    };

    int col0 = static_cast<int>( std::floor( *std::min_element( cols, cols + 4 ) ) ) - 1;
    int col1 = static_cast<int>( std::ceil( *std::max_element( cols, cols + 4 ) ) ) + 1;
    int row0 = static_cast<int>( std::floor( *std::min_element( rows, rows + 4 ) ) ) - 1;
    int row1 = static_cast<int>( std::ceil( *std::max_element( rows, rows + 4 ) ) ) + 1;

    col0 = std::clamp( col0, 0, W - 1 );
    col1 = std::clamp( col1, 0, W - 1 );
    row0 = std::clamp( row0, 0, H - 1 );
    row1 = std::clamp( row1, 0, H - 1 );
    if ( col1 < col0 || row1 < row0 )
        return succeed( out );

    const int winW = col1 - col0 + 1;
    const int winH = row1 - row0 + 1;

    // R30-T01 (WP04): bound the MEM band BEFORE allocating it — a single
    // geometry spanning a huge raster used to allocate its whole bbox up
    // front. Row chunks keep the band at maxWindowPixels while the union of
    // burned pixels stays identical (per-pixel center test).
    int chunkRows = winH;
    if ( maxWindowPixels > 0 && static_cast<qint64>( winW ) * winH > maxWindowPixels )
        chunkRows = static_cast<int>( std::max<qint64>( 1, maxWindowPixels / winW ));

    const QByteArray wkb = clipped.asWkb();
    OGRGeometryH ogrGeom = nullptr;
    OGR_G_CreateFromWkb(
        const_cast<unsigned char *>( reinterpret_cast<const unsigned char *>( wkb.constData() ) ),
        nullptr,
        &ogrGeom,
        wkb.size() );
    if ( !ogrGeom )
        return fail( false );

    for ( int chunkStart = row0; chunkStart <= row1; chunkStart += chunkRows )
    {
        if ( isCanceled && isCanceled() )
        {
            OGR_G_DestroyGeometry( ogrGeom );
            return fail( true );
        }
        const BurnWindow window{ col0, col1, chunkStart,
                                 std::min( chunkStart + chunkRows, row1 + 1 ) };
        bool cancelledNow = false;
        if ( !burnAndCollectWindow( ogrGeom, gt, W, H, window, isCanceled, out, &cancelledNow ) )
        {
            OGR_G_DestroyGeometry( ogrGeom );
            return fail( cancelledNow );
        }
    }
    OGR_G_DestroyGeometry( ogrGeom );
    return succeed( out );
}
