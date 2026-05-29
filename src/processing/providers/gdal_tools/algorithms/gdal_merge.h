// src/processing/providers/gdal_tools/algorithms/gdal_merge.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalMergeAlgorithm : public GdalToolWrapper
{
public:
    GdalMergeAlgorithm() = default;

    QString name() const override { return "gdal_merge"; }
    QString displayName() const override { return "GDAL Merge (Merge Rasters)"; }
    QString group() const override { return "Raster Conversion"; }
    QString toolName() const override { return "gdal_merge.py"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
