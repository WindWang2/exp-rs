// src/processing/providers/otb_tools/algorithms/otb_binary_morphological.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbBinaryMorphologicalAlgorithm : public OtbToolWrapper
{
public:
    OtbBinaryMorphologicalAlgorithm() = default;

    QString name() const override { return "otb_binary_morphological"; }
    QString displayName() const override { return "Binary Morphological Operation"; }
    QString group() const override { return "Image Processing"; }
    QString applicationName() const override { return "BinaryMorphologicalOperation"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
