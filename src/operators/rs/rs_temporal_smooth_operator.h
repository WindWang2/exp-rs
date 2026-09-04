// src/operators/rs/rs_temporal_smooth_operator.h
// rs:temporal_smooth — quality-aware smoothing of a per-pixel time series
// (Savitzky–Golay / Whittaker / moving average) over a multi-date collection.
//
// The series per pixel is the REAL acquisition sequence (column-major tile of
// all scenes, ordered chronologically by the collection); smoothing never
// reorders dates. NaN samples are treated as absent by every kernel, never as
// zero.
//
// Outputs: one band per scene date, same order ("smoothed_<date>"), plus
// per-band acquisition metadata. Determinism grade is "tolerance" because the
// Whittaker variant solves a banded system to a documented 1e-5 relative
// tolerance; Savitzky–Golay and moving average are bit-exact.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalSmoothOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_smooth"; }
  std::string displayName() const override { return "Temporal Smoothing"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  // Whittaker's banded solver is tolerance-grade (ADR 0124); the schema grade
  // covers the whole operator so one declaration stays method-insensitive.
  std::string determinismGrade() const override { return "tolerance"; }
    RSOperatorDeterminism determinism() const override { return RSOperatorDeterminism::Tolerance; }
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
