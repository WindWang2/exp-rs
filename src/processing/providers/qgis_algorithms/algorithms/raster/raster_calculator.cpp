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
#include <qgsrectangle.h>
#include <qgscoordinatereferencesystem.h>

#include <processing/algorithms/band_math.h>

#include <gdal.h>
#include <cpl_conv.h>

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

    if ( nCols <= 0 || nRows <= 0 )
        throw QgsProcessingException( QObject::tr( "Invalid raster dimensions" ) );

    for ( QgsRasterLayer *rl : rasterLayers )
    {
        if ( rl->crs() != crs )
        {
            throw QgsProcessingException(
                QObject::tr( "CRS mismatch: layer '%1' has CRS '%2', but reference layer has '%3'" )
                    .arg( rl->name(), rl->crs().authid(), crs.authid() ) );
        }
    }

    feedback->setProgress( 10 );

    // Read all input bands into BandMath::BandData
    BandMath::BandData bandData;
    size_t totalPixels = static_cast<size_t>( nCols ) * static_cast<size_t>( nRows );
    int bandIndex = 1;

    for ( QgsRasterLayer *rl : rasterLayers )
    {
        QgsRasterDataProvider *provider = rl->dataProvider();
        for ( int band = 1; band <= provider->bandCount(); ++band )
        {
            if ( feedback->isCanceled() )
                return {};

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
            bandIndex++;
        }
    }

    feedback->setProgress( 40 );

    // Evaluate expression using BandMath engine
    std::vector<float> result( totalPixels );
    if ( !BandMath::evaluate( expression, bandData, result.data(), totalPixels ) )
        throw QgsProcessingException( QObject::tr( "Failed to evaluate expression: %1" ).arg( expression ) );

    feedback->setProgress( 70 );

    // Write output as single-band GeoTIFF
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        throw QgsProcessingException( QObject::tr( "GTiff driver not available" ) );

    GDALDatasetH outDs = GDALCreate( driver, dest.toUtf8().constData(), nCols, nRows, 1, GDT_Float32, nullptr );
    if ( !outDs )
        throw QgsProcessingException( QObject::tr( "Could not create output file: %1" ).arg( dest ) );

    // Set GeoTransform
    double geoTransform[6] = { extent.xMinimum(), extent.width() / nCols, 0,
                               extent.yMaximum(), 0, -extent.height() / nRows };
    GDALSetGeoTransform( outDs, geoTransform );

    // Set CRS
    std::string wkt = crs.toWkt().toStdString();
    GDALSetProjection( outDs, wkt.c_str() );

    // Write data
    GDALRasterBandH outBand = GDALGetRasterBand( outDs, 1 );
    GDALSetRasterNoDataValue( outBand, std::numeric_limits<float>::quiet_NaN() );

    // Write row by row
    std::vector<float> rowBuf( nCols );
    for ( int row = 0; row < nRows; ++row )
    {
        size_t offset = static_cast<size_t>( row ) * nCols;
        std::memcpy( rowBuf.data(), result.data() + offset, nCols * sizeof( float ) );
        if ( GDALRasterIO( outBand, GF_Write, 0, row, nCols, 1, rowBuf.data(), nCols, 1, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( outDs );
            throw QgsProcessingException( QObject::tr( "Failed to write output raster at row %1" ).arg( row ) );
        }

        if ( feedback->isCanceled() )
        {
            GDALClose( outDs );
            return {};
        }
    }

    GDALClose( outDs );

    feedback->setProgress( 100 );

    QVariantMap results;
    results[QStringLiteral( "OUTPUT" )] = dest;
    return results;
}
