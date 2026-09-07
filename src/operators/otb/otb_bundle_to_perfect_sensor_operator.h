/***************************************************************************
 * otb_bundle_to_perfect_sensor_operator.h — OTB BundleToPerfectSensor
 *
 * Thin adapter over OtbOperatorBase for pan-sharpening: the fusion dialog's
 * former inline QProcess lambda (thin-client migration, Desktop Workbench
 * UX 4.0). Args follow the otbcli_BundleToPerfectSensor contract:
 *   -in <multispectral> -inp <panchromatic> -out <output>
 ***************************************************************************/
#pragma once

#include "otb_operator_base.h"

namespace sicnu::operators::otb {

class OtbBundleToPerfectSensorOperator : public OtbOperatorBase {
public:
    std::string name() const override { return "otb:bundle_to_perfect_sensor"; }
    std::string displayName() const override { return "OTB Bundle To Perfect Sensor"; }
    std::string group() const override { return "fusion"; }
    std::string description() const override {
        return "Pan-sharpen a multispectral image with a panchromatic image (OTB).";
    }

protected:
    QString otbApplicationName() const override { return QStringLiteral( "BundleToPerfectSensor" ); }

    QStringList buildOtbArgs( const Json::Value& params, RSOperatorContext& context ) const override;
};

} // namespace sicnu::operators::otb
