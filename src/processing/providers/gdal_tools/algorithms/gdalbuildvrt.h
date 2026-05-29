// src/processing/providers/gdal_tools/algorithms/gdalbuildvrt.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalBuildVrtAlgorithm : public GdalToolWrapper
{
public:
    GdalBuildVrtAlgorithm() = default;

    QString name() const override { return "gdalbuildvrt"; }
    QString displayName() const override { return "GDAL Build VRT (Virtual Raster)"; }
    QString group() const override { return "Raster Conversion"; }
    QString toolName() const override { return "gdalbuildvrt"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
