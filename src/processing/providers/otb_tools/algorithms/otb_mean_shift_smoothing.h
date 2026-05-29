// src/processing/providers/otb_tools/algorithms/otb_mean_shift_smoothing.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbMeanShiftSmoothingAlgorithm : public OtbToolWrapper
{
public:
    OtbMeanShiftSmoothingAlgorithm() = default;

    QString name() const override { return "otb_mean_shift_smoothing"; }
    QString displayName() const override { return "Mean Shift Smoothing"; }
    QString group() const override { return "Filtering"; }
    QString applicationName() const override { return "MeanShiftSmoothing"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
