// src/operators/rs/rs_temporal_phenology_operator.h
// rs:temporal_phenology — seasonal phenology metrics (SOS/POS/EOS/LOS/
// amplitude/base/integral) per pixel from a vegetation-index time series.
//
// Time semantics (goal §22): the season window is expressed in day-of-year
// (UTC calendar date of each acquisition) and the season length uses REAL
// acquisition day offsets — never array indices. Numerics (goal §23):
// deterministic threshold scans (first/last crossing of base +
// crossingFraction·amplitude) — bit-exact for identical inputs.
//
// Metrics are computed ONCE per pixel over the whole series for the requested
// season window (multi-year data mixes years; per-year splits are a
// follow-up). Pixels with fewer than minValidPerSeason valid in-season
// samples stay NoData.
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalPhenologyOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_phenology"; }
  std::string displayName() const override { return "Temporal Phenology Metrics"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
  std::string determinismGrade() const override { return "bit-exact"; } // deterministic threshold scans (#659, ADR 0124)
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
