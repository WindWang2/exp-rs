// src/processing/providers/otb_tools/algorithms/otb_radiometric_indices.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbRadiometricIndicesAlgorithm : public OtbToolWrapper
{
public:
    OtbRadiometricIndicesAlgorithm() = default;

    QString name() const override { return "otb_radiometric_indices"; }
    QString displayName() const override { return "Radiometric Indices"; }
    QString group() const override { return "Feature"; }
    QString groupId() const override { return "feature"; }
    QStringList tags() const override { return { QObject::tr( "radiometric" ), QObject::tr( "indices" ), QObject::tr( "ndvi" ), QObject::tr( "vegetation" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "RadiometricIndices"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbRadiometricIndicesAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
