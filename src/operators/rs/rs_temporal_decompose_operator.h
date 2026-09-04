// src/operators/rs/rs_temporal_decompose_operator.h
// rs:temporal_decompose — additive seasonal-trend decomposition of a
// per-pixel time series over a multi-date collection.
//
// Trend = Whittaker smoother with trend_lambda; seasonal = mean of detrended
// values grouped by day-of-year (circularly smoothed by seasonal_window_days);
// remainder = y − trend − seasonal. The seasonal climatology is keyed by the
// REAL day-of-year of each acquisition (UTC instant), never by array index.
//
// Outputs: band groups in the order of the "components" parameter
// ([trend 1..T][seasonal T+1..2T][remainder 2T+1..3T] when all are selected),
// each band named "<component>_<date>". Determinism grade is "tolerance"
// (Whittaker banded solve, documented 1e-5).
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalDecomposeOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_decompose"; }
  std::string displayName() const override { return "Temporal Decomposition"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  // The Whittaker trend solve is tolerance-grade (ADR 0124).
  std::string determinismGrade() const override { return "tolerance"; }
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
