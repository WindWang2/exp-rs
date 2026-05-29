// src/processing/providers/otb_tools/algorithms/otb_feature_extraction.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbFeatureExtractionAlgorithm : public OtbToolWrapper
{
public:
    OtbFeatureExtractionAlgorithm() = default;

    QString name() const override { return "otb_feature_extraction"; }
    QString displayName() const override { return "Feature Extraction"; }
    QString group() const override { return "Feature"; }
    QString applicationName() const override { return "FeatureExtraction"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
