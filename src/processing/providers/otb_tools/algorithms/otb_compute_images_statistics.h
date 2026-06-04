// src/processing/providers/otb_tools/algorithms/otb_compute_images_statistics.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbComputeImagesStatisticsAlgorithm : public OtbToolWrapper
{
public:
    OtbComputeImagesStatisticsAlgorithm() = default;

    QString name() const override { return "otb_compute_images_statistics"; }
    QString displayName() const override { return "Compute Images Statistics"; }
    QString group() const override { return "Radiometry"; }
    QString groupId() const override { return "radiometry"; }
    QStringList tags() const override { return { QObject::tr( "statistics" ), QObject::tr( "mean" ), QObject::tr( "variance" ), QObject::tr( "radiometry" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "ComputeImagesStatistics"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbComputeImagesStatisticsAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
