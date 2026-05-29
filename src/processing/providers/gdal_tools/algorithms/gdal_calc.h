// src/processing/providers/gdal_tools/algorithms/gdal_calc.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalCalcAlgorithm : public GdalToolWrapper
{
public:
    GdalCalcAlgorithm() = default;

    QString name() const override { return "gdal_calc"; }
    QString displayName() const override { return "GDAL Calc (Raster Calculator)"; }
    QString group() const override { return "Raster Analysis"; }
    QString toolName() const override { return "gdal_calc.py"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalCalcAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
