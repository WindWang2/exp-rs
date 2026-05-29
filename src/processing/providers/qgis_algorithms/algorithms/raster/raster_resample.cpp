// src/processing/providers/qgis_algorithms/algorithms/raster/raster_resample.cpp
#include "raster_resample.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterblock.h>
#include <qgsrasterpipe.h>
#include <qgsrasterprojector.h>
#include <qgsrasterfilewriter.h>
#include <qgsrectangle.h>

void RasterResampleAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterRasterLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ) ) );
    addParameter( new QgsProcessingParameterNumber( QStringLiteral( "RESAMPLE_FACTOR" ), QObject::tr( "Resample factor" ),
        Qgis::ProcessingNumberParameterType::Double, 2.0, false, 0.1 ) );
    addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Resampled raster" ) ) );
}

QVariantMap RasterResampleAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QgsRasterLayer *layer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
    if ( !layer || !layer->dataProvider() )
        throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

    double factor = parameterAsDouble( parameters, QStringLiteral( "RESAMPLE_FACTOR" ), context );
    if ( factor <= 0.0 )
        throw QgsProcessingException( QObject::tr( "Resample factor must be positive" ) );

    QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    feedback->setProgressText( QObject::tr( "Resampling raster..." ) );

    QgsRasterDataProvider *provider = layer->dataProvider();
    QgsRectangle extent = layer->extent();

    int newCols = static_cast<int>( layer->width() * factor );
    int newRows = static_cast<int>( layer->height() * factor );

    if ( newCols <= 0 || newRows <= 0 )
        throw QgsProcessingException( QObject::tr( "Invalid resample dimensions" ) );

    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );

    QgsRasterPipe *pipe = new QgsRasterPipe();
    if ( !pipe->set( provider->clone() ) )
    {
        delete pipe;
        throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );
    }

    QgsRasterProjector *projector = new QgsRasterProjector();
    projector->setCrs( layer->crs(), layer->crs() );
    pipe->insert( 2, projector );

    Qgis::RasterFileWriterResult err = writer.writeRaster( pipe, newCols, newRows, extent, layer->crs(), context.transformContext() );
    delete pipe;

    if ( err != Qgis::RasterFileWriterResult::Success )
        throw QgsProcessingException( QObject::tr( "Error writing resampled raster" ) );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = dest;
    return results;
}
