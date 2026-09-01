// src/operators/rs/rs_temporal_composite_operator.h
// rs:temporal_composite — best-pixel / mean / median composites over a
// multi-date collection, optionally grouped into periods (month / quarter /
// season / year / custom N days).
//
// Best-pixel selection is an explicit, deterministic contract (goal §16/§50):
// among valid observations (NoData/NaN/QA-mask excluded), maximize the quality
// score — a per-scene quality band when provided, else 1.0 — with ties broken
// by smallest |t − t_target| (target date, default = period midpoint), then by
// lowest scene index. NEVER "first non-NoData wins".
//
// Every composite output carries an observation-count band and the selected
// observation's quality score (goal §17): a 1-observation pixel never looks
// identical to a 30-observation pixel.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalCompositeOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_composite"; }
  std::string displayName() const override { return "Best Pixel Composite"; }
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
