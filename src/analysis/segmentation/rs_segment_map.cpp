// rs_segment_map.cpp — Phase 10B Task 10B.1
#include "rs_segment_map.h"
#include "sicnu_logging.h"

#include <gdal.h>
#include <cpl_error.h>
#include <cpl_string.h>

#include <QFile>

#include <cmath>

namespace
{

void removeIncompleteOutput( const QString &path )
{
    if ( path.isEmpty() )
        return;
    if ( QFile::exists( path ) )
        QFile::remove( path );
}

} // namespace

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

    const size_t nPx = static_cast<size_t>(w) * static_cast<size_t>(h);
    GDALDataType dtype = GDALGetRasterDataType( band );
    QVector<quint32> labels( nPx );
    if ( dtype == GDT_UInt32 )
    {
        CPLErr err = GDALRasterIO( band, GF_Read, 0, 0, w, h,
                                   labels.data(), w, h, GDT_UInt32, 0, 0 );
        GDALClose( ds );
        if ( err != CE_None )
        {
            SICNU_LOG_ERROR( SicnuLogTags::Segmentation, "GDALRasterIO UInt32 failed for segment map" );
            return {};
        }
    }
    else if ( dtype == GDT_Int32 || dtype == GDT_Int16 || dtype == GDT_UInt16 || dtype == GDT_Byte )
    {
        // Direct integer read then cast, no float precision loss
        QVector<qint32> tmp( nPx );
        CPLErr err = GDALRasterIO( band, GF_Read, 0, 0, w, h,
                                   tmp.data(), w, h, GDT_Int32, 0, 0 );
        GDALClose( ds );
        if ( err != CE_None )
        {
            SICNU_LOG_ERROR( SicnuLogTags::Segmentation, "GDALRasterIO Int32 failed for segment map" );
            return {};
        }
        for ( size_t i = 0; i < nPx; ++i )
        {
            qint32 v = tmp[i];
            labels[i] = ( v <= 0 ) ? 0u : static_cast<quint32>( v );
        }
    }
    else
    {
        // Fallback: float path (legacy Float32 label rasters) — clamping and rounding
        QVector<float> floatBuf( nPx );
        CPLErr err = GDALRasterIO( band, GF_Read, 0, 0, w, h,
                                   floatBuf.data(), w, h, GDT_Float32, 0, 0 );
        GDALClose( ds );
        if ( err != CE_None )
        {
            SICNU_LOG_ERROR( SicnuLogTags::Segmentation, "GDALRasterIO failed for segment map" );
            return {};
        }
        for ( size_t i = 0; i < nPx; ++i )
        {
            float v = floatBuf[i];
            if ( std::isnan( v ) || v < 0 )
                labels[i] = 0;
            else
                labels[i] = static_cast<quint32>( v + 0.5f );
        }
    }

    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation, QString( "Loaded segment map: %1x%2" ).arg( w ).arg( h ) );
    return RsSegmentMap( std::move( labels ), w, h );
}

bool RsSegmentMap::toGeoTIFF( const QString &path, const QString &refPath, QString *error ) const
{
    if ( isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "toGeoTIFF: empty segment map" );
        return false;
    }
    if ( path.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "toGeoTIFF: empty output path" );
        return false;
    }

    // Fail closed when the reference raster cannot be opened: an unwritten
    // label image must never silently lose its georeferencing.
    GDALDatasetH srcDs = GDALOpen( refPath.toUtf8().constData(), GA_ReadOnly );
    if ( !srcDs )
    {
        if ( error )
            *error = QStringLiteral( "toGeoTIFF: cannot open reference raster: %1" ).arg( refPath );
        return false;
    }
    const int refW = GDALGetRasterXSize( srcDs );
    const int refH = GDALGetRasterYSize( srcDs );
    if ( refW != mWidth || refH != mHeight )
    {
        if ( error )
            *error = QStringLiteral( "toGeoTIFF: reference raster size %1x%2 != segment map %3x%4" )
                        .arg( refW )
                        .arg( refH )
                        .arg( mWidth )
                        .arg( mHeight );
        GDALClose( srcDs );
        return false;
    }

    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
    {
        if ( error )
            *error = QStringLiteral( "GTiff driver not available" );
        GDALClose( srcDs );
        return false;
    }

    char **papszOptions = nullptr;
    papszOptions = CSLSetNameValue( papszOptions, "COMPRESS", "LZW" );
    GDALDatasetH dstDs = GDALCreate( driver, path.toUtf8().constData(),
                                     mWidth, mHeight, 1, GDT_UInt32, papszOptions );
    CSLDestroy( papszOptions );

    if ( !dstDs )
    {
        if ( error )
            *error = QStringLiteral( "Cannot create output: %1" ).arg( path );
        GDALClose( srcDs );
        return false;
    }

    // Copy georeferencing from the reference raster.
    double geoTransform[6];
    if ( GDALGetGeoTransform( srcDs, geoTransform ) == CE_None )
        GDALSetGeoTransform( dstDs, geoTransform );
    const char *proj = GDALGetProjectionRef( srcDs );
    if ( proj && proj[0] )
        GDALSetProjection( dstDs, proj );
    GDALClose( srcDs );

    GDALRasterBandH outBand = GDALGetRasterBand( dstDs, 1 );
    // 0 = NoData (background / unclassified).
    GDALSetRasterNoDataValue( outBand, 0 );

    const auto &labels = mLabels;
    QVector<quint32> rowBuf( mWidth );
    for ( int r = 0; r < mHeight; ++r )
    {
        const size_t rowBase = static_cast<size_t>(r) * static_cast<size_t>(mWidth);
        for ( int c = 0; c < mWidth; ++c )
            rowBuf[c] = labels[static_cast<int>(rowBase + static_cast<size_t>(c))];
        if ( GDALRasterIO( outBand, GF_Write, 0, r, mWidth, 1,
                           rowBuf.data(), mWidth, 1, GDT_UInt32, 0, 0 ) != CE_None )
        {
            if ( error )
                *error = QStringLiteral( "RasterIO write failed at row %1" ).arg( r );
            GDALClose( dstDs );
            removeIncompleteOutput( path );
            return false;
        }
    }

    GDALClose( dstDs );

    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation,
                       QStringLiteral( "Segment map written: %1 (%2x%3, NoData=0)" )
                         .arg( path )
                         .arg( mWidth )
                         .arg( mHeight ) );
    return true;
}

quint32 RsSegmentMap::labelAt( int row, int col ) const
{
    if ( row < 0 || row >= mHeight || col < 0 || col >= mWidth )
        return 0;
    const size_t idx = static_cast<size_t>(row) * static_cast<size_t>(mWidth) + static_cast<size_t>(col);
    return mLabels[static_cast<int>( idx )];
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
        const size_t rowBase = static_cast<size_t>(r) * static_cast<size_t>(mWidth);
        for ( int c = 0; c < mWidth; ++c )
        {
            if ( mLabels[static_cast<int>(rowBase + static_cast<size_t>(c))] == segmentId )
                coords.append( QPoint( c, r ) );
        }
    }
    return coords;
}

void RsSegmentMap::ensureCoordsIndex() const
{
    if ( mCoordsIndexBuilt )
        return;
    if ( mLabels.isEmpty() )
    {
        mCoordsIndexBuilt = true;
        return;
    }
    ensureSizeCache();
    // Reserve per segment to avoid realloc churn
    for ( auto it = mSizeCache.constBegin(); it != mSizeCache.constEnd(); ++it )
    {
        QVector<QPoint> v;
        v.reserve( it.value() );
        mCoordsCache.insert( it.key(), v );
    }
    for ( int r = 0; r < mHeight; ++r )
    {
        const size_t rowBase = static_cast<size_t>(r) * static_cast<size_t>(mWidth);
        for ( int c = 0; c < mWidth; ++c )
        {
            const quint32 sid = mLabels[static_cast<int>(rowBase + static_cast<size_t>(c))];
            if ( sid == 0 ) continue;
            auto it = mCoordsCache.find( sid );
            if ( it != mCoordsCache.end() )
                it.value().append( QPoint( c, r ) );
        }
    }
    mCoordsIndexBuilt = true;
}

QVector<QPoint> RsSegmentMap::pixelCoords( quint32 segmentId ) const
{
    if ( segmentId == 0 )
        return {};

    auto it = mCoordsCache.constFind( segmentId );
    if ( it != mCoordsCache.constEnd() )
        return it.value();

    if ( mCoordsCache.size() > 8 )
    {
        // Many segments -> build full inverted index once instead of scanning W*H per segment
        ensureCoordsIndex();
        auto it2 = mCoordsCache.constFind( segmentId );
        if ( it2 != mCoordsCache.constEnd() )
            return it2.value();
        return {};
    }

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
