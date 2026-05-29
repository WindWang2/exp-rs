// src/processing/providers/otb_tools/algorithms/otb_image_classifier.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbImageClassifierAlgorithm : public OtbToolWrapper
{
public:
    OtbImageClassifierAlgorithm() = default;

    QString name() const override { return "otb_image_classifier"; }
    QString displayName() const override { return "Image Classifier"; }
    QString group() const override { return "Learning"; }
    QString applicationName() const override { return "ImageClassifier"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
