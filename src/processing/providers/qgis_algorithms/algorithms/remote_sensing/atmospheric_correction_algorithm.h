// src/processing/providers/qgis_algorithms/algorithms/remote_sensing/atmospheric_correction_algorithm.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

/**
 * Processing algorithm wrapper for AtmosphericCorrection::* functions.
 *
 * Applies atmospheric correction (DN-to-radiance, DOS1, DOS2) to optical
 * remote sensing imagery via the Processing Toolbox.
 */
class AtmosphericCorrectionAlgorithm : public QgsProcessingAlgorithm
{
public:
    AtmosphericCorrectionAlgorithm() = default;

    QString name() const override { return QStringLiteral( "rs_atmospheric_correction" ); }
    QString displayName() const override { return QObject::tr( "Atmospheric Correction (RS)" ); }
    QString group() const override { return QObject::tr( "Remote Sensing" ); }
    QString groupId() const override { return QStringLiteral( "remotesensing" ); }
    QStringList tags() const override
    {
        return { QObject::tr( "atmospheric" ), QObject::tr( "correction" ), QObject::tr( "dos" ),
                 QObject::tr( "radiance" ), QObject::tr( "reflectance" ), QObject::tr( "remote sensing" ) };
    }

    QgsProcessingAlgorithm *createInstance() const override { return new AtmosphericCorrectionAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback ) override;
};
