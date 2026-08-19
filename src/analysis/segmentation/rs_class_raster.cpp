// rs_class_raster.cpp — Class-raster paint + optional polygonize.
#include "rs_class_raster.h"

#include "sicnu_logging.h"

#include <gdal.h>
#include <gdal_alg.h>
#include <cpl_string.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace
{

void removeIncompleteOutput( const QString &path )
{
    if ( path.isEmpty() )
        return;
    if ( QFile::exists( path ) )
        QFile::remove( path );
}

void removeShapefileWithSidecars( const QString &shpPath )
{
    if ( shpPath.isEmpty() )
        return;
    // Main .shp file
    removeIncompleteOutput( shpPath );
    // ESRI shapefile sidecars: same basename, different extensions.
    const QFileInfo fi( shpPath );
    const QString dir = fi.absolutePath();
    const QString base = fi.completeBaseName();
    // completeBaseName for "foo.shp" is "foo"; for "foo.shp.tmp~123" is "foo.shp"
    // Handle both cases by trying the raw path with sidecar suffix and the
    // canonical base+ext variant.
    const QStringList exts{ QStringLiteral( ".dbf" ), QStringLiteral( ".shx" ),
                            QStringLiteral( ".prj" ), QStringLiteral( ".cpg" ),
                            QStringLiteral( ".qpj" ) };
    for ( const QString &ext : exts )
    {
        // Candidate 1: direct suffix append (covers ".shp.tmp~123" temp datasets)
        QFile::remove( shpPath + ext );
        // Candidate 2: dir/base + ext (covers normal "foo.shp" -> "foo.dbf")
        QFile::remove( dir + QLatin1Char( '/' ) + base + ext );
        // Candidate 3: strip one extension if tempPath contains ".shp."
        // e.g. "foo.shp.tmp~123" -> "foo.tmp~123.dbf" is not needed — the
        // shapefile driver consistently creates sidecars as "<dataset>.dbf",
        // but be defensive.
    }
}

bool renameShapefileWithSidecars( const QString &tmpPath, const QString &finalPath )
{
    // Rename main dataset
    if ( !QFile::rename( tmpPath, finalPath ) )
        return false;
    const QFileInfo tmpFi( tmpPath );
    const QFileInfo finalFi( finalPath );
    const QStringList exts{ QStringLiteral( ".dbf" ), QStringLiteral( ".shx" ),
                            QStringLiteral( ".prj" ), QStringLiteral( ".cpg" ),
                            QStringLiteral( ".qpj" ) };
    for ( const QString &ext : exts )
    {
        const QString tmpSide = tmpPath + ext;
        if ( !QFile::exists( tmpSide ) )
            continue;
        const QString finalSide = finalPath + ext;
        // Also try dir/base variant for final path consistency
        QFile::remove( finalSide );
        // If final side exists via dir/base naming, remove it too
        const QString finalAlt = finalFi.absolutePath() + QLatin1Char( '/' )
                                 + finalFi.completeBaseName() + ext;
        if ( finalAlt != finalSide )
            QFile::remove( finalAlt );
        if ( !QFile::rename( tmpSide, finalSide ) )
        {
            // Rollback already-renamed main file is caller responsibility
            return false;
        }
    }
    // Clean up any stray dir/base sidecars left from normal naming
    for ( const QString &ext : exts )
    {
        const QString tmpAlt = tmpFi.absolutePath() + QLatin1Char( '/' )
                               + tmpFi.completeBaseName() + ext;
        if ( QFile::exists( tmpAlt ) )
        {
            const QString finalSide = finalPath + ext;
            if ( !QFile::exists( finalSide ) )
                QFile::rename( tmpAlt, finalSide );
            else
                QFile::remove( tmpAlt );
        }
    }
    return true;
}

} // namespace

RsClassRasterResult RsClassRaster::paint(
    const RsSegmentMap &segMap,
    const QMap<quint32, int> &segmentClasses,
    const QString &referenceRasterPath,
    const QString &outputPath,
    const QHash<int, QColor> &classColors )
{
    RsClassRasterResult result;

    if ( segMap.isEmpty() )
    {
        result.errorMessage = QStringLiteral( "paint: empty segment map" );
        return result;
    }
    if ( outputPath.isEmpty() )
    {
        result.errorMessage = QStringLiteral( "paint: empty output path" );
        return result;
    }

    // Class ids must be ≥ 1; 0 is reserved as NoData / unclassified.
    for ( auto it = segmentClasses.constBegin(); it != segmentClasses.constEnd(); ++it )
    {
        if ( it.value() <= 0 )
        {
            result.errorMessage = QStringLiteral(
                "paint: class ids must be >= 1 (got %1 for segment %2); 0 is NoData" )
                                    .arg( it.value() )
                                    .arg( it.key() );
            return result;
        }
    }

    const int w = segMap.width();
    const int h = segMap.height();

    int maxClassId = 0;
    for ( auto it = segmentClasses.constBegin(); it != segmentClasses.constEnd(); ++it )
        maxClassId = std::max( maxClassId, it.value() );

    if ( maxClassId > 65535 )
    {
        result.errorMessage = QStringLiteral( "Class ID %1 exceeds UInt16 range" ).arg( maxClassId );
        return result;
    }

    const bool useUInt16 = maxClassId > 255;
    const GDALDataType outType = useUInt16 ? GDT_UInt16 : GDT_Byte;

    // Copy georeferencing from reference when available; fail on grid size mismatch before creating output.
    double geoTransform[6];
    bool hasGeoTransform = false;
    QString projStr;
    if ( !referenceRasterPath.isEmpty() )
    {
        GDALDatasetH srcDs = GDALOpen( referenceRasterPath.toUtf8().constData(), GA_ReadOnly );
        if ( srcDs )
        {
            const int refW = GDALGetRasterXSize( srcDs );
            const int refH = GDALGetRasterYSize( srcDs );
            if ( refW != w || refH != h )
            {
                result.errorMessage = QStringLiteral(
                                         "paint: reference raster size %1x%2 != segment map %3x%4" )
                                       .arg( refW )
                                       .arg( refH )
                                       .arg( w )
                                       .arg( h );
                GDALClose( srcDs );
                return result;
            }

            if ( GDALGetGeoTransform( srcDs, geoTransform ) == CE_None )
                hasGeoTransform = true;
            const char *proj = GDALGetProjectionRef( srcDs );
            if ( proj && proj[0] )
                projStr = QString::fromUtf8( proj );
            GDALClose( srcDs );
        }
    }

    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
    {
        result.errorMessage = QStringLiteral( "GTiff driver not available" );
        return result;
    }

    const QString tempPath = outputPath + QStringLiteral( ".tmp~%1" ).arg( reinterpret_cast<quintptr>( &segMap ) );

    char **papszOptions = nullptr;
    papszOptions = CSLSetNameValue( papszOptions, "COMPRESS", "LZW" );
    GDALDatasetH dstDs = GDALCreate( driver, tempPath.toUtf8().constData(),
                                     w, h, 1, outType, papszOptions );
    CSLDestroy( papszOptions );

    if ( !dstDs )
    {
        result.errorMessage = QStringLiteral( "Cannot create output: %1" ).arg( outputPath );
        return result;
    }

    if ( hasGeoTransform )
        GDALSetGeoTransform( dstDs, geoTransform );
    if ( !projStr.isEmpty() )
        GDALSetProjection( dstDs, projStr.toUtf8().constData() );

    GDALRasterBandH outBand = GDALGetRasterBand( dstDs, 1 );
    // 0 = NoData / unclassified (class ids are ≥ 1).
    GDALSetRasterNoDataValue( outBand, 0 );

    if ( !useUInt16 && !classColors.isEmpty() )
    {
        GDALColorTableH ct = GDALCreateColorTable( GPI_RGB );
        GDALColorEntry nodataColor = { 0, 0, 0, 0 };
        GDALSetColorEntry( ct, 0, &nodataColor );
        for ( auto it = classColors.constBegin(); it != classColors.constEnd(); ++it )
        {
            if ( it.key() <= 0 )
                continue;
            GDALColorEntry ce;
            ce.c1 = static_cast<short>( it.value().red() );
            ce.c2 = static_cast<short>( it.value().green() );
            ce.c3 = static_cast<short>( it.value().blue() );
            ce.c4 = 255;
            GDALSetColorEntry( ct, it.key(), &ce );
        }
        GDALSetRasterColorTable( outBand, ct );
        GDALSetRasterColorInterpretation( outBand, GCI_PaletteIndex );
        GDALDestroyColorTable( ct );
    }

    const quint32 *labels = segMap.labels().constData();
    quint64 totalPixels = 0;

    if ( useUInt16 )
    {
        QVector<quint16> rowBuf( w );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
            {
                const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c);
                const quint32 segId = labels[idx];
                if ( segId == 0 )
                {
                    rowBuf[c] = 0;
                    continue;
                }
                const auto it = segmentClasses.constFind( segId );
                if ( it != segmentClasses.constEnd() && it.value() > 0 )
                {
                    rowBuf[c] = static_cast<quint16>( it.value() );
                    ++totalPixels;
                }
                else
                {
                    rowBuf[c] = 0;
                }
            }
            if ( GDALRasterIO( outBand, GF_Write, 0, r, w, 1,
                               rowBuf.data(), w, 1, GDT_UInt16, 0, 0 ) != CE_None )
            {
                result.errorMessage = QStringLiteral( "RasterIO write failed at row %1" ).arg( r );
                GDALClose( dstDs );
                removeIncompleteOutput( tempPath );
                return result;
            }
        }
    }
    else
    {
        QVector<quint8> rowBuf( w );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
            {
                const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c);
                const quint32 segId = labels[idx];
                if ( segId == 0 )
                {
                    rowBuf[c] = 0;
                    continue;
                }
                const auto it = segmentClasses.constFind( segId );
                if ( it != segmentClasses.constEnd() && it.value() > 0 )
                {
                    rowBuf[c] = static_cast<quint8>( it.value() );
                    ++totalPixels;
                }
                else
                {
                    rowBuf[c] = 0;
                }
            }
            if ( GDALRasterIO( outBand, GF_Write, 0, r, w, 1,
                               rowBuf.data(), w, 1, GDT_Byte, 0, 0 ) != CE_None )
            {
                result.errorMessage = QStringLiteral( "RasterIO write failed at row %1" ).arg( r );
                GDALClose( dstDs );
                removeIncompleteOutput( tempPath );
                return result;
            }
        }
    }

    GDALClose( dstDs );

    QFile::remove( outputPath );
    if ( !QFile::rename( tempPath, outputPath ) )
    {
        removeIncompleteOutput( tempPath );
        result.errorMessage = QStringLiteral( "Cannot finalize output: %1" ).arg( outputPath );
        return result;
    }

    result.ok = true;
    result.totalPixels = totalPixels;
    result.outputPath = outputPath;
    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation,
                       QStringLiteral( "Class raster written: %1 (%2 classified pixels, NoData=0)" )
                         .arg( outputPath )
                         .arg( totalPixels ) );
    return result;
}

RsClassRasterResult RsClassRaster::polygonize(
    const QString &classRasterPath,
    const QString &outputVectorPath,
    const QString &fieldName )
{
    RsClassRasterResult result;

    GDALDatasetH rasterDs = GDALOpen( classRasterPath.toUtf8().constData(), GA_ReadOnly );
    if ( !rasterDs )
    {
        result.errorMessage = QStringLiteral( "polygonize: cannot open %1" ).arg( classRasterPath );
        return result;
    }

    GDALRasterBandH band = GDALGetRasterBand( rasterDs, 1 );
    if ( !band )
    {
        GDALClose( rasterDs );
        result.errorMessage = QStringLiteral( "polygonize: no band 1" );
        return result;
    }

    GDALDriverH drv = GDALGetDriverByName( "ESRI Shapefile" );
    if ( !drv )
    {
        GDALClose( rasterDs );
        result.errorMessage = QStringLiteral( "polygonize: ESRI Shapefile driver missing" );
        return result;
    }

    // Build mask in MEM before touching the filesystem output — MEM failures
    // must not destroy a previous polygonize result (atomic write contract).
    const int w = GDALGetRasterXSize( rasterDs );
    const int h = GDALGetRasterYSize( rasterDs );
    GDALDriverH memDrv = GDALGetDriverByName( "MEM" );
    if ( !memDrv )
    {
        GDALClose( rasterDs );
        result.errorMessage = QStringLiteral( "polygonize: MEM driver unavailable for mask band" );
        return result;
    }

    GDALDatasetH maskDs = GDALCreate( memDrv, "", w, h, 1, GDT_Byte, nullptr );
    if ( !maskDs )
    {
        GDALClose( rasterDs );
        result.errorMessage = QStringLiteral( "polygonize: cannot create mask dataset" );
        return result;
    }

    GDALRasterBandH maskBand = GDALGetRasterBand( maskDs, 1 );
    if ( !maskBand )
    {
        GDALClose( maskDs );
        GDALClose( rasterDs );
        result.errorMessage = QStringLiteral( "polygonize: cannot get mask band" );
        return result;
    }

    QVector<float> classRow( w );
    QVector<quint8> maskRow( w );
    for ( int r = 0; r < h; ++r )
    {
        // Float read works for Byte and UInt16 class rasters.
        if ( GDALRasterIO( band, GF_Read, 0, r, w, 1,
                           classRow.data(), w, 1, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( maskDs );
            GDALClose( rasterDs );
            result.errorMessage = QStringLiteral( "polygonize: failed reading class raster row %1" )
                                    .arg( r );
            return result;
        }
        for ( int c = 0; c < w; ++c )
            maskRow[c] = classRow[c] > 0.0f ? 255 : 0;
        if ( GDALRasterIO( maskBand, GF_Write, 0, r, w, 1,
                           maskRow.data(), w, 1, GDT_Byte, 0, 0 ) != CE_None )
        {
            GDALClose( maskDs );
            GDALClose( rasterDs );
            result.errorMessage = QStringLiteral( "polygonize: failed writing mask row %1" ).arg( r );
            return result;
        }
    }

    const QString tempVectorPath = outputVectorPath
                                   + QStringLiteral( ".tmp~%1" )
                                         .arg( reinterpret_cast<quintptr>( &outputVectorPath ) );

    GDALDatasetH vecDs = GDALCreate( drv, tempVectorPath.toUtf8().constData(),
                                     0, 0, 0, GDT_Unknown, nullptr );
    if ( !vecDs )
    {
        GDALClose( maskDs );
        GDALClose( rasterDs );
        result.errorMessage = QStringLiteral( "polygonize: cannot create %1" ).arg( outputVectorPath );
        return result;
    }

    OGRSpatialReferenceH srs = nullptr;
    const char *proj = GDALGetProjectionRef( rasterDs );
    if ( proj && proj[0] )
    {
        srs = OSRNewSpatialReference( nullptr );
        if ( OSRSetFromUserInput( srs, proj ) != OGRERR_NONE )
        {
            OSRDestroySpatialReference( srs );
            srs = nullptr;
        }
    }

    OGRLayerH layer = GDALDatasetCreateLayer( vecDs, "classes", srs, wkbPolygon, nullptr );
    if ( srs )
        OSRDestroySpatialReference( srs );

    if ( !layer )
    {
        GDALClose( vecDs );
        GDALClose( maskDs );
        GDALClose( rasterDs );
        removeShapefileWithSidecars( tempVectorPath );
        result.errorMessage = QStringLiteral( "polygonize: CreateLayer failed" );
        return result;
    }

    OGRFieldDefnH field = OGR_Fld_Create( fieldName.toUtf8().constData(), OFTInteger );
    OGR_L_CreateField( layer, field, TRUE );
    OGR_Fld_Destroy( field );

    const int fieldIndex = 0;
    const CPLErr err = GDALPolygonize( band, maskBand, layer, fieldIndex, nullptr, nullptr, nullptr );

    GDALClose( maskDs );
    GDALClose( vecDs );
    GDALClose( rasterDs );

    if ( err != CE_None )
    {
        removeShapefileWithSidecars( tempVectorPath );
        result.errorMessage = QStringLiteral( "GDALPolygonize failed" );
        return result;
    }

    // Atomically publish temp dataset over the final path (preserves previous
    // result on failure, cleans orphan sidecars).
    removeShapefileWithSidecars( outputVectorPath );
    if ( !renameShapefileWithSidecars( tempVectorPath, outputVectorPath ) )
    {
        // Fallback: at least try to remove incomplete temp sidecars
        removeShapefileWithSidecars( tempVectorPath );
        result.errorMessage = QStringLiteral( "polygonize: failed to finalize output %1" )
                                .arg( outputVectorPath );
        return result;
    }

    result.ok = true;
    result.outputPath = outputVectorPath;
    return result;
}
