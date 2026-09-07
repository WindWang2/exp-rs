// src/operators/rs/rs_temporal_sen_trend_operator.h
// rs:temporal_sen_trend — per-pixel non-parametric monotonic trend (Sen's
// median slope + Mann-Kendall test) over a multi-date collection.
//
// Complement to rs:temporal_trend (OLS): robust to outliers and free of the
// residual-normality assumption; the kernel contract (definitions, tie
// handling, NaN semantics) lives in temporal_fit.h (mannKendallSenSlope).
//
// Time semantics: the test and slope use the REAL acquisition interval
// (t = days since the collection reference epoch); slope is per day.
//
// Outputs: slope, intercept, z, p_value, n (NaN = undefined; n = valid
// observations the test used).
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalSenTrendOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_sen_trend"; }
  std::string displayName() const override { return "Temporal Sen Trend"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  std::string determinismGrade() const override { return "bit-exact"; } // fixed-order median ranks (#659, ADR 0124)
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
