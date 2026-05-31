// src/processing/providers/gdal_tools/algorithms/gdal_grid.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalGridAlgorithm : public GdalToolWrapper
{
public:
    GdalGridAlgorithm() = default;

    QString name() const override { return "gdal_grid"; }
    QString displayName() const override { return "GDAL Grid (Point to Raster)"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QString toolName() const override { return "gdal_grid"; }
    QString shortHelpString() const override
    {
        return QObject::tr("Creates regular raster grid from scattered point data. Supports "
                           "multiple gridding algorithms including nearest neighbour, moving "
                           "average, inverse distance to a power, and metric (minimum/maximum) "
                           "methods. Input is a vector point layer.");
    }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalGridAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
