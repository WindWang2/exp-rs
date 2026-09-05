// src/operators/rs/rs_temporal_harmonic_fit_operator.h
// rs:temporal_harmonic_fit — per-pixel harmonic regression (annual +
// sub-annual Fourier terms) over a multi-date collection.
//
// Time semantics (goal §22): the regressors are sin/cos of 2π·k·t / 365.25
// with t = REAL acquisition days since the collection reference epoch — never
// array indices. Numerics (goal §23): closed-form weighted least squares
// (normal equations, partial pivoting, fixed sample order) with optional
// IRLS outlier damping — bit-exact for identical inputs.
//
// Outputs: one fitted band per acquisition date plus RMSE and R²; optionally
// the raw regression coefficients (intercept, sin/cos pairs). Pixels with
// fewer than minObservations valid samples (or an underdetermined system)
// stay NoData.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalHarmonicFitOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_harmonic_fit"; }
  std::string displayName() const override { return "Temporal Harmonic Fit"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  std::string determinismGrade() const override { return "bit-exact"; } // closed-form OLS; deterministic order (#659, ADR 0124)
  RSOperatorMemoryPolicy memoryPolicy() const override
  {
    return RSOperatorMemoryPolicy::MultiPassStreaming;
  }
  Json::Value schema() const override;
  Json::Value metadata() const override;
  Json::Value executionEstimate() const override;
  Json::Value estimateExecution( const Json::Value &params ) const override;
  Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
