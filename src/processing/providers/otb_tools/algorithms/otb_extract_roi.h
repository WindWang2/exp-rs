// src/processing/providers/otb_tools/algorithms/otb_extract_roi.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbExtractRoiAlgorithm : public OtbToolWrapper
{
public:
    OtbExtractRoiAlgorithm() = default;

    QString name() const override { return "otb_extract_roi"; }
    QString displayName() const override { return "Extract ROI (Region of Interest)"; }
    QString group() const override { return "Utilities"; }
    QString applicationName() const override { return "ExtractROI"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
