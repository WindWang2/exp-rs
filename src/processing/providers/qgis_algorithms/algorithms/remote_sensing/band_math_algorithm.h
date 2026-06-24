// src/processing/providers/qgis_algorithms/algorithms/remote_sensing/band_math_algorithm.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

/**
 * Processing algorithm wrapper for BandMath::evaluate().
 *
 * Allows evaluating arbitrary math expressions on multi-band raster data
 * from the Processing Toolbox. Bands are referenced as b1, b2, ... bN
 * in order across all input layers.
 */
class BandMathAlgorithm : public QgsProcessingAlgorithm
{
public:
    BandMathAlgorithm() = default;

    QString name() const override { return QStringLiteral( "rs_band_math" ); }
    QString displayName() const override { return QObject::tr( "Band Math (RS)" ); }
    QString group() const override { return QObject::tr( "Remote Sensing" ); }
    QString groupId() const override { return QStringLiteral( "remotesensing" ); }
    QStringList tags() const override
    {
        return { QObject::tr( "band" ), QObject::tr( "math" ), QObject::tr( "expression" ),
                 QObject::tr( "raster" ), QObject::tr( "remote sensing" ) };
    }

    QgsProcessingAlgorithm *createInstance() const override { return new BandMathAlgorithm(); }

    QString shortHelpString() const override;
    QVariantMap metadata() const override;
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;

protected:
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback ) override;
};
