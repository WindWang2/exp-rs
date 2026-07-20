/***************************************************************************
 * rs_spectral_index_operator.h  —  Spectral index RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Computes common spectral indices from multi-band raster data.
 *
 * Supported indices:
 *   - NDVI  = (NIR - Red) / (NIR + Red)
 *   - EVI   = 2.5 * (NIR - Red) / (NIR + 6*Red - 7.5*Blue + 1)
 *   - SAVI  = (NIR - Red) / (NIR + Red + L) * (1 + L), L = 0.5
 *   - NDWI  = (Green - NIR) / (Green + NIR)
 *   - NDBI  = (SWIR - NIR) / (SWIR + NIR)
 *   - MNDWI = (Green - SWIR) / (Green + SWIR)
 *
 * Parameters:
 *   input    (string, required) Input raster path
 *   output   (string, required) Output raster path
 *   index    (string, required) One of: NDVI, EVI, SAVI, NDWI, NDBI, MNDWI
 *   nir      (int, optional)    1-based NIR band number (default: 4)
 *   red      (int, optional)    1-based Red band number (default: 3)
 *   green    (int, optional)    1-based Green band number (default: 2)
 *   blue     (int, optional)    1-based Blue band number (default: 1)
 *   swir     (int, optional)    1-based SWIR band number (default: 5)
 *
 * Returns JSON object with:
 *   output  (string) Output raster path
 *   index   (string) Computed index name
 *   width   (int)    Output width
 *   height  (int)    Output height
 */
class RsSpectralIndexOperator : public RSOperator {
public:
    std::string name() const override { return "rs:spectral_index"; }
    std::string displayName() const override { return "Spectral Index"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Compute a spectral index (NDVI, EVI, SAVI, NDWI, NDBI, MNDWI) from raster bands.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
