// src/processing/providers/otb_tools/algorithms/otb_ortho_rectification.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbOrthoRectificationAlgorithm : public OtbToolWrapper
{
public:
    OtbOrthoRectificationAlgorithm() = default;

    QString name() const override { return "otb_ortho_rectification"; }
    QString displayName() const override { return "Ortho Rectification"; }
    QString group() const override { return "Geometry"; }
    QString applicationName() const override { return "OrthoRectification"; }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
