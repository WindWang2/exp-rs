// src/operators/rs/rs_temporal_breakpoints_operator.h
// rs:temporal_breakpoints — per-pixel piecewise-linear trend segmentation with
// break detection (BSFAST-lite) over a multi-date collection.
//
// Time semantics (goal §22): segments are fitted on the REAL acquisition
// interval (t = days since the collection reference epoch) and break dates
// are reported in the same day-offset units — never array indices. Numerics
// (goal §23): greedy binary segmentation (repeated OLS, split at the largest
// RSS reduction) in fixed sample order — bit-exact for identical inputs
// (greedy, not the global optimum).
//
// Outputs: break count, one break-date band per allowed break (day offsets,
// NoData beyond a pixel's actual break count), one per-segment slope band per
// segment (per day) and the overall RMSE.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalBreakpointsOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_breakpoints"; }
  std::string displayName() const override { return "Temporal Breakpoints"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  std::string determinismGrade() const override { return "bit-exact"; } // deterministic greedy RSS segmentation (#659, ADR 0124)
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
