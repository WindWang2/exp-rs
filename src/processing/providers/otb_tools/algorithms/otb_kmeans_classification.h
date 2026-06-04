// src/processing/providers/otb_tools/algorithms/otb_kmeans_classification.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbKMeansClassificationAlgorithm : public OtbToolWrapper
{
public:
    OtbKMeansClassificationAlgorithm() = default;

    QString name() const override { return "otb_kmeans_classification"; }
    QString displayName() const override { return "K-Means Classification"; }
    QString group() const override { return "Learning"; }
    QString groupId() const override { return "learning"; }
    QStringList tags() const override { return { QObject::tr( "k-means" ), QObject::tr( "clustering" ), QObject::tr( "classification" ), QObject::tr( "unsupervised" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "KMeansClassification"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbKMeansClassificationAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
