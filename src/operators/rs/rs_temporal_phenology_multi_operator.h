// src/operators/rs/rs_temporal_phenology_multi_operator.h
// rs:temporal_phenology_multi — Phenology 2.0: automatic multi-cycle
// candidates, cross-year (harvest-year) windows, per-metric quality flags
// (Temporal Intelligence 11.0, package D).
//
// Windows are PROPOSED from the pixel's own seasonal component (peak
// detection on the seasonalDecompose climatology, bounded by
// maxCyclesPerYear) — no caller-declared season windows. Wrapped windows
// (Dec→May style) count toward the harvest year (year of the window end).
// Every window is quality-gated (sample count, coverage, largest-gap
// fraction, amplitude share) and refused — never guessed — when the gate
// fails; refused windows carry no metrics. Per cycle index the product
// reports the median-of-seasons metrics plus a validity band, so a pixel
// with 1 complete season and 2 refused seasons shows exactly that.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalPhenologyMultiOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_phenology_multi"; }
  std::string displayName() const override
  {
    return "Temporal Phenology Multi (Auto Cycles + Quality Flags)";
  }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
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
