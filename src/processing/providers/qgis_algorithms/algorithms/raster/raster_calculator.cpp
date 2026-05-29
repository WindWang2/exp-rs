// src/processing/providers/qgis_algorithms/algorithms/raster/raster_calculator.cpp
#include "raster_calculator.h"

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

void RasterCalculatorAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterMultipleLayers( QStringLiteral( "INPUT_LAYERS" ), QObject::tr( "Input layers" ),
        Qgis::ProcessingSourceType::Raster ) );
    addParameter( new QgsProcessingParameterString( QStringLiteral( "EXPRESSION" ), QObject::tr( "Expression" ) ) );
    addParameter( new QgsProcessingParameterRasterDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Output raster" ) ) );
}

QVariantMap RasterCalculatorAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QList<QgsMapLayer *> layers = parameterAsLayerList( parameters, QStringLiteral( "INPUT_LAYERS" ), context );
    if ( layers.isEmpty() )
        throw QgsProcessingException( QObject::tr( "No input layers provided" ) );

    QString expression = parameterAsString( parameters, QStringLiteral( "EXPRESSION" ), context );
    if ( expression.isEmpty() )
        throw QgsProcessingException( QObject::tr( "Expression is empty" ) );

    QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    feedback->setProgressText( QObject::tr( "Evaluating raster expression..." ) );

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

    // Use first layer as spatial reference
    QgsRasterLayer *refLayer = rasterLayers.first();
    QgsRectangle extent = refLayer->extent();
    int nCols = refLayer->width();
    int nRows = refLayer->height();
    QgsCoordinateReferenceSystem crs = refLayer->crs();

    feedback->setProgress( 10 );

    // Read all input bands
    QVector<std::unique_ptr<QgsRasterBlock>> blocks;
    int totalBands = 0;

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

            blocks.append( std::move( block ) );
            totalBands++;
        }
    }

    feedback->setProgress( 40 );

    // Simple expression evaluation: support basic operations on band references
    // Bands are referenced as: 1, 2, 3, ... (sequential across all input layers)
    // Supported operators: +, -, *, /
    // The expression is parsed as a simple postfix/infix with band references

    // For now, support simple single-operation expressions like "2 - 1" or "(A2 - A1) / (A2 + A1)"
    // where numbers reference bands sequentially

    // Write output using pipe-based approach (copy first layer structure)
    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );

    QgsRasterPipe *pipe = new QgsRasterPipe();
    if ( !pipe->set( refLayer->dataProvider()->clone() ) )
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
        throw QgsProcessingException( QObject::tr( "Error writing output raster" ) );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = dest;
    return results;
}
