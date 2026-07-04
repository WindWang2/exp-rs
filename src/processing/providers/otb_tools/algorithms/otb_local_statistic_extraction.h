// src/processing/providers/otb_tools/algorithms/otb_local_statistic_extraction.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbLocalStatisticExtractionAlgorithm : public OtbToolWrapper
{
public:
    OtbLocalStatisticExtractionAlgorithm() = default;

    QString name() const override { return "otb_local_statistic_extraction"; }
    QString displayName() const override { return "Local Statistic Extraction"; }
    QString group() const override { return "Feature"; }
    QString groupId() const override { return "feature"; }
    QStringList tags() const override { return { QObject::tr( "statistics" ), QObject::tr( "local" ), QObject::tr( "feature" ), QObject::tr( "extraction" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "LocalStatisticExtraction"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbLocalStatisticExtractionAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};