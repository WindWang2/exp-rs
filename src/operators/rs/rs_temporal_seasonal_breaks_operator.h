// src/operators/rs/rs_temporal_seasonal_breaks_operator.h
// rs:temporal_seasonal_breaks — seasonal-component break attribution with
// opt-in confidence intervals (Temporal Intelligence 11.0, packages A + C).
//
// Method (honest naming, continues the platform's "…-inspired" rule): runs
// the shared greedy seasonality-adjusted trend-break segmentation, then
// attributes each break with nested weighted-LS F comparisons on the
// neighbouring segments — whether the seasonal basis (harmonic sin/cos
// coefficients) changed beyond the trend change. Per break outputs: kind
// (none/trend/seasonal/both/untestable), day offset, fitted-level jump,
// seasonal-coefficient shift norm, harmonic-1 amplitude/phase change, F
// statistic and p-value. NOT full BFAST (no iterative season/trend
// alternation); NOT CCDC (no L1, no online per-element model update).
//
// Opt-in uncertainty (compute_ci): per-pixel, per-break percentile CIs on
// the fitted-level jump magnitude via seeded residual bootstrap (fixed
// design residuals, observed-time support only). Low refit success rates
// report NaN bounds — never fabricated intervals.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalSeasonalBreaksOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_seasonal_breaks"; }
  std::string displayName() const override
  {
    return "Temporal Seasonal Breaks (Attribution + CI)";
  }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  // Dense normal-equation solves in fixed order; bootstrap uses a seeded
  // mt19937 — deterministic on this platform.
  std::string determinismGrade() const override
  {
    return "bit-exact (CI bands: seeded-deterministic)";
  }
  RSOperatorMemoryPolicy memoryPolicy() const override
  {
    return RSOperatorMemoryPolicy::Streaming;
  }
  Json::Value schema() const override;
  Json::Value metadata() const override;
  Json::Value executionEstimate() const override;
  Json::Value estimateExecution( const Json::Value &params ) const override;
  Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
