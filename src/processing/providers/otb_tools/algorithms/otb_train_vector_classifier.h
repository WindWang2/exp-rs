// src/processing/providers/otb_tools/algorithms/otb_train_vector_classifier.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbTrainVectorClassifierAlgorithm : public OtbToolWrapper
{
public:
    OtbTrainVectorClassifierAlgorithm() = default;

    QString name() const override { return "otb_train_vector_classifier"; }
    QString displayName() const override { return "Train Vector Classifier"; }
    QString group() const override { return "Learning"; }
    QString applicationName() const override { return "TrainVectorClassifier"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
