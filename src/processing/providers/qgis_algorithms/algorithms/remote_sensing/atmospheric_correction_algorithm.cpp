// src/processing/providers/qgis_algorithms/algorithms/remote_sensing/atmospheric_correction_algorithm.cpp
#include "atmospheric_correction_algorithm.h"

#include "../../../../algorithms/atmospheric_correction.h"

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

#include <vector>

void AtmosphericCorrectionAlgorithm::initAlgorithm( const QVariantMap & )
{
    QStringList methodOptions;
    methodOptions << QStringLiteral( "DN to Radiance" )
                  << QStringLiteral( "DOS1 (Dark Object Subtraction)" )
                  << QStringLiteral( "DOS2 (DOS + Transmittance)" )
                  << QStringLiteral( "QUAC (Quick Atmospheric Correction)" );

    addParameter( new QgsProcessingParameterRasterLayer(
        QStringLiteral( "INPUT" ), QObject::tr( "Input raster (DN values)" ) ) );
    addParameter( new QgsProcessingParameterEnum(
        QStringLiteral( "METHOD" ), QObject::tr( "Correction method" ), methodOptions, false, 0 ) );
    addParameter( new QgsProcessingParameterNumber(
        QStringLiteral( "GAIN" ), QObject::tr( "Radiance gain" ),
        Qgis::ProcessingNumberParameterType::Double, 1.0 ) );
    addParameter( new QgsProcessingParameterNumber(
        QStringLiteral( "BIAS" ), QObject::tr( "Radiance bias" ),
        Qgis::ProcessingNumberParameterType::Double, 0.0 ) );
    addParameter( new QgsProcessingParameterNumber(
        QStringLiteral( "TRANSMITTANCE" ), QObject::tr( "Atmospheric transmittance (DOS2 only)" ),
        Qgis::ProcessingNumberParameterType::Double, 0.8, false, 0.0, 1.0 ) );
    addParameter( new QgsProcessingParameterRasterDestination(
        QStringLiteral( "OUTPUT" ), QObject::tr( "Corrected raster" ) ) );
}

QVariantMap AtmosphericCorrectionAlgorithm::processAlgorithm( const QVariantMap &parameters,
    QgsProcessingContext &context, QgsProcessingFeedback *feedback )
{
    QgsRasterLayer *inputLayer = parameterAsRasterLayer( parameters, QStringLiteral( "INPUT" ), context );
    if ( !inputLayer || !inputLayer->dataProvider() )
        throw QgsProcessingException( invalidRasterError( parameters, QStringLiteral( "INPUT" ) ) );

    int method = parameterAsEnum( parameters, QStringLiteral( "METHOD" ), context );
    float gain = static_cast<float>( parameterAsDouble( parameters, QStringLiteral( "GAIN" ), context ) );
    float bias = static_cast<float>( parameterAsDouble( parameters, QStringLiteral( "BIAS" ), context ) );
    float transmittance = static_cast<float>( parameterAsDouble( parameters, QStringLiteral( "TRANSMITTANCE" ), context ) );

    QgsRasterDataProvider *provider = inputLayer->dataProvider();
    QgsRectangle extent = inputLayer->extent();
    int nCols = inputLayer->width();
    int nRows = inputLayer->height();
    QgsCoordinateReferenceSystem crs = inputLayer->crs();

    if ( nCols <= 0 || nRows <= 0 )
        throw QgsProcessingException( QObject::tr( "Invalid raster dimensions" ) );

    QString dest = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

    // QUAC is multi-band: delegate to processFileMultiBand which owns
    // its own GDAL I/O, bypassing the single-band QgsRasterBlock path below.
    if ( method == AtmosphericCorrection::Quac )
    {
        feedback->setProgressText( QObject::tr( "Running QUAC (multi-band)..." ) );
        QString error;
        if ( !AtmosphericCorrection::processFileMultiBand(
                inputLayer->source(), dest, method, &error,
                [feedback]( double frac, const QString & msg ) {
                    feedback->setProgressText( msg );
                    feedback->setProgress( static_cast<int>( frac * 100 ) );
                } ) )
            throw QgsProcessingException( error );

        feedback->setProgress( 100 );
        return QVariantMap{ { QStringLiteral( "OUTPUT" ), dest } };
    }

    size_t totalPixels = static_cast<size_t>( nCols ) * static_cast<size_t>( nRows );

    // Read input DN band
    feedback->setProgressText( QObject::tr( "Reading input raster..." ) );
    std::unique_ptr<QgsRasterBlock> inBlock( provider->block( 1, extent, nCols, nRows ) );
    if ( !inBlock || !inBlock->isValid() )
        throw QgsProcessingException( QObject::tr( "Could not read input raster band" ) );

    std::vector<float> dnData( totalPixels );
    for ( size_t i = 0; i < totalPixels; ++i )
    {
        int row = static_cast<int>( i / nCols );
        int col = static_cast<int>( i % nCols );
        if ( inBlock->isNoData( row, col ) )
        {
            dnData[i] = std::numeric_limits<float>::quiet_NaN();
        }
        else
        {
            dnData[i] = static_cast<float>( inBlock->value( row, col ) );
        }
    }

    feedback->setProgress( 30 );

    // Apply atmospheric correction
    feedback->setProgressText( QObject::tr( "Applying atmospheric correction..." ) );
    std::vector<float> result( totalPixels );
    bool ok = false;

    switch ( method )
    {
        case AtmosphericCorrection::DnToRadiance:
            ok = AtmosphericCorrection::dnToRadiance( dnData.data(), result.data(), totalPixels, gain, bias );
            break;
        case AtmosphericCorrection::Dos1:
            ok = AtmosphericCorrection::dos1( dnData.data(), result.data(), totalPixels, gain, bias );
            break;
        case AtmosphericCorrection::Dos2:
            if ( transmittance <= 0.0f || transmittance > 1.0f )
                throw QgsProcessingException( QObject::tr( "Transmittance must be in range (0, 1]" ) );
            ok = AtmosphericCorrection::dos2( dnData.data(), result.data(), totalPixels, gain, bias, transmittance );
            break;
        default:
            throw QgsProcessingException( QObject::tr( "Unknown correction method" ) );
    }

    if ( !ok )
        throw QgsProcessingException( QObject::tr( "Atmospheric correction failed" ) );

    feedback->setProgress( 70 );

    // Write output raster
    feedback->setProgressText( QObject::tr( "Writing output raster..." ) );

    QgsRasterFileWriter writer( dest );
    writer.setOutputFormat( QStringLiteral( "GTiff" ) );
    std::unique_ptr<QgsRasterDataProvider> outProvider(
        writer.createOneBandRaster( Qgis::DataType::Float32, nCols, nRows, extent, crs ) );
    if ( !outProvider )
        throw QgsProcessingException( QObject::tr( "Could not create output raster" ) );

    outProvider->setNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );
    QgsRasterBlock outBlock( Qgis::DataType::Float32, nCols, nRows );
    outBlock.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
    for ( int row = 0; row < nRows; ++row )
    {
        for ( int col = 0; col < nCols; ++col )
        {
            outBlock.setValue( row, col, static_cast<double>( result[static_cast<size_t>( row ) * nCols + col] ) );
        }
    }

    if ( !outProvider->writeBlock( &outBlock, 1 ) )
        throw QgsProcessingException( QObject::tr( "Error writing output raster" ) );

    feedback->setProgress( 100 );

    return QVariantMap{{QStringLiteral( "OUTPUT" ), dest}};
}

QString AtmosphericCorrectionAlgorithm::shortHelpString() const
{
    return QObject::tr( "Performs atmospheric correction on a raster layer using DOS1, DOS2, or QUAC (Quick Atmospheric Correction) methods to convert digital numbers to surface reflectance." );
}

QVariantMap AtmosphericCorrectionAlgorithm::metadata() const
{
    return QVariantMap{
        { QStringLiteral( "purpose" ), QObject::tr( "Applies simple atmospheric correction to satellite imagery." ) },
        { QStringLiteral( "useCases" ), QStringList{ QObject::tr( "Converting raw DN values to surface reflectance" ), QObject::tr( "Pre-processing multi-temporal imagery for change detection" ) } },
        { QStringLiteral( "prerequisites" ), QStringList{ QObject::tr( "Input raster with raw DN values." ) } },
        { QStringLiteral( "limitations" ), QStringList{ QObject::tr( "Assumes the presence of a dark object in the scene." ), QObject::tr( "DOS2 requires sun elevation and acquisition details." ) } },
        { QStringLiteral( "workflowHints" ), QStringList{ QObject::tr( "Should be run before computing spectral indices or performing classification to ensure physical consistency of reflectance values." ) } }
    };
}
