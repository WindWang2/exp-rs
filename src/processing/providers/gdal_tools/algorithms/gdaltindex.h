// src/processing/providers/gdal_tools/algorithms/gdaltindex.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalTindexAlgorithm : public GdalToolWrapper
{
public:
    GdalTindexAlgorithm() = default;

    QString name() const override { return "gdaltindex"; }
    QString displayName() const override { return "GDAL Tile Index (Raster Tile Index)"; }
    QString group() const override { return "Raster Conversion"; }
    QString toolName() const override { return "gdaltindex"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalTindexAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
