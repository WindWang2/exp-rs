// src/processing/providers/qgis_algorithms/algorithms/raster/raster_ndvi.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class RasterNdviAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterNdviAlgorithm() = default;

    QString name() const override { return QStringLiteral( "raster_ndvi" ); }
    QString displayName() const override { return QObject::tr( "Calculate NDVI" ); }
    QString group() const override { return QObject::tr( "Raster" ); }
    QString groupId() const override { return QStringLiteral( "raster" ); }
    QString provider() const override { return QStringLiteral( "qgis_algorithms" ); }
    QStringList tags() const override { return { QObject::tr( "raster" ), QObject::tr( "ndvi" ), QObject::tr( "vegetation" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new RasterNdviAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
