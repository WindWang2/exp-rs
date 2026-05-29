// src/processing/providers/qgis_algorithms/algorithms/raster/raster_merge_bands.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class RasterMergeBandsAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterMergeBandsAlgorithm() = default;

    QString name() const override { return QStringLiteral( "raster_merge_bands" ); }
    QString displayName() const override { return QObject::tr( "Merge Raster Bands" ); }
    QString group() const override { return QObject::tr( "Raster" ); }
    QString groupId() const override { return QStringLiteral( "raster" ); }
    QString provider() const override { return QStringLiteral( "qgis_algorithms" ); }
    QStringList tags() const override { return { QObject::tr( "raster" ), QObject::tr( "merge" ), QObject::tr( "bands" ), QObject::tr( "multiband" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new RasterMergeBandsAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
