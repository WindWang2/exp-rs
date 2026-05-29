// src/processing/providers/gdal_tools/algorithms/gdal_warp.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalWarpAlgorithm : public GdalToolWrapper
{
public:
    GdalWarpAlgorithm() = default;

    QString name() const override { return "gdalwarp"; }
    QString displayName() const override { return "GDAL Warp (Reproject/Clip)"; }
    QString group() const override { return "Raster Transformation"; }
    QString toolName() const override { return "gdalwarp"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
