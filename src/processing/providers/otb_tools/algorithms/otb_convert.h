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
    QString groupId() const override { return "utilities"; }
    QStringList tags() const override { return { QObject::tr( "convert" ), QObject::tr( "pixel type" ), QObject::tr( "utilities" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "Convert"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbConvertAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
