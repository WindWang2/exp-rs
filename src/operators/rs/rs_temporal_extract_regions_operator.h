// src/operators/rs/rs_temporal_extract_regions_operator.h
// rs:temporal_extract_regions — batch multi-ROI temporal extraction
// (Temporal Platform 10.0, closes the C-2 gap: rs:temporal_extract_series
// handles exactly one point or one polygon per call).
//
// Regions are declared inline (regions: [{"id", "point"|"polygon"}, ...]) or
// as a JSON file (regions_file). The date loop is outermost: every scene is
// read exactly once and only region windows are touched; per region × date
// the reducer produces mean / min / max / stddev (population) / exact median
// (bounded scratch) / valid_count, streamed as CSV rows
// (region_id,date,t_days,mean,min,max,stddev,median,valid_count).
//
// Rows are emitted only where validCount > 0 (a 100k-region × 100-date table
// would otherwise be mostly NaN padding); the row/empty-cell accounting in
// the JSON result keeps the omission honest. Cancellation checkpoints run
// per date and per region window.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalExtractRegionsOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_extract_regions"; }
  std::string displayName() const override { return "Temporal Extract Region Series (Batch Multi-ROI)"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  // Fixed-order accumulation over deterministic region/date order.
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
