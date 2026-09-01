// src/processing/algorithms/spectral_roi.cpp — ROI mean spectrum
#include "spectral_roi.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFileInfo>

#include "qgsdatasourceresolver.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <gdal.h>

namespace SpectralRoiProfile
{

bool meanSpectrum( const QString &rasterPath, const QPolygonF &polygon,
                   RoiProfileResult *result, QString *errorMessage )
{
    if ( !result )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "null result" );
        return false;
    }
    *result = RoiProfileResult();
    if ( polygon.size() < 3 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "ROI polygon needs at least 3 points" );
        return false;
    }
    if ( QgsDataSourceResolver::requiresLocalExistenceCheck( rasterPath ) && !QFileInfo::exists( rasterPath ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Raster not found: %1" ).arg( rasterPath );
        return false;
    }

    ensureGdalInit();
    GDALDatasetH ds = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Cannot open raster: %1" ).arg( rasterPath );
        return false;
    }

    const int width = GDALGetRasterXSize( ds );
    const int height = GDALGetRasterYSize( ds );
    const int bandCount = GDALGetRasterCount( ds );
    if ( bandCount < 1 )
    {
        GDALClose( ds );
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Raster has no bands" );
        return false;
    }

    std::array<double, 6> gt{};
    if ( GDALGetGeoTransform( ds, gt.data() ) != CE_None )
    {
        GDALClose( ds );
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Raster has no geotransform" );
        return false;
    }

    std::array<double, 6> invGt{};
    if ( !GDALInvGeoTransform( gt.data(), invGt.data() ) )
    {
        GDALClose( ds );
        if ( errorMessage )
            *errorMessage = QStringLiteral( "Invertible geotransform required" );
        return false;
    }

    // Bounding box of the polygon in map coordinates -> pixel column/row range via all 4 corners.
    QRectF bounds = polygon.boundingRect();
    const double xs[4] = { bounds.left(), bounds.right(), bounds.left(), bounds.right() };
    const double ys[4] = { bounds.top(), bounds.top(), bounds.bottom(), bounds.bottom() };

    double minCol = std::numeric_limits<double>::infinity();
    double maxCol = -std::numeric_limits<double>::infinity();
    double minRow = std::numeric_limits<double>::infinity();
    double maxRow = -std::numeric_limits<double>::infinity();

    for ( int i = 0; i < 4; ++i )
    {
        const double px = invGt[0] + xs[i] * invGt[1] + ys[i] * invGt[2];
        const double py = invGt[3] + xs[i] * invGt[4] + ys[i] * invGt[5];
        minCol = std::min( minCol, px );
        maxCol = std::max( maxCol, px );
        minRow = std::min( minRow, py );
        maxRow = std::max( maxRow, py );
    }

    // Clamp in double space BEFORE the int cast: a polygon projected far
    // outside the raster (or a degenerate geotransform feeding NaN) would
    // otherwise hit an out-of-range/UB double→int conversion.
    if ( !std::isfinite( minCol ) || !std::isfinite( maxCol )
         || !std::isfinite( minRow ) || !std::isfinite( maxRow ) )
    {
        GDALClose( ds );
        result->mean.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
        result->stddev.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
        return true;
    }
    const double colLo = std::clamp( std::floor( minCol ), 0.0, static_cast<double>( width ) );
    const double colHi = std::clamp( std::ceil( maxCol ) + 1.0, 0.0, static_cast<double>( width ) );
    const double rowLo = std::clamp( std::floor( minRow ), 0.0, static_cast<double>( height ) );
    const double rowHi = std::clamp( std::ceil( maxRow ) + 1.0, 0.0, static_cast<double>( height ) );
    const int col0 = static_cast<int>( colLo );
    const int col1 = static_cast<int>( colHi );
    const int row0 = static_cast<int>( rowLo );
    const int row1 = static_cast<int>( rowHi );

    // No overlap between the polygon's pixel-space box and the raster.
    if ( col0 >= col1 || row0 >= row1 )
    {
        GDALClose( ds );
        result->mean.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
        result->stddev.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
        return true;
    }

    // Accumulate per band with Welford's online algorithm: a sum / sum-of-
    // squares pair loses precision to catastrophic cancellation when the mean
    // is large and the spread small (DN ~10000 ± 1). Point-in-polygon is
    // evaluated once per pixel; per band the ROI window is read in ONE
    // GDALRasterIO call (band-major), not 1x1 per pixel — O(bands) I/O calls
    // instead of O(pixels x bands) (ADR 0105 review remediation).
    std::vector<double> welfordMean( bandCount, 0.0 );
    std::vector<double> welfordM2( bandCount, 0.0 );
    std::vector<size_t> validCount( bandCount, 0 );
    size_t pixels = 0;

    const int windowW = col1 - col0;
    const int windowH = row1 - row0;
    std::vector<uint8_t> inside( static_cast<size_t>( windowW ) * windowH, 0 );

    // #704: per-polygon containment is O(vertices) per pixel (a 5,000-vertex
    // ROI over a 3000x3000 window ≈ 4.5e10 edge tests). Rasterize the polygon
    // ONCE with an even-odd scanline in pixel space: O(vertices*rows + W*H).
    // Degenerate geotransforms fall back to the exact per-pixel test.
    const double det = gt[1] * gt[5] - gt[2] * gt[4];
    bool rasterized = false;
    if ( std::abs( det ) > 1e-15 )
    {
        // Transform polygon vertices into pixel space via the inverse affine.
        struct PixEdge
        {
            double x0, y0, x1, y1;
        };
        std::vector<PixEdge> edges;
        edges.reserve( static_cast<size_t>( polygon.size() ) );
        const double invA = gt[5] / det, invB = -gt[2] / det;
        const double invC = -gt[4] / det, invD = gt[1] / det;
        std::vector<QPointF> px( static_cast<size_t>( polygon.size() ) );
        for ( int i = 0; i < polygon.size(); ++i )
        {
            const QPointF &pt = polygon.at( i );
            const double dx = pt.x() - gt[0];
            const double dy = pt.y() - gt[3];
            px[static_cast<size_t>( i )] = QPointF( invA * dx + invB * dy,
                                                    invC * dx + invD * dy );
        }
        for ( size_t i = 0; i < px.size(); ++i )
        {
            const QPointF &a = px[i];
            const QPointF &b = px[( i + 1 ) % px.size()];
            if ( a.y() == b.y() )
                continue; // horizontal edges contribute nothing to even-odd crossings
            edges.push_back( PixEdge{ a.x(), a.y(), b.x(), b.y() } );
        }

        std::vector<double> xs;
        xs.reserve( 16 );
        for ( int row = row0; row < row1; ++row )
        {
            const double cy = row + 0.5; // pixel-center scanline
            xs.clear();
            for ( const PixEdge &e : edges )
            {
                const double yMin = std::min( e.y0, e.y1 );
                const double yMax = std::max( e.y0, e.y1 );
                if ( cy < yMin || cy >= yMax )
                    continue;
                const double t = ( cy - e.y0 ) / ( e.y1 - e.y0 );
                xs.push_back( e.x0 + t * ( e.x1 - e.x0 ) );
            }
            if ( xs.size() < 2 )
                continue;
            std::sort( xs.begin(), xs.end() );
            const size_t rowBase = static_cast<size_t>( row - row0 ) * windowW;
            for ( size_t k = 0; k + 1 < xs.size(); k += 2 )
            {
                // Pixel center col+0.5 is inside when xs[k] <= center < xs[k+1].
                int cStart = static_cast<int>( std::ceil( xs[k] - 0.5 ) );
                int cEnd = static_cast<int>( std::ceil( xs[k + 1] - 0.5 ) );
                cStart = std::max( cStart, col0 );
                cEnd = std::min( cEnd, col1 );
                for ( int col = cStart; col < cEnd; ++col )
                {
                    inside[rowBase + static_cast<size_t>( col - col0 )] = 1;
                    ++pixels;
                }
            }
        }
        rasterized = true;
    }

    if ( !rasterized )
    {
        for ( int row = row0; row < row1; ++row )
        {
            const double rCenter = row + 0.5;
            for ( int col = col0; col < col1; ++col )
            {
                const double cCenter = col + 0.5;
                const double mapX = gt[0] + cCenter * gt[1] + rCenter * gt[2];
                const double mapY = gt[3] + cCenter * gt[4] + rCenter * gt[5];
                if ( polygon.containsPoint( QPointF( mapX, mapY ), Qt::OddEvenFill ) )
                {
                    inside[static_cast<size_t>( row - row0 ) * windowW + ( col - col0 )] = 1;
                    ++pixels;
                }
            }
        }
    }

    // Per-band NoData values
    std::vector<double> bandNodata( bandCount, 0.0 );
    std::vector<char> bandHasNodata( bandCount, 0 );
    for ( int b = 1; b <= bandCount; ++b )
    {
        int hasNd = 0;
        double nd = GDALGetRasterNoDataValue( GDALGetRasterBand( ds, b ), &hasNd );
        bandHasNodata[b - 1] = ( hasNd != 0 );
        bandNodata[b - 1] = nd;
    }
    if ( pixels > 0 )
    {
        std::vector<float> window( static_cast<size_t>( windowW ) * windowH );
        for ( int b = 1; b <= bandCount; ++b )
        {
            if ( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Read,
                               col0, row0, windowW, windowH, window.data(),
                               windowW, windowH, GDT_Float32, 0, 0 ) != CE_None )
            {
                // The band reports NaN downstream (validCount stays 0) — log
                // so a failed window read is not silent.
                qWarning( "SpectralRoiProfile: ROI window read failed for band %d", b );
                continue;
            }
            const bool hasNd = bandHasNodata[b - 1];
            // Float-space compare: matches large sentinels exactly (#444).
            const float ndF = static_cast<float>( bandNodata[b - 1] );
            const size_t nWin = static_cast<size_t>( windowW ) * windowH;
            for ( size_t i = 0; i < nWin; ++i )
            {
                if ( !inside[i] )
                    continue;
                const float value = window[i];
                if ( !std::isfinite( value ) )
                    continue;
                if ( hasNd && value == ndF )
                    continue;
                const size_t n = ++validCount[b - 1];
                const double delta = static_cast<double>( value ) - welfordMean[b - 1];
                welfordMean[b - 1] += delta / static_cast<double>( n );
                welfordM2[b - 1] += delta * ( static_cast<double>( value ) - welfordMean[b - 1] );
            }
        }
    }

    // Wavelength grid from band WAVELENGTH metadata (aligned with the
    // spectral-profile and resampling surfaces).
    result->wavelengths.assign( bandCount, 0.0f );
    for ( int b = 1; b <= bandCount; ++b )
    {
        const char *wl = GDALGetMetadataItem( GDALGetRasterBand( ds, b ), "WAVELENGTH", nullptr );
        bool ok = false;
        const double v = wl ? QString::fromUtf8( wl ).toDouble( &ok ) : 0.0;
        result->wavelengths[b - 1] = ( ok && v > 0.0 ) ? static_cast<float>( v ) : 0.0f;
    }

    GDALClose( ds );

    result->pixelCount = pixels;
    result->mean.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
    result->stddev.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
    if ( pixels > 0 )
    {
        for ( int b = 0; b < bandCount; ++b )
        {
            const size_t vc = validCount[b];
            if ( vc == 0 )
                continue;
            // Population variance (same N denominator as before); Welford's
            // M2 accumulation just removes the cancellation error.
            const double variance = std::max( 0.0, welfordM2[b] / static_cast<double>( vc ) );
            result->mean[b] = static_cast<float>( welfordMean[b] );
            result->stddev[b] = static_cast<float>( std::sqrt( variance ) );
        }
    }

    return true;
}

} // namespace SpectralRoiProfile
