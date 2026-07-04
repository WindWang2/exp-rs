// src/processing/providers/gdal_tools/algorithms/gdaltransform.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalTransformAlgorithm : public GdalToolWrapper
{
public:
    GdalTransformAlgorithm() = default;

    QString name() const override { return "gdaltransform"; }
    QString displayName() const override { return "GDAL Transform Coordinates"; }
    QString group() const override { return "Raster Transformation"; }
    QString groupId() const override { return "rastertransformation"; }
    QStringList tags() const override { return { QObject::tr( "gdaltransform" ), QObject::tr( "transform" ), QObject::tr( "coordinates" ), QObject::tr( "crs" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdaltransform"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalTransformAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

    QVariantMap processAlgorithm(const QVariantMap &parameters,
                               QgsProcessingContext &context,
                               QgsProcessingFeedback *feedback) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};