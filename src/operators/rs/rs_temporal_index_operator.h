// src/operators/rs/rs_temporal_index_operator.h
// rs:temporal_index_series — spectral index computed per date over a temporal
// collection, materialized as one stacked GeoTIFF (band per date, per-band
// SICNU_ACQUISITION_DATE metadata).
//
// Formula reuse (goal §19/§20): every index is computed by the SAME kernels
// the single-scene rs:spectral_index operator uses — SpectralIndices::ndvi/
// evi/savi/ndwi/ndbi/mndwi and MathUtils::normalizedDifference — with the same
// role resolution and the same NaN conventions, so a single-scene run and the
// matching date of the series are bit-identical (asserted by test).
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs
{

class RsTemporalIndexSeriesOperator : public RSOperator
{
public:
  std::string name() const override { return "rs:temporal_index_series"; }
  std::string displayName() const override { return "Temporal Index Series"; }
  std::string group() const override { return "temporal"; }
  std::string description() const override;
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
