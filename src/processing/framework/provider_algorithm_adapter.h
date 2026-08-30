// src/processing/framework/provider_algorithm_adapter.h
//
// ADR 0012 — Mirrors QgsProcessingAlgorithm instances from the QGIS Processing
// Registry into the Atomic Algorithm Registry, so the Agent's exported tool
// schema covers GDAL/OTB/QGIS-native algorithms alongside RS operators.
#pragma once

#include "atomic_algorithm_adapter.h"

class QgsProcessingAlgorithm;

namespace sicnu::processing {

/**
 * Adapter that wraps a QgsProcessingAlgorithm into the AtomicAlgorithmAdapter
 * interface.  Constructed from a const reference to a registry-owned algorithm
 * (a clone is created for execution).
 */
class ProviderAlgorithmAdapter : public AtomicAlgorithmAdapter
{
public:
  /// Constructs from a registry-owned algorithm. A descriptor is built eagerly;
  /// execution re-resolves the algorithm from QgsProcessingRegistry by id and
  /// clones it via create() — the provider owns the algorithm, so caching the
  /// raw pointer here was a use-after-free window across provider
  /// removal/refresh (#695).
  explicit ProviderAlgorithmAdapter( const QgsProcessingAlgorithm &alg );
  ~ProviderAlgorithmAdapter() override = default;

  std::string algorithmId() const override;
  AlgorithmDescriptor descriptor() const override;
  Json::Value execute( const Json::Value &params, ProgressCallback progressCb = nullptr,
                       std::function<bool()> isCancelledFn = nullptr ) override;
  /// Provider algorithms have no parameter-derived estimate; falls back to the
  /// descriptor-level execution metadata (static, typical input).
  Json::Value estimateExecution( const Json::Value &params ) const override;

private:
  AlgorithmDescriptor mDesc;
};

} // namespace sicnu::processing
