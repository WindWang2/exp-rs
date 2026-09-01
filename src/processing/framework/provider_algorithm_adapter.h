// src/processing/framework/provider_algorithm_adapter.h
//
// ADR 0012 — Mirrors QgsProcessingAlgorithm instances from the QGIS Processing
// Registry into the Atomic Algorithm Registry, so the Agent's exported tool
// schema covers GDAL/OTB/QGIS-native algorithms alongside RS operators.
#pragma once

#include "atomic_algorithm_adapter.h"

#include <QString>

class QgsProcessingAlgorithm;

namespace sicnu::processing {

/**
 * Adapter that wraps a QgsProcessingAlgorithm into the AtomicAlgorithmAdapter
 * interface.  Constructed from a const reference to a registry-owned algorithm
 * (a clone is created for execution).
 *
 * Lifetime note (#695): the adapter does NOT cache the provider-owned
 * algorithm pointer — QgsProcessingRegistry may delete the algorithm when its
 * provider is unloaded. execute() re-resolves the live algorithm from the
 * processing registry by id and fails with a typed error when it is gone.
 */
class ProviderAlgorithmAdapter : public AtomicAlgorithmAdapter
{
public:
  /// Constructs from a registry-owned algorithm. A descriptor is built eagerly;
  /// execution re-resolves the algorithm by id and clones it via create().
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
  /// Owning provider id (empty for provider-less algorithms) and the full
  /// algorithm id used to look the live algorithm up in QgsProcessingRegistry.
  QString mProviderId;
  QString mAlgorithmId;
  AlgorithmDescriptor mDesc;
};

} // namespace sicnu::processing
