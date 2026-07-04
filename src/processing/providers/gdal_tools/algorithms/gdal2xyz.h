// src/processing/providers/gdal_tools/algorithms/gdal2xyz.h
#pragma once

#include "../gdal_tool_wrapper.h"

class Gdal2XyzAlgorithm : public GdalToolWrapper
{
public:
    Gdal2XyzAlgorithm() = default;

    QString name() const override { return "gdal2xyz"; }
    QString displayName() const override { return "GDAL Raster to XYZ"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QStringList tags() const override
    {
        return { QObject::tr( "gdal2xyz" ), QObject::tr( "xyz" ), QObject::tr( "ascii" ),
                 QObject::tr( "export" ), QObject::tr( "gdal" ) };
    }
    QString toolName() const override { return "gdal2xyz.py"; }

    QgsProcessingAlgorithm *createInstance() const override { return new Gdal2XyzAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};