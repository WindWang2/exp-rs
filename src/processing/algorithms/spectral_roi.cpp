// src/processing/algorithms/spectral_roi.cpp — ROI mean spectrum
#include "spectral_roi.h"

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFileInfo>

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
    if ( !QFileInfo::exists( rasterPath ) )
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

    const int col0 = std::max( 0, static_cast<int>( std::floor( minCol ) ) );
    const int col1 = std::min( width, static_cast<int>( std::ceil( maxCol ) ) + 1 );
    const int row0 = std::max( 0, static_cast<int>( std::floor( minRow ) ) );
    const int row1 = std::min( height, static_cast<int>( std::ceil( maxRow ) ) + 1 );

    // No overlap between the polygon's pixel-space box and the raster.
    if ( col0 >= col1 || row0 >= row1 )
    {
        GDALClose( ds );
        result->mean.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
        result->stddev.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
        return true;
    }

    // Accumulate per band: sum and sum of squares. Point-in-polygon is
    // evaluated once per pixel; per band the ROI window is read in ONE
    // GDALRasterIO call (band-major), not 1x1 per pixel — O(bands) I/O calls
    // instead of O(pixels x bands) (ADR 0105 review remediation).
    std::vector<double> sum( bandCount, 0.0 );
    std::vector<double> sumSq( bandCount, 0.0 );
    size_t pixels = 0;

    const int windowW = col1 - col0;
    const int windowH = row1 - row0;
    std::vector<uint8_t> inside( static_cast<size_t>( windowW ) * windowH, 0 );
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
    std::vector<size_t> validCount( bandCount, 0 );
    if ( pixels > 0 )
    {
        std::vector<float> window( static_cast<size_t>( windowW ) * windowH );
        for ( int b = 1; b <= bandCount; ++b )
        {
            if ( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Read,
                               col0, row0, windowW, windowH, window.data(),
                               windowW, windowH, GDT_Float32, 0, 0 ) != CE_None )
                continue;
            const bool hasNd = bandHasNodata[b - 1];
            const double nd = bandNodata[b - 1];
            for ( int i = 0; i < windowW * windowH; ++i )
            {
                if ( !inside[static_cast<size_t>( i )] )
                    continue;
                const double value = window[static_cast<size_t>( i )];
                if ( !std::isfinite( value ) )
                    continue;
                if ( hasNd && value == nd )
                    continue;
                sum[b - 1] += value;
                sumSq[b - 1] += value * value;
                ++validCount[b - 1];
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
            const double n = static_cast<double>( vc );
            result->mean[b] = static_cast<float>( sum[b] / n );
            const double variance = std::max( 0.0, sumSq[b] / n - ( sum[b] / n ) * ( sum[b] / n ) );
            result->stddev[b] = static_cast<float>( std::sqrt( variance ) );
        }
    }

    return true;
}

} // namespace SpectralRoiProfile
