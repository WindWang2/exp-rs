// src/processing/providers/otb_tools/algorithms/otb_multi_resolution_pyramid.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbMultiResolutionPyramidAlgorithm : public OtbToolWrapper
{
public:
    OtbMultiResolutionPyramidAlgorithm() = default;

    QString name() const override { return "otb_multi_resolution_pyramid"; }
    QString displayName() const override { return "Multi-Resolution Pyramid"; }
    QString group() const override { return "Image Processing"; }
    QString groupId() const override { return "imageprocessing"; }
    QStringList tags() const override { return { QObject::tr( "pyramid" ), QObject::tr( "multi-resolution" ), QObject::tr( "resampling" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "MultiResolutionPyramid"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbMultiResolutionPyramidAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
