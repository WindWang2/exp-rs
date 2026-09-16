/***************************************************************************
 * rs_sparse_unmixing_operator.h  —  sparse (L1, non-negative) unmixing
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Sparse linear spectral unmixing: per-pixel abundances under an L1
 * sparsity constraint, non-negativity, and the same (penalty-style)
 * sum-to-one convention as the FCLS solver. Supports OVERCOMPLETE
 * dictionaries (atoms > bands), unlike rs:spectral_unmixing.
 *
 * Parameters:
 *   input          (string, required) Input multi-band raster
 *   output         (string, required) Abundance raster (one band per atom)
 *   endmembers / endmembersRef / libraryPath (+libraryMaterials)
 *                  (reference seam, exactly one form)
 *   errorOut       (string, optional) Reconstruction RMSE raster
 *   lambda         (double, default 0.01) L1 weight
 *   sumToOnePenalty (double, default 0.0) rho penalty (FCLS convention)
 *   maxIterations  (int, default 2000)
 *   tolerance      (double, default 1e-8) relative iterate change
 *   collinearAngleDegrees (double, default 0.5; 0 disables the guard)
 *
 * Returns JSON with output/atoms/meanError/lambda/convergedFraction/
 * meanIterations and the reference provenance echo.
 */
class RsSparseUnmixingOperator : public RSOperator {
public:
    std::string name() const override { return "rs:sparse_unmixing"; }
    std::string displayName() const override { return "Sparse Unmixing (L1 + Non-negative)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Sparse abundance estimation (L1 + non-negativity, optional "
               "sum-to-one penalty) with FISTA; supports overcomplete dictionaries.";
    }

    RSOperatorMemoryPolicy memoryPolicy() const override {
        // Band-window streaming like the OLS/FCLS unmixing operator.
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
