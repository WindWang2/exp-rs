// src/processing/providers/otb_tools/algorithms/otb_read_image_info.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbReadImageInfoAlgorithm : public OtbToolWrapper
{
public:
    OtbReadImageInfoAlgorithm() = default;

    QString name() const override { return "otb_read_image_info"; }
    QString displayName() const override { return "Read Image Info"; }
    QString group() const override { return "Utilities"; }
    QString groupId() const override { return "utilities"; }
    QStringList tags() const override { return { QObject::tr( "image" ), QObject::tr( "info" ), QObject::tr( "metadata" ), QObject::tr( "utilities" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "ReadImageInfo"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbReadImageInfoAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;

    // Override to avoid requiring OUTPUT parameter
    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;
};
