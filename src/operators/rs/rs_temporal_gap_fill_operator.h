// src/operators/rs/rs_temporal_gap_fill_operator.h
// rs:temporal_gap_fill — time-aware interpolation of missing (masked/NaN)
// samples in a per-pixel time series over a multi-date collection.
//
// Interpolation uses the REAL acquisition interval (days since the collection
// reference epoch), never array indices: linear mode weights the bracketing
// valid neighbours by day distance, nearest mode copies the closest valid
// neighbour. Gaps wider than max_gap_days stay NaN — no extrapolation.
//
// Outputs: one filled band per scene date ("filled_<date>", same order) plus
// a final "filled_count" band (float count of synthesised samples per pixel).
// Stateless per pixel: streaming, bit-exact.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalGapFillOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_gap_fill"; }
  std::string displayName() const override { return "Temporal Gap Fill"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  // Pure per-pixel interpolation with deterministic tie-breaking (ADR 0124).
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
