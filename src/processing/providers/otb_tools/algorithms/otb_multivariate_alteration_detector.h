// src/processing/providers/otb_tools/algorithms/otb_multivariate_alteration_detector.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbMultivariateAlterationDetectorAlgorithm : public OtbToolWrapper
{
public:
    OtbMultivariateAlterationDetectorAlgorithm() = default;

    QString name() const override { return "otb_multivariate_alteration_detector"; }
    QString displayName() const override { return "Multivariate Alteration Detector (MAD)"; }
    QString group() const override { return "Change Detection"; }
    QString groupId() const override { return "change_detection"; }
    QStringList tags() const override { return { QObject::tr( "mad" ), QObject::tr( "change detection" ), QObject::tr( "multivariate" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "MultivariateAlterationDetector"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbMultivariateAlterationDetectorAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};