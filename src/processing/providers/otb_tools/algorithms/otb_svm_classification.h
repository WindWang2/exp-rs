// src/processing/providers/otb_tools/algorithms/otb_svm_classification.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbSvmClassificationAlgorithm : public OtbToolWrapper
{
public:
    OtbSvmClassificationAlgorithm() = default;

    QString name() const override { return "otb_svm_classification"; }
    QString displayName() const override { return "SVM Classification (Train)"; }
    QString group() const override { return "Learning"; }
    QString groupId() const override { return "learning"; }
    QStringList tags() const override { return { QObject::tr( "svm" ), QObject::tr( "classification" ), QObject::tr( "train" ), QObject::tr( "machine learning" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "TrainImagesClassifier"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbSvmClassificationAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};