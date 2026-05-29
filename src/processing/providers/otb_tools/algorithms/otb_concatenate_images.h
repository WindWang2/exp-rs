// src/processing/providers/otb_tools/algorithms/otb_concatenate_images.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbConcatenateImagesAlgorithm : public OtbToolWrapper
{
public:
    OtbConcatenateImagesAlgorithm() = default;

    QString name() const override { return "otb_concatenate_images"; }
    QString displayName() const override { return "Concatenate Images"; }
    QString group() const override { return "Utilities"; }
    QString applicationName() const override { return "ConcatenateImages"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
