// src/processing/providers/otb_tools/algorithms/otb_gray_scale_morphological.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbGrayScaleMorphologicalAlgorithm : public OtbToolWrapper
{
public:
    OtbGrayScaleMorphologicalAlgorithm() = default;

    QString name() const override { return "otb_gray_scale_morphological"; }
    QString displayName() const override { return "Gray Scale Morphological Operation"; }
    QString group() const override { return "Image Processing"; }
    QString applicationName() const override { return "GrayScaleMorphologicalOperation"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbGrayScaleMorphologicalAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
