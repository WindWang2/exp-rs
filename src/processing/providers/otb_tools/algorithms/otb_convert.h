// src/processing/providers/otb_tools/algorithms/otb_convert.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbConvertAlgorithm : public OtbToolWrapper
{
public:
    OtbConvertAlgorithm() = default;

    QString name() const override { return "otb_convert"; }
    QString displayName() const override { return "Convert"; }
    QString group() const override { return "Utilities"; }
    QString applicationName() const override { return "Convert"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
