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
#include <qgsrasterprojector.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>

#include <cmath>

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

    // Use the red layer as spatial reference
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

    // Calculate NDVI: (NIR - RED) / (NIR + RED)
    // Range: -1 to 1
    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );

    QgsRasterPipe *pipe = new QgsRasterPipe();
    if ( !pipe->set( redProvider->clone() ) )
    {
        delete pipe;
        throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );
    }

    QgsRasterProjector *projector = new QgsRasterProjector();
    projector->setCrs( crs, crs );
    pipe->insert( 2, projector );

    Qgis::RasterFileWriterResult err = writer.writeRaster( pipe, nCols, nRows, extent, crs, context.transformContext() );
    delete pipe;

    if ( err != Qgis::RasterFileWriterResult::Success )
        throw QgsProcessingException( QObject::tr( "Error writing NDVI raster" ) );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = dest;
    return results;
}
