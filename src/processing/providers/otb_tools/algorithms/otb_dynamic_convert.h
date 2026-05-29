// src/processing/providers/otb_tools/algorithms/otb_dynamic_convert.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbDynamicConvertAlgorithm : public OtbToolWrapper
{
public:
    OtbDynamicConvertAlgorithm() = default;

    QString name() const override { return "otb_dynamic_convert"; }
    QString displayName() const override { return "Dynamic Convert"; }
    QString group() const override { return "Utilities"; }
    QString applicationName() const override { return "DynamicConvert"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
