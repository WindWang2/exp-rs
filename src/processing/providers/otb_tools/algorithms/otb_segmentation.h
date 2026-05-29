// src/processing/providers/otb_tools/algorithms/otb_segmentation.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbSegmentationAlgorithm : public OtbToolWrapper
{
public:
    OtbSegmentationAlgorithm() = default;

    QString name() const override { return "otb_segmentation"; }
    QString displayName() const override { return "Image Segmentation"; }
    QString group() const override { return "Segmentation"; }
    QString applicationName() const override { return "Segmentation"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbSegmentationAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
