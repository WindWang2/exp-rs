// src/processing/providers/qgis_algorithms/algorithms/remote_sensing/band_math_algorithm.cpp
#include "band_math_algorithm.h"

#include "../../../../algorithms/band_math.h"

#include <processing/qgsprocessingparameters.h>
#include <processing/qgsprocessingoutputs.h>
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterblock.h>
#include <qgsrasterfilewriter.h>
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>

void BandMathAlgorithm::initAlgorithm( const QVariantMap & )
{
    addParameter( new QgsProcessingParameterMultipleLayers(
        QStringLiteral( "INPUT_LAYERS" ), QObject::tr( "Input raster layers" ),
        Qgis::ProcessingSourceType::Raster ) );
    addParameter( new QgsProcessingParameterString(
        QStringLiteral( "EXPRESSION" ), QObject::tr( "Expression (e.g., (b1 - b2) / (b1 + b2))" ) ) );
    addParameter( new QgsProcessingParameterRasterDestination(
        QStringLiteral( "OUTPUT" ), QObject::tr( "Output raster" ) ) );
}

QVariantMap BandMathAlgorithm::processAlgorithm( const QVariantMap &parameters,
    QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QList<QgsMapLayer *> layers = parameterAsLayerList( parameters, QStringLiteral( "INPUT_LAYERS" ), context );
    if ( layers.isEmpty() )
        throw QgsProcessingException( QObject::tr( "No input layers provided" ) );

    QString expression = parameterAsString( parameters, QStringLiteral( "EXPRESSION" ), context );
    if ( expression.isEmpty() )
        throw QgsProcessingException( QObject::tr( "Expression is empty" ) );

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

    if ( nCols <= 0 || nRows <= 0 )
        throw QgsProcessingException( QObject::tr( "Invalid raster dimensions" ) );

    feedback->setProgressText( QObject::tr( "Reading input bands..." ) );

    // Count total bands for accurate monotonic progress
    int totalBands = 0;
    for ( QgsRasterLayer *rl : rasterLayers )
    {
        if ( rl->crs() != crs )
        {
            throw QgsProcessingException(
                QObject::tr( "CRS mismatch: layer '%1' has CRS '%2', but reference layer has '%3'" )
                    .arg( rl->name(), rl->crs().authid(), crs.authid() ) );
        }
        if ( rl->dataProvider() )
            totalBands += rl->dataProvider()->bandCount();
    }
    if ( totalBands <= 0 )
        totalBands = 1;

    // Read REFERENCED bands only into BandData map (b1, b2, ...): a 13-band
    // 50k x 50k stack materialized ~130 GB when the expression touches two
    // bands. Mirror of the raster_calculator filter (#631; P1-26).
    const std::vector<int> referenced = BandMath::referencedBands( expression );
    const QSet<int> wanted( referenced.begin(), referenced.end() );
    BandMath::BandData bandData;
    size_t totalPixels = static_cast<size_t>( nCols ) * static_cast<size_t>( nRows );
    int bandIndex = 1;
    int loadedBands = 0;
    for ( QgsRasterLayer *rl : rasterLayers )
        if ( rl->dataProvider() )
            loadedBands += rl->dataProvider()->bandCount();
    if ( !wanted.isEmpty() )
        totalBands = std::min( totalBands, static_cast<int>( wanted.size() ) );
    (void)loadedBands;

    for ( QgsRasterLayer *rl : rasterLayers )
    {
        QgsRasterDataProvider *provider = rl->dataProvider();
        for ( int band = 1; band <= provider->bandCount(); ++band )
        {
            if ( feedback->isCanceled() )
                return {};
            // bN indexes the concatenated band sequence: skip bands the
            // expression never references.
            if ( !wanted.isEmpty() && !wanted.contains( bandIndex ) )
            {
                bandIndex++;
                continue;
            }

            std::unique_ptr<QgsRasterBlock> block( provider->block( band, extent, nCols, nRows ) );
            if ( !block || !block->isValid() )
                throw QgsProcessingException( QObject::tr( "Could not read band %1 from %2" ).arg( band ).arg( rl->name() ) );

            std::vector<float> &bandVec = bandData[bandIndex];
            bandVec.resize( totalPixels );
            for ( size_t i = 0; i < totalPixels; ++i )
            {
                if ( block->isNoData( i ) )
                    bandVec[i] = std::numeric_limits<float>::quiet_NaN();
                else
                    bandVec[i] = static_cast<float>( block->value( i ) );
            }

            feedback->setProgress( 40.0 * bandIndex / totalBands );
            bandIndex++;
        }
    }

    feedback->setProgressText( QObject::tr( "Evaluating expression: %1" ).arg( expression ) );

    // Evaluate expression
    std::vector<float> result( totalPixels );
    if ( !BandMath::evaluate( expression, bandData, result.data(), totalPixels ) )
        throw QgsProcessingException( QObject::tr( "Failed to evaluate expression: %1" ).arg( expression ) );

    feedback->setProgress( 70 );

    // Write output raster
    feedback->setProgressText( QObject::tr( "Writing output raster..." ) );

    QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );
    std::unique_ptr<QgsRasterDataProvider> outProvider(
        writer.createOneBandRaster( Qgis::DataType::Float32, nCols, nRows, extent, crs ) );
    if ( !outProvider )
        throw QgsProcessingException( QObject::tr( "Could not create output raster" ) );

    const double outNodata = std::numeric_limits<double>::quiet_NaN();
    outProvider->setNoDataValue( 1, outNodata );

    QgsRasterBlock outBlock( Qgis::DataType::Float32, nCols, nRows );
    outBlock.setNoDataValue( outNodata );
    for ( int row = 0; row < nRows; ++row )
    {
        for ( int col = 0; col < nCols; ++col )
        {
            size_t idx = static_cast<size_t>( row ) * nCols + col;
            float val = result[idx];
            if ( std::isnan( val ) )
                outBlock.setIsNoData( row, col );
            else
                outBlock.setValue( row, col, static_cast<double>( val ) );
        }
    }

    if ( !outProvider->writeBlock( &outBlock, 1 ) )
        throw QgsProcessingException( QObject::tr( "Error writing output raster" ) );

    feedback->setProgress( 100 );

    return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
}

QString BandMathAlgorithm::shortHelpString() const
{
    return QObject::tr( "Evaluates arbitrary mathematical expressions on multi-band raster data. Bands are referenced sequentially as b1, b2, ..., bN across all selected input layers." );
}

QVariantMap BandMathAlgorithm::metadata() const
{
    return QVariantMap{
        { QStringLiteral( "purpose" ), QObject::tr( "Performs band algebra and custom spectral index calculations using mathematical expressions." ) },
        { QStringLiteral( "useCases" ), QStringList{ QObject::tr( "Custom index creation (e.g., customized NDVI)" ), QObject::tr( "Band ratioing" ), QObject::tr( "Thresholding and masking" ) } },
        { QStringLiteral( "prerequisites" ), QStringList{ QObject::tr( "Input layers must be rasters." ), QObject::tr( "Expression must use valid variables (b1, b2, etc.)." ) } },
        { QStringLiteral( "limitations" ), QStringList{ QObject::tr( "All input bands must have matching spatial extents and resolutions, or they will be resampled to the first layer's geometry." ) } },
        { QStringLiteral( "workflowHints" ), QStringList{ QObject::tr( "Usually the first step in creating custom classification features or masks." ) } }
    };
}
