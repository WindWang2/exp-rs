// src/processing/providers/gdal_tools/algorithms/gdal_retile.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalRetileAlgorithm : public GdalToolWrapper
{
public:
    GdalRetileAlgorithm() = default;

    QString name() const override { return "gdal_retile"; }
    QString displayName() const override { return "GDAL Retile (Raster Tiling)"; }
    QString group() const override { return "Raster Conversion"; }
    QString toolName() const override { return "gdal_retile.py"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalRetileAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
