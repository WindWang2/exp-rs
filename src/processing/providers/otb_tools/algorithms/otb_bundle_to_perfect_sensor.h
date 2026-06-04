// src/processing/providers/otb_tools/algorithms/otb_bundle_to_perfect_sensor.h
#pragma once

#include "../otb_tool_wrapper.h"

class OtbBundleToPerfectSensorAlgorithm : public OtbToolWrapper
{
public:
    OtbBundleToPerfectSensorAlgorithm() = default;

    QString name() const override { return "otb_bundle_to_perfect_sensor"; }
    QString displayName() const override { return "Bundle to Perfect Sensor"; }
    QString group() const override { return "Geometry"; }
    QString groupId() const override { return "geometry"; }
    QStringList tags() const override { return { QObject::tr( "bundle" ), QObject::tr( "sensor" ), QObject::tr( "pan-sharpening" ), QObject::tr( "geometry" ), QObject::tr( "otb" ) }; }
    QString applicationName() const override { return "BundleToPerfectSensor"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OtbBundleToPerfectSensorAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
