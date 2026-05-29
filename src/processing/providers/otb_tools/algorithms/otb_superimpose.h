// src/processing/providers/otb_tools/algorithms/otb_superimpose.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbSuperimposeAlgorithm : public OtbToolWrapper
{
public:
    OtbSuperimposeAlgorithm() = default;

    QString name() const override { return "otb_superimpose"; }
    QString displayName() const override { return "Superimpose"; }
    QString group() const override { return "Geometry"; }
    QString applicationName() const override { return "Superimpose"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
