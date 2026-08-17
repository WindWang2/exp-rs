// src/processing/providers/qgis_algorithms/algorithms/raster/raster_merge_bands.cpp
#include "raster_merge_bands.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterblock.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterpipe.h>
#include <qgsrasterprojector.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>
#include <gdal.h>

void RasterMergeBandsAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterMultipleLayers( QStringLiteral( "INPUT_LAYERS" ), QObject::tr( "Input layers" ),
        Qgis::ProcessingSourceType::Raster ) );
    addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Merged raster" ) ) );
}

QVariantMap RasterMergeBandsAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QList<QgsMapLayer *> layers = parameterAsLayerList( parameters, QStringLiteral( "INPUT_LAYERS" ), context );
    if ( layers.isEmpty() )
        throw QgsProcessingException( QObject::tr( "No input layers provided" ) );

    // Filter to valid raster layers
    QList<QgsRasterLayer *> rasterLayers;
    for ( QgsMapLayer *layer : layers )
    {
        QgsRasterLayer *rl = qobject_cast<QgsRasterLayer *>( layer );
        if ( rl && rl->dataProvider() )
            rasterLayers.append( rl );
    }

    if ( rasterLayers.isEmpty() )
        throw QgsProcessingException( QObject::tr( "No valid raster layers found" ) );

    QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    feedback->setProgressText( QObject::tr( "Merging raster bands..." ) );

    // Use the first layer as reference for extent, CRS and dimensions
    QgsRasterLayer *refLayer = rasterLayers.first();
    QgsRectangle extent = refLayer->extent();
    QgsCoordinateReferenceSystem crs = refLayer->crs();
    int nCols = refLayer->width();
    int nRows = refLayer->height();

    for ( QgsRasterLayer *rl : rasterLayers )
    {
        if ( rl->width() != nCols || rl->height() != nRows )
        {
            throw QgsProcessingException(
                QObject::tr( "Raster layer %1 dimensions (%2x%3) do not match reference (%4x%5)" )
                    .arg( rl->name() ).arg( rl->width() ).arg( rl->height() ).arg( nCols ).arg( nRows ) );
        }
    }

    // Calculate total band count across all input layers
    int totalBands = 0;
    for ( QgsRasterLayer *rl : rasterLayers )
        totalBands += rl->bandCount();

    if ( totalBands == 0 )
        throw QgsProcessingException( QObject::tr( "Input layers have no bands" ) );

    // Read all bands from all layers into memory blocks, then write merged output
    std::vector<std::unique_ptr<QgsRasterBlock>> blocks;
    int currentBand = 0;

    for ( QgsRasterLayer *rl : rasterLayers )
    {
        if ( feedback->isCanceled() )
            return QVariantMap();

        QgsRasterDataProvider *provider = rl->dataProvider();
        for ( int band = 1; band <= provider->bandCount(); ++band )
        {
            if ( feedback->isCanceled() )
                return QVariantMap();

            std::unique_ptr<QgsRasterBlock> block( provider->block( band, extent, nCols, nRows ) );
            if ( !block || !block->isValid() )
                throw QgsProcessingException( QObject::tr( "Could not read band %1 from %2" ).arg( band ).arg( rl->name() ) );

            blocks.push_back( std::move( block ) );
            currentBand++;
            feedback->setProgress( 50.0 * currentBand / totalBands );
        }
    }

    if ( feedback->isCanceled() || static_cast<int>( blocks.size() ) < totalBands )
        return QVariantMap();

    // Write multi-band output using GDAL
    GDALDriverH hDriver = GDALGetDriverByName( "GTiff" );
    if ( !hDriver )
        throw QgsProcessingException( QObject::tr( "GTiff driver not available" ) );

    GDALDataType gdalType = GDT_Float32;
    if ( !blocks.empty() )
    {
        const Qgis::DataType dt = blocks[0]->dataType();
        if ( dt == Qgis::DataType::Byte || dt == Qgis::DataType::UInt16 || dt == Qgis::DataType::Int16 || dt == Qgis::DataType::UInt32 || dt == Qgis::DataType::Int32 || dt == Qgis::DataType::Float32 || dt == Qgis::DataType::Float64 )
        {
            gdalType = ( dt == Qgis::DataType::Byte ) ? GDT_Byte :
                       ( dt == Qgis::DataType::UInt16 ) ? GDT_UInt16 :
                       ( dt == Qgis::DataType::Int16 ) ? GDT_Int16 :
                       ( dt == Qgis::DataType::UInt32 ) ? GDT_UInt32 :
                       ( dt == Qgis::DataType::Int32 ) ? GDT_Int32 :
                       ( dt == Qgis::DataType::Float64 ) ? GDT_Float64 : GDT_Float32;
        }
    }

    GDALDatasetH hOutDs = GDALCreate( hDriver, dest.toUtf8().constData(), nCols, nRows, totalBands, gdalType, nullptr );
    if ( !hOutDs )
        throw QgsProcessingException( QObject::tr( "Could not create output file %1" ).arg( dest ) );

    double geoTransform[6] = { extent.xMinimum(), extent.width() / nCols, 0,
                               extent.yMaximum(), 0, -extent.height() / nRows };
    GDALSetGeoTransform( hOutDs, geoTransform );
    if ( crs.isValid() )
    {
        QByteArray wkt = crs.toWkt( Qgis::CrsWktVariant::Wkt1Gdal ).toUtf8();
        GDALSetProjection( hOutDs, wkt.constData() );
    }

    for ( int b = 0; b < totalBands; ++b )
    {
        if ( feedback->isCanceled() )
        {
            GDALClose( hOutDs );
            return QVariantMap();
        }
        GDALRasterBandH hBand = GDALGetRasterBand( hOutDs, b + 1 );
        if ( blocks[b]->hasNoDataValue() )
        {
            GDALSetRasterNoDataValue( hBand, blocks[b]->noDataValue() );
        }
        const void *data = blocks[b]->bits();
        CPLErr cplErr = GDALRasterIO( hBand, GF_Write, 0, 0, nCols, nRows,
                                     const_cast<void *>( data ), nCols, nRows,
                                     gdalType, 0, 0 );
        if ( cplErr != CE_None )
        {
            GDALClose( hOutDs );
            throw QgsProcessingException( QObject::tr( "Error writing band %1" ).arg( b + 1 ) );
        }
        feedback->setProgress( 50.0 + 50.0 * ( b + 1 ) / totalBands );
    }
    GDALClose( hOutDs );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = dest;
    return results;
}
