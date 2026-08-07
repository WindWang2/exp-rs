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

    // Bounding box of the polygon in map coordinates -> pixel column/row range.
    QRectF bounds = polygon.boundingRect();
    const double minCol = std::floor( ( bounds.left() - gt[0] ) / gt[1] );
    const double maxCol = std::ceil( ( bounds.right() - gt[0] ) / gt[1] );
    const double minRow = std::floor( ( bounds.bottom() - gt[3] ) / gt[5] ); // gt[5] < 0
    const double maxRow = std::ceil( ( bounds.top() - gt[3] ) / gt[5] );
    const int col0 = std::max( 0, static_cast<int>( minCol ) );
    const int col1 = std::min( width, static_cast<int>( maxCol ) + 1 );
    const int row0 = std::max( 0, static_cast<int>( minRow ) );
    const int row1 = std::min( height, static_cast<int>( maxRow ) + 1 );

    // No overlap between the polygon's pixel-space box and the raster.
    if ( col0 >= col1 || row0 >= row1 )
    {
        GDALClose( ds );
        result->mean.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
        result->stddev.assign( bandCount, std::numeric_limits<float>::quiet_NaN() );
        return true;
    }

    // Accumulate per band: sum and sum of squares.
    std::vector<double> sum( bandCount, 0.0 );
    std::vector<double> sumSq( bandCount, 0.0 );
    size_t pixels = 0;

    std::vector<float> line( static_cast<size_t>( col1 - col0 ) );
    for ( int row = row0; row < row1; ++row )
    {
        const double mapY = gt[3] + ( row + 0.5 ) * gt[5];
        for ( int col = col0; col < col1; ++col )
        {
            const double mapX = gt[0] + ( col + 0.5 ) * gt[1];
            if ( !polygon.containsPoint( QPointF( mapX, mapY ), Qt::OddEvenFill ) )
                continue;

            for ( int b = 1; b <= bandCount; ++b )
            {
                float value = 0.0f;
                if ( GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Read, col, row, 1, 1,
                                   &value, 1, 1, GDT_Float32, 0, 0 ) != CE_None )
                    continue;
                if ( !std::isfinite( value ) )
                    continue;
                sum[b - 1] += value;
                sumSq[b - 1] += static_cast<double>( value ) * value;
            }
            ++pixels;
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
            const double n = static_cast<double>( pixels );
            result->mean[b] = static_cast<float>( sum[b] / n );
            const double variance = std::max( 0.0, sumSq[b] / n - ( sum[b] / n ) * ( sum[b] / n ) );
            result->stddev[b] = static_cast<float>( std::sqrt( variance ) );
        }
    }

    return true;
}

} // namespace SpectralRoiProfile
