// src/processing/providers/gdal_tools/algorithms/ogrinfo.h
#pragma once

#include "../gdal_tool_wrapper.h"

class OgrInfoAlgorithm : public GdalToolWrapper
{
public:
    OgrInfoAlgorithm() = default;

    QString name() const override { return "ogrinfo"; }
    QString displayName() const override { return "OGR Info (Vector Information)"; }
    QString group() const override { return "Vector Information"; }
    QString toolName() const override { return "ogrinfo"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OgrInfoAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
