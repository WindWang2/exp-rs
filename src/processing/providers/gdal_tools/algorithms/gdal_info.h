// src/processing/providers/gdal_tools/algorithms/gdal_info.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalInfoAlgorithm : public GdalToolWrapper
{
public:
    GdalInfoAlgorithm() = default;

    QString name() const override { return "gdalinfo"; }
    QString displayName() const override { return "GDAL Info (Raster Information)"; }
    QString group() const override { return "Raster Information"; }
    QString toolName() const override { return "gdalinfo"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
