// src/processing/providers/qgis_algorithms/algorithms/raster/raster_resample.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class RasterResampleAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterResampleAlgorithm() = default;

    QString name() const override { return QStringLiteral( "raster_resample" ); }
    QString displayName() const override { return QObject::tr( "Resample Raster" ); }
    QString group() const override { return QObject::tr( "Raster" ); }
    QString groupId() const override { return QStringLiteral( "raster" ); }
    QString provider() const override { return QStringLiteral( "qgis_algorithms" ); }
    QStringList tags() const override { return { QObject::tr( "raster" ), QObject::tr( "resample" ), QObject::tr( "resolution" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new RasterResampleAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
