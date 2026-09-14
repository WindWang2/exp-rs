/***************************************************************************
 * rs_hj_import_operator.h — Import HJ-1A/1B CCD L1A → multi-band GeoTIFF
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Imports a Huanjing HJ-1A/1B CCD L1A product (CRESDA sidecar XML + TIFF)
 * into a stacked, role-tagged, calibration-annotated multi-band GeoTIFF.
 *
 * Parameters:
 *   input   (string, required)  HJ product dir, sidecar XML or image TIFF
 *   output  (string, required)  Output multi-band GeoTIFF
 *   bands   (array, optional)   Band names; default: all bands declared by the sidecar
 *
 * Result: same contract as rs:gaofen_import (ADR 0146).
 */
class RsHjImportOperator : public RSOperator {
public:
    std::string name() const override { return "rs:hj_import"; }
    std::string displayName() const override { return "Huanjing CCD Product Import"; }
    std::string group() const override { return "data-formats"; }
    std::string description() const override {
        return "Import a Huanjing (HJ-1A/1B or HJ-2A/B) CCD L1A product into a "
               "multi-band GeoTIFF with band roles, sun geometry and declared "
               "calibration metadata.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
