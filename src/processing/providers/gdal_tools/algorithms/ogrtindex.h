// src/processing/providers/gdal_tools/algorithms/ogrtindex.h
#pragma once

#include "../gdal_tool_wrapper.h"

class OgrTindexAlgorithm : public GdalToolWrapper
{
public:
    OgrTindexAlgorithm() = default;

    QString name() const override { return "ogrtindex"; }
    QString displayName() const override { return "OGR Tile Index (Vector Tile Index)"; }
    QString group() const override { return "Vector Conversion"; }
    QString toolName() const override { return "ogrtindex"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
