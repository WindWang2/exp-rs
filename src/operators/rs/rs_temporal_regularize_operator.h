// src/operators/rs/rs_temporal_regularize_operator.h
// rs:temporal_regularize — re-casts an irregular acquisition series onto a
// configurable regular calendar (Temporal Platform 10.0, closes the T-2 gap:
// rs:temporal_gap_fill fills between real acquisitions but cannot re-cast
// the calendar itself).
//
// Methods (all on the real day-offset axis, all refusing extrapolation past
// the observed span): nearest / window_mean / linear work per calendar point
// from the raw observations; whittaker fits a penalized smoother DEFINED ON
// the calendar grid (penalty bridges small data-free runs only, max_gap_nodes).
//
// Outputs: one "reg_<date>" band per calendar point (chronological) plus a
// "valid_count" band (observations contributing per pixel/point) and a
// "filled_count" band (synthetic values per pixel) — the provenance pair
// that keeps downstream consumers honest about what is observed.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalRegularizeOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_regularize"; }
  std::string displayName() const override { return "Temporal Regularize (Regular Calendar)"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  // Nearest/window_mean/linear are bit-exact; whittaker is the banded
  // tolerance-grade solver (ADR 0124 convention, documented 1e-4).
  std::string determinismGrade() const override { return "tolerance"; }
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
