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
    QString groupId() const override { return "utilities"; }
    QStringList tags() const override { return { QObject::tr( "extract" ), QObject::tr( "roi" ), QObject::tr( "crop" ), QObject::tr( "clip" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "ExtractROI"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbExtractRoiAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
