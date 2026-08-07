/***************************************************************************
 * rs_endmember_extraction_operator.h  —  Endmember extraction RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Extracts spectral endmembers by Pixel Purity Index (PPI): the pixels that
 * are most often the extreme of a random projection are the purest spectra
 * of the scene. Endmembers are returned in the result JSON (array of spectra)
 * so they can feed rs:spectral_unmixing / rs:sam_classify directly.
 *
 * Parameters:
 *   input       (string, required) Input multi-band raster
 *   nEndmembers (int, required)    Number of endmembers to extract
 *   projections (int, optional)    Random projections (default 1000, min 16)
 *
 * Returns JSON object with:
 *   endmembers (array)  Extracted endmember spectra (arrays of band-count floats)
 *   indices    (array)  Source pixel index per endmember
 *   ppiCounts  (array)  Per-pixel PPI extreme counts
 */
class RsEndmemberExtractionOperator : public RSOperator {
public:
    std::string name() const override { return "rs:endmember_extraction"; }
    std::string displayName() const override { return "Endmember Extraction (PPI)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Extract spectral endmembers by Pixel Purity Index.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
