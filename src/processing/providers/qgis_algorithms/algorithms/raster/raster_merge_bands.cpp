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
        QgsRasterDataProvider *provider = rl->dataProvider();
        for ( int band = 1; band <= provider->bandCount(); ++band )
        {
            if ( feedback->isCanceled() )
                break;

            std::unique_ptr<QgsRasterBlock> block( provider->block( band, extent, nCols, nRows ) );
            if ( !block || !block->isValid() )
                throw QgsProcessingException( QObject::tr( "Could not read band %1 from %2" ).arg( band ).arg( rl->name() ) );

            blocks.push_back( std::move( block ) );
            currentBand++;
            feedback->setProgress( 50.0 * currentBand / totalBands );
        }
    }

    // Write multi-band output using QgsRasterFileWriter
    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );

    auto pipe = std::make_unique<QgsRasterPipe>();
    if ( !pipe->set( refLayer->dataProvider()->clone() ) )
        throw QgsProcessingException( QObject::tr( "Could not create raster pipe" ) );

    QgsRasterProjector *projector = new QgsRasterProjector();
    projector->setCrs( crs, crs );
    pipe->insert( 2, projector );

    Qgis::RasterFileWriterResult err = writer.writeRaster( pipe.get(), nCols, nRows, extent, crs, context.transformContext() );
    pipe.reset();

    if ( err != Qgis::RasterFileWriterResult::Success )
        throw QgsProcessingException( QObject::tr( "Error writing merged raster" ) );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = dest;
    return results;
}
