// src/operators/rs/rs_temporal_model_select_operator.h
// rs:temporal_model_select — bounded per-segment model selection (Temporal
// Intelligence 11.0, package B; closes the ADR 0148 declared gap).
//
// Per pixel, a bounded candidate grid {harmonic order 0..maxHarmonics} ×
// {break budget 0..maxBreaks} is scored with AICc, BIC, or deterministic
// contiguous-block CV and the parsimony-tie-broken winner is reported with
// its parameter count and score. Degenerate pixels report a refusal band
// value, never a fabricated selection. This is explicit bounded selection —
// still NOT CCDC's online per-element model updating.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalModelSelectOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_model_select"; }
  std::string displayName() const override
  {
    return "Temporal Model Select (Per-Segment Order + Breaks)";
  }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
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
