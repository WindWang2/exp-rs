// rs_pixel_rasterizer.cpp — see header for design notes.
#include "rs_pixel_rasterizer.h"

#include <gdal_alg.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_vsi.h>

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <memory>

// Test-only failure injection hooks (#1056). Null by default in production.
bool ( *RsPixelRasterizer::sForceRasterizeGeometriesFailure )() = nullptr;
bool ( *RsPixelRasterizer::sForceRasterIOFailure )() = nullptr;

namespace
{

/// One burn window: columns [col0, col1] and rows [rowStart, rowEnd) of the
/// geometry's bbox window. A window of the whole bbox is the legacy
/// single-shot burn; smaller row chunks bound the mask.
struct BurnWindow
{
    int col0 = 0;
    int col1 = 0; // inclusive
    int rowStart = 0;
    int rowEnd = 0; // exclusive
};

/// R30-T01 (WP04): adapter that exposes the cancel hook as a GDAL progress
/// callback. Remember a request even on GDAL versions that ignore the return
/// value of the initial progress notification.
struct GDALProgressCancelHook
{
    const std::function<bool()> *isCanceled = nullptr;
    bool stopped = false;

    static int CPL_STDCALL call( double, const char *, void *self )
    {
        auto *hook = static_cast<GDALProgressCancelHook *>( self );
        hook->stopped = hook->stopped || ( *hook->isCanceled )();
        return hook->stopped ? 0 : 1;
    }
};

/// Burn the layer into the reusable window buffer and emit covered pixels.
bool burnAndCollectWindow( OGRLayerH layer,
                           GByte *mask,
                           const double gt[6],
                           int W,
                           const BurnWindow &window,
                           const std::function<bool()> &isCanceled,
                           const RsPixelRasterizer::PixelSink &sink,
                           bool *cancelled )
{
    const int winW = window.col1 - window.col0 + 1;
    const int winH = window.rowEnd - window.rowStart;
    std::fill_n( mask, static_cast<size_t>( winW ) * winH, GByte{ 0 } );

    // Window geotransform: origin at the window's top-left corner in map
    // space; the affine terms are inherited unchanged (rotation/shear safe).
    double winGt[6] = {
        gt[0] + gt[1] * window.col0 + gt[2] * window.rowStart,
        gt[1],
        gt[2],
        gt[3] + gt[4] * window.col0 + gt[5] * window.rowStart,
        gt[4],
        gt[5],
    };
    if ( RsPixelRasterizer::sForceRasterizeGeometriesFailure
         && RsPixelRasterizer::sForceRasterizeGeometriesFailure() )
        return false;

    // The clipped geometry is already in the raster's coordinate space.
    void *transform = GDALCreateGenImgProjTransformer3( nullptr, nullptr, nullptr, winGt );
    if ( !transform )
        return false;
    GDALProgressCancelHook progressHook{ &isCanceled };
    const CPLErr rasterizeErr = GDALRasterizeLayersBuf(
        mask, winW, winH, GDT_Byte, 1, winW, 1, &layer, nullptr, winGt,
        GDALGenImgProjTransform, transform, 1.0, nullptr,
        isCanceled ? &GDALProgressCancelHook::call : nullptr, &progressHook );
    GDALDestroyGenImgProjTransformer( transform );
    if ( progressHook.stopped || rasterizeErr != CE_None )
    {
        *cancelled = progressHook.stopped;
        return false;
    }

    int rowsSinceCancelCheck = 0;
    for ( int y = 0; y < winH; ++y )
    {
        if ( RsPixelRasterizer::sForceRasterIOFailure && RsPixelRasterizer::sForceRasterIOFailure() )
            return false;
        const GByte *row = mask + static_cast<size_t>( y ) * winW;
        const int globalRow = window.rowStart + y;
        for ( int x = 0; x < winW; ++x )
        {
            if ( !row[x] )
                continue;
            const int globalCol = window.col0 + x;
            if ( !sink( static_cast<quint64>( globalRow ) * static_cast<quint64>( W )
                        + static_cast<quint64>( globalCol ) ) )
                return false;
        }
        // Scanline cancellation checks are amortized over 64 rows.
        if ( isCanceled && ++rowsSinceCancelCheck >= 64 )
        {
            rowsSinceCancelCheck = 0;
            if ( isCanceled() )
            {
                *cancelled = true;
                return false;
            }
        }
    }

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

bool RsPixelRasterizer::rasterizeEach( const QgsGeometry &geom,
                                       const double gt[6],
                                       int W,
                                       int H,
                                       const PixelSink &sink,
                                       bool *ok,
                                       const std::function<bool()> &isCanceled,
                                       bool *cancelled,
                                       qint64 maxWindowPixels )
{
    if ( cancelled )
        *cancelled = false;
    const auto succeed = [&ok]( bool value ) -> bool {
        if ( ok )
            *ok = value;
        return value;
    };
    const auto fail = [&ok, &cancelled]( bool cancel ) -> bool {
        if ( ok )
            *ok = false;
        if ( cancelled )
            *cancelled = cancel;
        return false;
    };

    // A null/empty geometry over a valid raster footprint is exact empty
    // coverage, not a failure. W/H <= 0 leaves no pixels to burn, so the
    // (empty) result is also exact.
    if ( geom.isNull() || geom.isEmpty() || W <= 0 || H <= 0 )
        return succeed( true );

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
            return succeed( true );
    }

    const QgsRectangle bbox = clipped.boundingBox();
    if ( bbox.isEmpty() )
        return succeed( true );

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
        return succeed( true );

    const int winW = col1 - col0 + 1;
    const int winH = row1 - row0 + 1;

    // R30-T01 (WP04): bound the burn buffer BEFORE allocating it — a single
    // geometry spanning a huge raster used to allocate its whole bbox up
    // front. Row chunks keep the band at maxWindowPixels while the union of
    // burned pixels stays identical (per-pixel center test).
    int chunkRows = winH;
    if ( maxWindowPixels > 0 && static_cast<qint64>( winW ) * winH > maxWindowPixels )
        chunkRows = static_cast<int>( std::max<qint64>( 1, maxWindowPixels / winW ));

    // GDAL 3.11 merged the vector Memory driver into MEM.
    GDALAllRegister();
#if GDAL_VERSION_NUM >= 3110000
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "MEM" );
#else
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "Memory" );
#endif
    GDALDatasetUniquePtr source( driver ? driver->Create( "", 0, 0, 0, GDT_Unknown, nullptr ) : nullptr );
    OGRLayer *layer = source ? source->CreateLayer( "roi", nullptr, wkbUnknown ) : nullptr;
    if ( !layer )
        return fail( false );
    const QByteArray wkb = clipped.asWkb();
    OGRGeometry *shape = nullptr;
    if ( OGRGeometryFactory::createFromWkb( wkb.constData(), nullptr, &shape, wkb.size() ) != OGRERR_NONE )
        return fail( false );
    OGRFeature feature( layer->GetLayerDefn() );
    if ( feature.SetGeometryDirectly( shape ) != OGRERR_NONE || layer->CreateFeature( &feature ) != OGRERR_NONE )
        return fail( false );

    // Rasterizing into an existing byte buffer avoids both a new MEM dataset
    // and GDAL's temporary raster I/O buffer for each row stripe.
    std::unique_ptr<GByte, decltype( &VSIFree )> mask(
        static_cast<GByte *>( VSI_MALLOC2_VERBOSE( winW, chunkRows ) ), VSIFree );
    if ( !mask )
        return fail( false );
    for ( int chunkStart = row0; chunkStart <= row1; )
    {
        if ( isCanceled && isCanceled() )
            return fail( true );
        const int chunkEnd = chunkStart + std::min( chunkRows, row1 - chunkStart + 1 );
        const BurnWindow window{ col0, col1, chunkStart, chunkEnd };
        bool cancelledNow = false;
        if ( !burnAndCollectWindow( OGRLayer::ToHandle( layer ), mask.get(), gt, W, window,
                                    isCanceled, sink, &cancelledNow ) )
            return fail( cancelledNow );
        chunkStart = chunkEnd;
    }
    return succeed( true );
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
    const bool succeeded = rasterizeEach(
        geom, gt, W, H,
        [&out]( quint64 pixel ) {
            out.insert( pixel );
            return true;
        },
        ok, isCanceled, cancelled, maxWindowPixels );
    return succeeded ? out : QSet<quint64>();
}
