// src/processing/providers/qgis_algorithms/algorithms/raster/raster_ndvi.cpp
#include "raster_ndvi.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterblock.h>
#include <qgsrasterfilewriter.h>
#include <qgsrasterpipe.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>

#include "processing/algorithms/spectral_indices.h"

#include <gdal.h>
#include <cpl_conv.h>

#include <cmath>
#include <vector>

void RasterNdviAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "RED_BAND" ), QObject::tr( "Red band layer" ) ) );
    addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "NIR_BAND" ), QObject::tr( "Near-infrared band layer" ) ) );
    addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "NDVI" ) ) );
}

QVariantMap RasterNdviAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QgsRasterLayer *redLayer = parameterAsRasterLayer( parameters, QStringLiteral( "RED_BAND" ), context );
    if ( !redLayer || !redLayer->dataProvider() )
        throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "RED_BAND" ) ) );

    QgsRasterLayer *nirLayer = parameterAsRasterLayer( parameters, QStringLiteral( "NIR_BAND" ), context );
    if ( !nirLayer || !nirLayer->dataProvider() )
        throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "NIR_BAND" ) ) );

    QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    feedback->setProgressText( QObject::tr( "Calculating NDVI..." ) );

    QgsRasterDataProvider *redProvider = redLayer->dataProvider();
    QgsRasterDataProvider *nirProvider = nirLayer->dataProvider();
    QgsRectangle extent = redLayer->extent();
    int nCols = redLayer->width();
    int nRows = redLayer->height();
    QgsCoordinateReferenceSystem crs = redLayer->crs();

    feedback->setProgress( 10 );

    // Read red band (band 1)
    std::unique_ptr<QgsRasterBlock> redBlock( redProvider->block( 1, extent, nCols, nRows ) );
    if ( !redBlock || !redBlock->isValid() )
        throw QgsProcessingException( QObject::tr( "Could not read red band" ) );

    feedback->setProgress( 30 );

    // Read NIR band (band 1)
    std::unique_ptr<QgsRasterBlock> nirBlock( nirProvider->block( 1, extent, nCols, nRows ) );
    if ( !nirBlock || !nirBlock->isValid() )
        throw QgsProcessingException( QObject::tr( "Could not read NIR band" ) );

    feedback->setProgress( 50 );

    // Extract float arrays from blocks
    qgssize pixelCount = static_cast<qgssize>( nCols ) * nRows;
    std::vector<float> redData( pixelCount );
    std::vector<float> nirData( pixelCount );
    std::vector<float> ndviData( pixelCount );

    for ( qgssize i = 0; i < pixelCount; i++ )
    {
        redData[i] = static_cast<float>( redBlock->value( i ) );
        nirData[i] = static_cast<float>( nirBlock->value( i ) );
    }

    // Compute NDVI using SpectralIndices
    SpectralIndices::ndvi( nirData.data(), redData.data(), ndviData.data(), pixelCount );

    feedback->setProgress( 70 );

    // Create output raster
    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );

    std::unique_ptr<QgsRasterPipe> pipe( new QgsRasterPipe() );

    // Create a provider clone for the output
    std::unique_ptr<QgsRasterDataProvider> provider( redProvider->clone() );
    if ( !provider )
        throw QgsProcessingException( QObject::tr( "Could not clone raster provider" ) );

    if ( !pipe->set( provider.release() ) )
        throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );

    // Write NDVI data to the output block
    std::unique_ptr<QgsRasterBlock> outBlock( new QgsRasterBlock( Qgis::DataType::Float32, nCols, nRows ) );
    if ( !outBlock || !outBlock->isValid() )
        throw QgsProcessingException( QObject::tr( "Could not create output block" ) );

    double noDataValue = redBlock->noDataValue();
    outBlock->setNoDataValue( noDataValue );

    for ( qgssize i = 0; i < pixelCount; i++ )
    {
        if ( redBlock->isNoData( i ) || nirBlock->isNoData( i ) )
            outBlock->setIsNoData( i );
        else
            outBlock->setValue( i, static_cast<double>( ndviData[i] ) );
    }

    // Write the block to the output file
    // Create a simple writer that writes the computed block
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        throw QgsProcessingException( QObject::tr( "GTiff driver not available" ) );

    GDALDatasetH dataset = GDALCreate( driver, dest.toUtf8().constData(), nCols, nRows, 1, GDT_Float32, nullptr );
    if ( !dataset )
        throw QgsProcessingException( QObject::tr( "Could not create output file: %1" ).arg( dest ) );

    // Set projection and geotransform
    double geoTransform[6] = { extent.xMinimum(), extent.width() / nCols, 0,
                               extent.yMaximum(), 0, -extent.height() / nRows };
    GDALSetGeoTransform( dataset, geoTransform );
    GDALSetProjection( dataset, crs.toWkt().toUtf8().constData() );

    // Write NDVI data
    GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
    GDALSetRasterNoDataValue( band, noDataValue );

    std::vector<float> rowData( nCols );
    for ( int row = 0; row < nRows; row++ )
    {
        for ( int col = 0; col < nCols; col++ )
        {
            qgssize idx = static_cast<qgssize>( row ) * nCols + col;
            rowData[col] = ndviData[idx];
        }
        (void)GDALRasterIO( band, GF_Write, 0, row, nCols, 1, rowData.data(), nCols, 1, GDT_Float32, 0, 0 );
    }

    GDALClose( dataset );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = dest;
    return results;
}
