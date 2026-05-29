// src/processing/providers/otb_tools/algorithms/otb_lsms.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbLsmsAlgorithm : public OtbToolWrapper
{
public:
    OtbLsmsAlgorithm() = default;

    QString name() const override { return "otb_lsms"; }
    QString displayName() const override { return "LSMS Segmentation"; }
    QString group() const override { return "Filtering"; }
    QString applicationName() const override { return "LSMS"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
