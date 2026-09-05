/***************************************************************************
 * src/plugins/framework/plugin_operator_adapter.h
 *
 * Bridges a manifest-declared plugin operator into the canonical agent
 * catalog (AtomicAlgorithmRegistry) without loading the plugin binary:
 * the descriptor comes from the manifest; the first execute() triggers the
 * actual plugin load (lazy binary loading, Phase 33 startup contract).
 * After the operator is instantiated its live schema() is authoritative.
 ***************************************************************************/
#pragma once

#include "processing/framework/atomic_algorithm_adapter.h"
#include "exprs/plugin_manifest.h"

#include <atomic>
#include <functional>

namespace sicnu::plugins {

/// Descriptor-only view of one manifest operator contribution.
/// manifests stay the discovery-time source of truth.
class PluginOperatorAdapter : public sicnu::processing::AtomicAlgorithmAdapter
{
public:
    using OperatorFactory = std::function<std::unique_ptr<sicnu::operators::RSOperator>()>;

    /// @param manifestOperator the manifest contribution (descriptor source)
    /// @param lazyFactory invoked on first execute(); empty when the plugin
    ///        has no binary (pure-manifest operators are not adapter-backed)
    /// @param ensurePluginLoaded invoked before the factory runs
    PluginOperatorAdapter( exprs::ManifestOperator manifestOperator, OperatorFactory lazyFactory,
                           std::function<bool()> ensurePluginLoaded );

    /// Overload for binary-registered operators without a manifest
    /// declaration: @p precomputedDescriptor was derived from the operator
    /// instance at registration time.
    PluginOperatorAdapter( sicnu::processing::AlgorithmDescriptor precomputedDescriptor,
                           OperatorFactory lazyFactory,
                           std::function<bool()> ensurePluginLoaded );
    ~PluginOperatorAdapter() override = default;

    std::string algorithmId() const override { return mManifest.id; }
    sicnu::processing::AlgorithmDescriptor descriptor() const override;

    Json::Value execute( const Json::Value &params, sicnu::processing::ProgressCallback progressCb,
                         std::function<bool()> isCancelledFn ) override;

    Json::Value estimateExecution( const Json::Value &params ) const override;

private:
    sicnu::processing::AlgorithmDescriptor buildDescriptor() const;
    /// Converts a manifest port into a processing port descriptor.
    static sicnu::processing::PortDescriptor toPortDescriptor( const exprs::ManifestPort &port );

    exprs::ManifestOperator mManifest;
    OperatorFactory mFactory;
    std::function<bool()> mEnsureLoaded;
    mutable sicnu::processing::AlgorithmDescriptor mDescriptorCache;
};

/// Shared conversion helper (also used by the agent tool provider bridge).
sicnu::processing::PortDescriptor manifestPortToPortDescriptor( const exprs::ManifestPort &port );

} // namespace sicnu::plugins
