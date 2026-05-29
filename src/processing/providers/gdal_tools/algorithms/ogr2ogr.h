// src/processing/providers/gdal_tools/algorithms/ogr2ogr.h
#pragma once

#include "../gdal_tool_wrapper.h"

class Ogr2OgrAlgorithm : public GdalToolWrapper
{
public:
    Ogr2OgrAlgorithm() = default;

    QString name() const override { return "ogr2ogr"; }
    QString displayName() const override { return "OGR2OGR (Vector Format Conversion)"; }
    QString group() const override { return "Vector Conversion"; }
    QString toolName() const override { return "ogr2ogr"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
