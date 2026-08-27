// src/processing/providers/qgis_algorithms/algorithms/raster/raster_clip.cpp
#include "raster_clip.h"

#include <cmath>
#include <memory>
#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterpipe.h>
#include <qgsrasterprojector.h>
#include <qgsrasterfilewriter.h>
#include <qgsrectangle.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatereferencesystem.h>

void RasterClipAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
    addParameter( new QgsProcessingParameterExtent( QStringLiteral( "EXTENT" ), QObject::tr( "Extent" ) ) );
    addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Clipped raster" ) ) );
}

QVariantMap RasterClipAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
    if ( !layer || !layer->dataProvider() )
        throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

    QgsRectangle extent = parameterAsExtent( parameters, QStringLiteral( "EXTENT" ), context, layer->crs() );
    QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    feedback->setProgressText( QObject::tr( "Clipping raster..." ) );

    QgsRasterDataProvider *provider = layer->dataProvider();
    double pixelX = layer->rasterUnitsPerPixelX();
    double pixelY = layer->rasterUnitsPerPixelY();

    int nCols = static_cast<int>( std::lround( extent.width() / pixelX ) );  // floor truncated up to 1 px (#631 P1-28)
    int nRows = static_cast<int>( std::lround( extent.height() / pixelY ) );

    if ( nCols <= 0 || nRows <= 0 )
        throw QgsProcessingException( QObject::tr( "Invalid extent for clipping" ) );

    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );

    auto pipe = std::make_unique<QgsRasterPipe>();
    if ( !pipe->set( provider->clone() ) )
    {
        throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );
    }

    QgsRasterProjector *projector = new QgsRasterProjector();
    projector->setCrs( layer->crs(), layer->crs() );
    pipe->insert( 2, projector );

    Qgis::RasterFileWriterResult err = writer.writeRaster( pipe.get(), nCols, nRows, extent, layer->crs(), context.transformContext() );

    if ( err != Qgis::RasterFileWriterResult::Success )
        throw QgsProcessingException( QObject::tr( "Error writing clipped raster" ) );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = dest;
    return results;
}
