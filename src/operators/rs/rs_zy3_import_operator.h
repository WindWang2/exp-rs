/***************************************************************************
 * rs_zy3_import_operator.h — Import ZY-3 (TLC/NAD/FWD/BWD) L1A → multi-band GeoTIFF
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Imports a Ziyuan-3 L1A product (NAD multispectral, or the TLC/NAD/FWD/BWD
 * panchromatic; CRESDA sidecar XML + TIFF) into a stacked, role-tagged,
 * calibration-annotated multi-band GeoTIFF.
 *
 * Parameters:
 *   input   (string, required)  ZY3 product dir, sidecar XML or image TIFF
 *   output  (string, required)  Output multi-band GeoTIFF
 *   bands   (array, optional)   Band names; default: all bands declared by the sidecar
 *
 * Result: same contract as rs:gaofen_import (ADR 0157).
 */
class RsZy3ImportOperator : public RSOperator {
public:
    std::string name() const override { return "rs:zy3_import"; }
    std::string displayName() const override { return "Ziyuan-3 Product Import"; }
    std::string group() const override { return "data-formats"; }
    std::string description() const override {
        return "Import a Ziyuan-3 L1A product (TLC/NAD/FWD/BWD) into a multi-band GeoTIFF "
               "with band roles, sun geometry and declared calibration metadata.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
