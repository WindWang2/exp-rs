// src/operators/rs/rs_temporal_sar_fusion_operator.h
// rs:temporal_sar_fusion — feature-level fusion of already-coregistered
// optical and SAR temporal feature stacks (Temporal Phenology 12.0, WP6).
//
// Concatenates the bands of two feature rasters that share ONE verified
// pixel grid (dimensions + geotransform + CRS), emitting opt_*/sar_* band
// names and fusion provenance metadata. A grid mismatch is a typed failure;
// coregistration/resampling is never performed here.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalSarFusionOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_sar_fusion"; }
  std::string displayName() const override { return "Temporal Optical+SAR Feature Fusion"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  std::string determinismGrade() const override { return "bit-exact"; } // band copy, no resampling
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
