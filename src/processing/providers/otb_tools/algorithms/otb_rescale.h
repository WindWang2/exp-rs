// src/processing/providers/otb_tools/algorithms/otb_rescale.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbRescaleAlgorithm : public OtbToolWrapper
{
public:
    OtbRescaleAlgorithm() = default;

    QString name() const override { return "otb_rescale"; }
    QString displayName() const override { return "Rescale"; }
    QString group() const override { return "Utilities"; }
    QString applicationName() const override { return "Rescale"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbRescaleAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
