// src/processing/providers/gdal_tools/algorithms/gdal_contour.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalContourAlgorithm : public GdalToolWrapper
{
public:
    GdalContourAlgorithm() = default;

    QString name() const override { return "gdal_contour"; }
    QString displayName() const override { return "GDAL Contour (Raster to Contours)"; }
    QString group() const override { return "Raster Conversion"; }
    QString toolName() const override { return "gdal_contour"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
