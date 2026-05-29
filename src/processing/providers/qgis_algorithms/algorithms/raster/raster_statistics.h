// src/processing/providers/qgis_algorithms/algorithms/raster/raster_statistics.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class RasterStatisticsAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterStatisticsAlgorithm() = default;

    QString name() const override { return QStringLiteral( "raster_statistics" ); }
    QString displayName() const override { return QObject::tr( "Raster Statistics" ); }
    QString group() const override { return QObject::tr( "Raster" ); }
    QString groupId() const override { return QStringLiteral( "raster" ); }
    QString provider() const override { return QStringLiteral( "qgis_algorithms" ); }
    QStringList tags() const override { return { QObject::tr( "raster" ), QObject::tr( "statistics" ), QObject::tr( "min" ), QObject::tr( "max" ), QObject::tr( "mean" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new RasterStatisticsAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
