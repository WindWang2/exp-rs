/***************************************************************************
 * rs_qa_mask_operator.h  —  Quality / cloud / shadow / snow mask RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Derives a binary mask (1 = masked/obscured, 0 = clear) from a
 * quality-assurance band of a remote-sensing product.
 *
 * Sources:
 *   - Landsat Collection 2 QA_PIXEL bit flags
 *   - Sentinel-2 Scene Classification Layer (SCL) class ids
 *   - a generic bitmask (explicit `bits`)
 *
 * The QA band is resolved from the input's semantic band roles
 * (SICNU_BAND_ROLE: scene_classification preferred, then qa) unless `qa_band`
 * is given explicitly. `source` auto-detects from the resolved band; explicit
 * selection overrides it.
 *
 * Parameters:
 *   input    (string, required) Raster containing the QA band (stacked product
 *                               or standalone QA file)
 *   output   (string, required) Output mask raster path (UInt8, 1 = masked)
 *   qa_band  (int, optional)    1-based QA band; omitted -> resolved from roles
 *   source   (enum, optional)   auto / landsat_qa_pixel / sentinel2_scl /
 *                               generic_bitmask (default: auto)
 *   mask     (enum, optional)   cloud_and_shadow (default) / cloud /
 *                               cloud_shadow / snow / water / all
 *   bits     (int, optional)    bit flags for generic_bitmask (required for it)
 *
 * Returns JSON object with:
 *   output        (string) Output mask raster path
 *   source        (string) Resolved QA source
 *   maskClasses   (string) Applied mask selection
 *   maskedPixels  (int)    Number of masked pixels
 *   totalPixels   (int)    Number of evaluated pixels
 *   maskedPercent (double) Percentage of masked pixels
 */
class RsQaMaskOperator : public RSOperator {
public:
    std::string name() const override { return "rs:qa_mask"; }
    std::string displayName() const override { return "QA Mask"; }
    std::string group() const override { return "qa"; }
    std::string description() const override {
        return "Derive a cloud / cloud-shadow / snow mask from Landsat QA_PIXEL "
               "or Sentinel-2 SCL quality bands.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
