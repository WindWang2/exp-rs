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
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/staged_raster_output.h"
#include "processing/providers/qgis_algorithms/algorithms/toolbox_raster_preflight.h"

#include <gdal.h>
#include <cpl_conv.h>
#include <cpl_vsi.h>

#include <cmath>
#include <limits>
#include <memory>
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

    // Stage beside the destination, publish atomically on success (#617).
    const QString destTarget = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );
    auto staged = sicnu::processing::makeStagedRasterOutput( destTarget );
    if ( !staged )
        throw QgsProcessingException( QObject::tr( "Could not allocate a staging path next to %1" ).arg( destTarget ) );
    const QString dest = staged->stagedPath();

    feedback->setProgressText( QObject::tr( "Calculating NDVI..." ) );

    QgsRasterDataProvider *redProvider = redLayer->dataProvider();
    QgsRasterDataProvider *nirProvider = nirLayer->dataProvider();
    QgsRectangle extent = redLayer->extent();
    int nCols = redLayer->width();
    int nRows = redLayer->height();
    QgsCoordinateReferenceSystem crs = redLayer->crs();

    if ( redLayer->crs() != nirLayer->crs() )
    {
        throw QgsProcessingException(
            QObject::tr( "CRS mismatch: red layer has CRS '%1', but NIR layer has '%2'" )
                .arg( redLayer->crs().authid(), nirLayer->crs().authid() ) );
    }

    sicnu::processing::toolbox::requireCompatibleRasterGrid(
        redLayer, nirLayer, QObject::tr( "NIR band" ) );

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

    const float outNodata = std::numeric_limits<float>::quiet_NaN();

    for ( qgssize i = 0; i < pixelCount; i++ )
    {
        if ( redBlock->isNoData( i ) || nirBlock->isNoData( i ) )
        {
            redData[i] = outNodata;
            nirData[i] = outNodata;
        }
        else
        {
            redData[i] = static_cast<float>( redBlock->value( i ) );
            nirData[i] = static_cast<float>( nirBlock->value( i ) );
        }
    }

    // Compute NDVI using SpectralIndices
    SpectralIndices::ndvi( nirData.data(), redData.data(), ndviData.data(), pixelCount );

    for ( qgssize i = 0; i < pixelCount; i++ )
    {
        if ( redBlock->isNoData( i ) || nirBlock->isNoData( i ) )
            ndviData[i] = outNodata;
    }

    feedback->setProgress( 70 );

    // Write the block to the output file
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
    GDALSetRasterNoDataValue( band, outNodata );

    std::vector<float> rowData( nCols );
    for ( int row = 0; row < nRows; row++ )
    {
        for ( int col = 0; col < nCols; col++ )
        {
            qgssize idx = static_cast<qgssize>( row ) * nCols + col;
            rowData[col] = ndviData[idx];
        }
        // A per-row write error must not be swallowed: the algorithm would
        // report success with a truncated output (#631 P1-25).
        if ( GDALRasterIO( band, GF_Write, 0, row, nCols, 1, rowData.data(), nCols, 1, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( dataset );
            // Remove the truncated output: a failed run must not leave a
            // plausible-looking partial raster behind (#1043).
            VSIUnlink( dest.toUtf8().constData() );
            throw QgsProcessingException( QObject::tr( "Failed to write NDVI output row %1" ).arg( row ) );
        }
    }

    // Same close-time flush gate as raster_calculator: the per-row GF_Write
    // checks only covered the driver's cache — GDALClose is where deferred
    // write failures (disk full, quota) surface.
    QString closeError;
    if ( !closeDatasetFailClosed( dataset, &closeError ) )
    {
        VSIUnlink( dest.toUtf8().constData() );
        throw QgsProcessingException( closeError.isEmpty()
                                          ? QObject::tr( "Failed to flush NDVI output %1" ).arg( destTarget )
                                          : closeError );
    }
    QString publishError;
    if ( !staged->publish( &publishError ) )
        throw QgsProcessingException( publishError.isEmpty()
                                          ? QObject::tr( "Failed to publish NDVI output %1" ).arg( destTarget )
                                          : publishError );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = destTarget;
    return results;
}
