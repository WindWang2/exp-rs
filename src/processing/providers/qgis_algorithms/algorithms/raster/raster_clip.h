// src/processing/providers/qgis_algorithms/algorithms/raster/raster_clip.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class RasterClipAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterClipAlgorithm() = default;

    QString name() const override { return QStringLiteral( "raster_clip" ); }
    QString displayName() const override { return QObject::tr( "Clip Raster" ); }
    QString group() const override { return QObject::tr( "Raster" ); }
    QString groupId() const override { return QStringLiteral( "raster" ); }
    QString provider() const override { return QStringLiteral( "qgis_algorithms" ); }
    QStringList tags() const override { return { QObject::tr( "raster" ), QObject::tr( "clip" ), QObject::tr( "extent" ) }; }

    QgsProcessingAlgorithm *createInstance() const override { return new RasterClipAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
