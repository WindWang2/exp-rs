// src/processing/providers/gdal_tools/algorithms/pct2rgb.h
#pragma once

#include "../gdal_tool_wrapper.h"

class Pct2RgbAlgorithm : public GdalToolWrapper
{
public:
    Pct2RgbAlgorithm() = default;

    QString name() const override { return "pct2rgb"; }
    QString displayName() const override { return "GDAL PCT to RGB"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QStringList tags() const override
    {
        return { QObject::tr( "pct2rgb" ), QObject::tr( "palette" ), QObject::tr( "rgb" ),
                 QObject::tr( "color table" ), QObject::tr( "gdal" ) };
    }
    QString toolName() const override { return "pct2rgb.py"; }

    QgsProcessingAlgorithm *createInstance() const override { return new Pct2RgbAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};