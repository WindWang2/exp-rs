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
    QString groupId() const override { return "geometry"; }
    QStringList tags() const override { return { QObject::tr( "ortho" ), QObject::tr( "rectification" ), QObject::tr( "geometry" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "OrthoRectification"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbOrthoRectificationAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
