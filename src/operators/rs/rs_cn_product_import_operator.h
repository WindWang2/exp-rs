/***************************************************************************
 * rs_cn_product_import_operator.h — unified Chinese-satellite product import
 * (rs:cn_product_import, ADR 0147).
 *
 * One operator over the standardized rs_product_import_plan service for any
 * supported CN family: GF-1/2/6 PMS/WFV, GF-7 FWD/BWD, ZY-3 TLC/NAD/FWD/BWD,
 * ZY-1 02C PMS/HRC, HJ-1A/1B CCD, HJ-2A/B CCD. The per-family operators
 * (rs:gaofen_import / rs:zy3_import / rs:hj_import) share the same service
 * and remain as course-stable entry points.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Imports any supported Chinese satellite L1A product into a stacked,
 * role-tagged, calibration-annotated multi-band GeoTIFF.
 *
 * Parameters:
 *   input             (string, required)  Product dir, sidecar XML or image TIFF
 *   output            (string, required)  Output multi-band GeoTIFF
 *   bands             (array, optional)   Band names; default: all declared by the sidecar
 *   apply_calibration (bool,  optional)   DN → radiance via declared gain/bias; typed
 *                                         refusal when any requested band lacks both
 *
 * Result: plan payload (identity, sensor profile, sidecar generation,
 * constituents, completeness) + output/productId/bands/bandRoles/declared{}/
 * missingDeclaredFields[]/calibration{} provenance.
 */
class RsCnProductImportOperator : public RSOperator {
public:
    std::string name() const override { return "rs:cn_product_import"; }
    std::string displayName() const override { return "Chinese Satellite Product Import"; }
    std::string group() const override { return "data-formats"; }
    std::string description() const override {
        return "Import any supported Chinese satellite L1A product (GF-1/2/6/7, ZY-3, ZY-1 02C, "
               "HJ-1/2 CCD) into a multi-band GeoTIFF with band roles, sensor profile, sidecar "
               "generation, sun geometry, optional declared calibration and full provenance.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
