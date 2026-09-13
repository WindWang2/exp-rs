// src/operators/rs/rs_temporal_region_features_operator.h
// rs:temporal_region_features — machine-readable temporal feature table
// (Temporal Platform 10.0, goal G: temporal outputs that downstream ML /
// agent workflows can consume instead of dying in terminal JSON).
//
// One row per region; typed, versioned feature schema
// (exp_rs_temporal_region_features/1) covering: series quality
// (valid_fraction), distribution (mean/stddev/min/max), robust trend
// (Sen slope + MK p, or OLS), anomaly (recent z, max |z|), change
// (break count / first break day / max magnitude from the joint
// harmonic+trend model, optional disturbance onset + recovery), and
// multi-cycle phenology (per-cycle median-across-years SOS/POS/EOS/LOS/
// amplitude/integral via phenologyCyclesPerYear).
//
// Output: feature CSV (missing = "nan") + a JSON schema sidecar so a join by
// region_id against label tables is mechanical.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalRegionFeaturesOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_region_features"; }
  std::string displayName() const override { return "Temporal Region Features (ML Table)"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  // Fixed-order kernels over deterministic region/date order; the whittaker
  // banded solve is not used here — all fits are bit-exact dense solves.
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
