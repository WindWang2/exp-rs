// src/processing/providers/otb_tools/algorithms/otb_haralick_texture.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbHaralickTextureAlgorithm : public OtbToolWrapper
{
public:
    OtbHaralickTextureAlgorithm() = default;

    QString name() const override { return "otb_haralick_texture"; }
    QString displayName() const override { return "Haralick Texture Extraction"; }
    QString group() const override { return "Feature"; }
    QString applicationName() const override { return "HaralickTextureExtraction"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
