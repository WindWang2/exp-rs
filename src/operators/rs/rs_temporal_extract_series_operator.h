// src/operators/rs/rs_temporal_extract_series_operator.h
// rs:temporal_extract_series — time-series extraction at a point or inside a
// polygon ROI (goal §25/§26).
//
// Point: map coordinate -> pixel per scene, one (time, value, valid) row per
// date. ROI: the raster window is bounded by the polygon's bounding box and
// the polygon mask is evaluated ONLY inside that window (never a full-scene
// scan); stats per date: mean/median/min/max/stddev/valid_count. Output: CSV
// file + JSON series in the result. No interpolation — missing stays missing.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalExtractSeriesOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_extract_series"; }
  std::string displayName() const override { return "Extract Temporal Series"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  RSOperatorMemoryPolicy memoryPolicy() const override
  {
    return RSOperatorMemoryPolicy::Streaming;
  }
  Json::Value schema() const override;
  Json::Value metadata() const override;
  Json::Value executionEstimate() const override;
  Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
