// src/operators/rs/rs_temporal_harmonic_breaks_operator.h
// rs:temporal_harmonic_breaks — joint seasonal-trend change segmentation
// (Temporal Platform 10.0, closes the T-3 gap).
//
// Method (honest naming): greedy seasonality-adjusted trend-break
// segmentation with per-segment harmonic + linear-trend refit —
// BFAST/CCDC-inspired, NOT the full BFAST or CCDC algorithms (see
// temporal_change.h). Each pixel's series is modeled per segment as
// [intercept, per-day trend, sin/cos(k·2πt/365.25)...]; breaks are trend
// breaks of the seasonality-adjusted residual; every accepted split lowers
// the residual RSS by more than minImprovement.
//
// Outputs: break_count, break_day_k / break_mag_k bands (day offsets from
// the first acquisition; NoData beyond a pixel's break count), first/last
// segment per-day slopes, overall rmse/r2, and — when a disturbance
// direction is declared — the first onset_day with its recovery_days
// (NaN when no qualifying onset; -1 recovery = never recovered in-range).
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalHarmonicBreaksOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_harmonic_breaks"; }
  std::string displayName() const override { return "Temporal Harmonic Breaks (Joint Seasonal-Trend Change)"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  // Dense normal-equation solves in fixed order; bit-exact regression
  // anchors (ADR 0124).
  std::string determinismGrade() const override { return "bit-exact"; }
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
