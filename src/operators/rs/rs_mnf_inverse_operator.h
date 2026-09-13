/***************************************************************************
 * rs_mnf_inverse_operator.h — inverse MNF RSOperator (Hyperspectral 10.0)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:mnf_inverse — invert the MNF transform: reconstruct band space from an
 * MNF-components raster (+ transform model artifact) or convert a single
 * MNF-space spectrum back to band space.
 *
 * Raster mode (default):
 *   input      MNF-components raster produced by rs:mnf
 *   transform  path to the exp-rs:mnf-transform artifact (transformOut of
 *              rs:mnf); digest-verified
 *   output     reconstructed band-space raster (model.bandCount bands, with
 *              the model's wavelength axis when the model carries one)
 *   components optional 0-based component subset (default: all bands of the
 *              input raster, missing coefficients treated as zero)
 *   errorOut   optional RMSE raster: per pixel, the norm over bands of the
 *              contribution of the NOT-selected components (the reconstruction
 *              mass the selection discards)
 *
 * Spectrum mode (spectrumRef present):
 *   spectrumRef  path to a spectral-table artifact holding exactly one
 *                MNF-space spectrum
 *   spectrumOut  path for the converted band-space spectrum table
 *
 * Safe-conversion rule: with all B components the inverse is the exact
 * adjoint of the forward transform; any subset is a documented, quantified
 * approximation (errorOut / the spectrum result's reconstructionError).
 */
class RsMnfInverseOperator : public RSOperator {
public:
    std::string name() const override { return "rs:mnf_inverse"; }
    std::string displayName() const override { return "Inverse MNF"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Reconstruct band space from MNF components using the transform "
               "model artifact from rs:mnf (full inverse or a quantified "
               "component subset).";
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
