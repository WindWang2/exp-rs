// src/operators/rs/rs_temporal_trend_operator.h
// rs:temporal_trend — per-pixel ordinary-least-squares linear trend over a
// multi-date collection.
//
// Time semantics (goal §22): the regressor is the REAL acquisition interval
// (t = days since the collection reference epoch), never array indices.
// Numerics (goal §23): West's online covariance update (centered), so the
// per-pixel state is six floats and memory is O(tile) — no date buffer.
//
// Outputs: slope (per day), intercept (at the reference epoch), R², valid
// observation count, RMSE. Pixels with < 2 valid observations are NoData.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalTrendOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_trend"; }
  std::string displayName() const override { return "Temporal Linear Trend"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
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
