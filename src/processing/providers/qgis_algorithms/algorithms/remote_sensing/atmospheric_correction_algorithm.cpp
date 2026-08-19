// src/processing/providers/qgis_algorithms/algorithms/remote_sensing/atmospheric_correction_algorithm.cpp
#include "atmospheric_correction_algorithm.h"

#include "../../../../algorithms/atmospheric_correction.h"
#include "../../../../algorithms/radiometric_calibration.h"

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
        Qgis::ProcessingNumberParameterType::Double,
        static_cast<double>(AtmosphericCorrection::estimateTransmittance(1.0f)), false, 0.0, 1.0 ) );
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

    // For DOS/DN-to-radiance: resolve gain/bias from metadata when the caller
    // left the defaults (1/0) — mirrors RsAtmosphericCorrectionOperator which
    // throws if unresolved. This closes the silent identity-fallback gap (#301/#367).
    {
        const bool isDefaultGainBias = (gain == 1.0f && bias == 0.0f);
        if (isDefaultGainBias) {
            const QString metaPath = RadiometricCalibration::autoDetectMetadataFile(inputLayer->source());
            if (!metaPath.isEmpty()) {
                RadiometricCalibration::CalibrationMetadata meta;
                QString metaErr;
                QMap<int, QString> bandNames;
                bandNames.insert(1, QStringLiteral("B1"));
                if (RadiometricCalibration::loadMetadata(inputLayer->source(), metaPath, bandNames, &meta, &metaErr)
                    && meta.bands.contains(1)) {
                    const auto &c = meta.bands.value(1);
                    if (c.hasRadiance) {
                        gain = static_cast<float>(c.radianceGain);
                        bias = static_cast<float>(c.radianceBias);
                    }
                }
            }
            if (gain == 1.0f && bias == 0.0f) {
                // No metadata resolved and caller left defaults -> fail closed like the operator.
                throw QgsProcessingException(
                    QObject::tr("Radiance gain/bias unresolved: provide GAIN/BIAS or place MTL/MTD next to input"));
            }
        }
    }

    // Delegate to the streaming processFile which uses the histogram-based
    // dark-object estimator (Chavez 1996) — same kernel as the rs: operator.
    // This eliminates the previous divergence where the provider used the naive
    // global-min dos1/dos2 kernels.
    {
        float airmass = 1.0f;
        if (method == AtmosphericCorrection::Dos2) {
            if (transmittance <= 0.0f || transmittance > 1.0f)
                throw QgsProcessingException(QObject::tr("Transmittance must be in range (0, 1]"));
            // Invert estimateTransmittance to recover airmass: airmass = -ln(T)/0.1
            airmass = static_cast<float>(-std::log(static_cast<double>(transmittance)) / 0.1);
            if (!std::isfinite(airmass) || airmass <= 0.0f)
                airmass = 1.0f;
        }
        feedback->setProgressText(QObject::tr("Running atmospheric correction (streaming)..."));
        QString error;
        bool ok = AtmosphericCorrection::processFile(
            inputLayer->source(), dest, 1, method, gain, bias, airmass, &error);
        if (!ok)
            throw QgsProcessingException(
                error.isEmpty() ? QObject::tr("Atmospheric correction failed") : error);
        feedback->setProgress(100);
        return QVariantMap{{QStringLiteral("OUTPUT"), dest}};
    }
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
