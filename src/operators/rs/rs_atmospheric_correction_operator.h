/***************************************************************************
 * rs_atmospheric_correction_operator.h  —  Atmospheric correction RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Applies atmospheric correction to optical raster data.
 *
 * Methods:
 *   - dn_to_radiance: L = gain * DN + bias
 *   - dos1: surface_radiance = radiance - min(radiance)
 *   - dos2: surface = (radiance - path_radiance) / transmittance
 *
 * Parameters:
 *   input        (string, required) Input raster path
 *   output       (string, required) Output raster path
 *   band         (int, optional)    1-based band number (default: 1)
 *   method       (string, optional) One of: dn_to_radiance, dos1, dos2 (default: dos1)
 *   gain         (number, optional) Radiance gain (default: 1.0)
 *   bias         (number, optional) Radiance bias (default: 0.0)
 *   airmass      (number, optional) Relative airmass for DOS2 transmittance (default: 1.0)
 *
 * Returns JSON object with:
 *   output  (string) Output raster path
 *   method  (string) Applied method
 *   band    (int)    Processed band
 */
class RsAtmosphericCorrectionOperator : public RSOperator {
public:
    std::string name() const override { return "rs:atmospheric_correction"; }
    std::string displayName() const override { return "Atmospheric Correction"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Apply atmospheric correction (DOS1/DOS2/radiance) to optical imagery.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
