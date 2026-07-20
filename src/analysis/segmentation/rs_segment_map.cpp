// rs_segment_map.cpp — Phase 10B Task 10B.1
#include "rs_segment_map.h"
#include "sicnu_logging.h"

#include <gdal.h>
#include <cpl_error.h>

#include <cmath>

RsSegmentMap::RsSegmentMap( QVector<quint32> labels, int width, int height )
    : mLabels( std::move( labels ) )
    , mWidth( width )
    , mHeight( height )
{
}

RsSegmentMap RsSegmentMap::fromGeoTIFF( const QString &path )
{
    SICNU_LOG_INFO( SicnuLogTags::Segmentation, QString( "Loading segment map from: %1" ).arg( path ) );

    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    if ( !ds )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Segmentation, QString( "Failed to open segment raster: %1" ).arg( path ) );
        return {};
    }

    int w = GDALGetRasterXSize( ds );
    int h = GDALGetRasterYSize( ds );

    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    if ( !band )
    {
        GDALClose( ds );
        return {};
    }

    // ISSUE 4 fix: Read as Float32 and convert, handling all numeric raster types
    QVector<float> floatBuf( static_cast<size_t>(w) * static_cast<size_t>(h) );
    CPLErr err = GDALRasterIO( band, GF_Read, 0, 0, w, h,
                               floatBuf.data(), w, h, GDT_Float32, 0, 0 );
    GDALClose( ds );

    if ( err != CE_None )
    {
        SICNU_LOG_ERROR( SicnuLogTags::Segmentation, "GDALRasterIO failed for segment map" );
        return {};
    }

    // Convert float to uint32, clamping negative values to 0
    const size_t nPx = static_cast<size_t>(w) * static_cast<size_t>(h);
    QVector<quint32> labels( nPx );
    for ( size_t i = 0; i < nPx; ++i )
    {
        float v = floatBuf[i];
        if ( std::isnan( v ) || v < 0 )
            labels[i] = 0;
        else
            labels[i] = static_cast<quint32>( v + 0.5f ); // round to nearest
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation, QString( "Loaded segment map: %1x%2" ).arg( w ).arg( h ) );
    return RsSegmentMap( std::move( labels ), w, h );
}

quint32 RsSegmentMap::labelAt( int row, int col ) const
{
    if ( row < 0 || row >= mHeight || col < 0 || col >= mWidth )
        return 0;
    return mLabels[row * mWidth + col];
}

QSet<quint32> RsSegmentMap::uniqueLabels() const
{
    ensureSizeCache();
    QSet<quint32> result;
    for ( auto it = mSizeCache.constBegin(); it != mSizeCache.constEnd(); ++it )
        result.insert( it.key() );
    return result;
}

int RsSegmentMap::segmentCount() const
{
    ensureSizeCache();
    return mSizeCache.size();
}

void RsSegmentMap::ensureSizeCache() const
{
    if ( mSizeCacheBuilt )
        return;

    mSizeCache.clear();
    for ( quint32 label : mLabels )
    {
        if ( label != 0 )
            ++mSizeCache[label];
    }
    mSizeCacheBuilt = true;
}

QVector<QPoint> RsSegmentMap::buildCoordsForSegment( quint32 segmentId ) const
{
    QVector<QPoint> coords;
    if ( segmentId == 0 || mLabels.isEmpty() )
        return coords;

    // Pre-size when size cache is available to avoid realloc churn.
    ensureSizeCache();
    const auto sizeIt = mSizeCache.constFind( segmentId );
    if ( sizeIt == mSizeCache.constEnd() )
        return coords;
    coords.reserve( sizeIt.value() );

    for ( int r = 0; r < mHeight; ++r )
    {
        const int rowBase = r * mWidth;
        for ( int c = 0; c < mWidth; ++c )
        {
            if ( mLabels[rowBase + c] == segmentId )
                coords.append( QPoint( c, r ) );
        }
    }
    return coords;
}

QVector<QPoint> RsSegmentMap::pixelCoords( quint32 segmentId ) const
{
    if ( segmentId == 0 )
        return {};

    auto it = mCoordsCache.constFind( segmentId );
    if ( it != mCoordsCache.constEnd() )
        return it.value();

    QVector<QPoint> coords = buildCoordsForSegment( segmentId );
    mCoordsCache.insert( segmentId, coords );
    return coords;
}

int RsSegmentMap::pixelCount( quint32 segmentId ) const
{
    if ( segmentId == 0 )
        return 0;

    ensureSizeCache();
    return mSizeCache.value( segmentId, 0 );
}
