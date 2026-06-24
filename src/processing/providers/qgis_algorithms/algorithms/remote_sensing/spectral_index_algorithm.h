// src/processing/providers/qgis_algorithms/algorithms/remote_sensing/spectral_index_algorithm.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

/**
 * Processing algorithm wrapper for SpectralIndices::* functions.
 *
 * Computes spectral indices (NDVI, EVI, SAVI, NDWI, NDBI, MNDWI)
 * from input raster bands via the Processing Toolbox.
 */
class SpectralIndexAlgorithm : public QgsProcessingAlgorithm
{
public:
    SpectralIndexAlgorithm() = default;

    QString name() const override { return QStringLiteral( "rs_spectral_index" ); }
    QString displayName() const override { return QObject::tr( "Spectral Index (RS)" ); }
    QString group() const override { return QObject::tr( "Remote Sensing" ); }
    QString groupId() const override { return QStringLiteral( "remotesensing" ); }
    QStringList tags() const override
    {
        return { QObject::tr( "spectral" ), QObject::tr( "index" ), QObject::tr( "ndvi" ),
                 QObject::tr( "evi" ), QObject::tr( "savi" ), QObject::tr( "ndwi" ),
                 QObject::tr( "ndbi" ), QObject::tr( "mndwi" ), QObject::tr( "remote sensing" ) };
    }

    QgsProcessingAlgorithm *createInstance() const override { return new SpectralIndexAlgorithm(); }

    QString shortHelpString() const override;
    QVariantMap metadata() const override;
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;

protected:
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback ) override;
};
