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
  /// execution clones the algorithm via create().
  explicit ProviderAlgorithmAdapter( const QgsProcessingAlgorithm &alg );
  ~ProviderAlgorithmAdapter() override = default;

  std::string algorithmId() const override;
  AlgorithmDescriptor descriptor() const override;
  Json::Value execute( const Json::Value &params, ProgressCallback progressCb = nullptr ) override;

private:
  /// The registry-owned algorithm pointer (non-owning, used for create()).
  const QgsProcessingAlgorithm *mAlg = nullptr;
  AlgorithmDescriptor mDesc;
};

} // namespace sicnu::processing
