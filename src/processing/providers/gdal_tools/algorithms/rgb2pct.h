// src/processing/providers/gdal_tools/algorithms/rgb2pct.h
#pragma once

#include "../gdal_tool_wrapper.h"

class Rgb2PctAlgorithm : public GdalToolWrapper
{
public:
    Rgb2PctAlgorithm() = default;

    QString name() const override { return "rgb2pct"; }
    QString displayName() const override { return "GDAL RGB to PCT"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QStringList tags() const override
    {
        return { QObject::tr( "rgb2pct" ), QObject::tr( "palette" ), QObject::tr( "rgb" ),
                 QObject::tr( "color table" ), QObject::tr( "gdal" ) };
    }
    QString toolName() const override { return "rgb2pct.py"; }

    QgsProcessingAlgorithm *createInstance() const override { return new Rgb2PctAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};