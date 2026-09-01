// src/operators/rs/rs_temporal_anomaly_operator.h
// rs:temporal_anomaly — deviation of one target date from a baseline built
// from the collection, per pixel.
//
// Methods: zscore = (y − baseline_mean) / baseline_stddev (sample stddev);
// difference = y − baseline_mean. The baseline is selected by an explicit
// date range (default: whole collection minus the target scene). Degenerate
// cases are part of the contract (goal §24): baseline stddev == 0 -> z-score
// is NoData (difference stays defined); baseline observations below
// min_observations -> NoData + warning, never a silently wrong number.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalAnomalyOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_anomaly"; }
  std::string displayName() const override { return "Temporal Anomaly"; }
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
