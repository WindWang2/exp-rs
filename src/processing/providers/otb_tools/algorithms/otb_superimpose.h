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
    QString groupId() const override { return "geometry"; }
    QStringList tags() const override { return { QObject::tr( "superimpose" ), QObject::tr( "registration" ), QObject::tr( "geometry" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "Superimpose"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbSuperimposeAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
