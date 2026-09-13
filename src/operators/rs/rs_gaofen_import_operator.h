/***************************************************************************
 * rs_gaofen_import_operator.h — Import Gaofen (GF-1/2/6) L1A → multi-band GeoTIFF
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Imports a Gaofen-1/2/6 L1A product (PMS multispectral/panchromatic or WFV;
 * CRESDA sidecar XML + TIFF) into a stacked, role-tagged, calibration-annotated
 * multi-band GeoTIFF for teaching / Agent use.
 *
 * Parameters:
 *   input   (string, required)  GF product dir, *_MSS*.xml/_PAN*.xml or .tiff
 *   output  (string, required)  Output multi-band GeoTIFF
 *   bands   (array, optional)   Band names (["B1","B2","B3","B4"]); default:
 *                               all bands declared by the sidecar
 *
 * Result: output, productId, satellite, sensor, sensorMode, sensorKey,
 * bandCount, bands[], bandRoles[], radiometricState, declared{},
 * missingDeclaredFields[] (explicit absence — never defaulted).
 */
class RsGaofenImportOperator : public RSOperator {
public:
    std::string name() const override { return "rs:gaofen_import"; }
    std::string displayName() const override { return "Gaofen Product Import"; }
    std::string group() const override { return "data-formats"; }
    std::string description() const override {
        return "Import a Gaofen-1/2/6 or GF-7 L1A product (PMS/WFV, FWD/BWD) into a "
               "multi-band GeoTIFF with band roles, sun geometry and declared "
               "calibration metadata.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
