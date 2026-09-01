// src/operators/rs/rs_temporal_summary_operator.h
// rs:temporal_summary — per-pixel statistics over a multi-date collection
// (count / valid_count / mean / min / max / stddev, optional exact median).
//
// Streaming design (goal §12–§15): spatial tiles × one date at a time; the
// per-pixel state is five floats (Welford n/mean/M2 + min/max), so the peak
// working set is independent of the date count. Median is EXACT and uses an
// auto-shrunk tile so T × tilePixels × 4 B stays within the memory budget
// (never a T×H×W cube, never an undisclosed approximation).
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalSummaryOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_summary"; }
  std::string displayName() const override { return "Temporal Summary"; }
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
