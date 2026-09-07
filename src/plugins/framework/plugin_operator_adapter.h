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

#include "plugin_execution_barrier.h"

#include "operators/framework/rs_operator.h"
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
    /// @param pluginId owning plugin (execution-barrier scope; may be empty
    ///        for non-plugin adapters, which then run unguarded)
    /// @param lazyFactory invoked on first execute(); empty when the plugin
    ///        has no binary (pure-manifest operators are not adapter-backed)
    /// @param ensurePluginLoaded invoked before the factory runs
    PluginOperatorAdapter( exprs::ManifestOperator manifestOperator, std::string pluginId,
                           OperatorFactory lazyFactory, std::function<bool()> ensurePluginLoaded );

    /// Overload for binary-registered operators without a manifest
    /// declaration: @p precomputedDescriptor was derived from the operator
    /// instance at registration time.
    PluginOperatorAdapter( sicnu::processing::AlgorithmDescriptor precomputedDescriptor,
                           std::string pluginId, OperatorFactory lazyFactory,
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
    std::string mPluginId;
    OperatorFactory mFactory;
    std::function<bool()> mEnsureLoaded;
    mutable sicnu::processing::AlgorithmDescriptor mDescriptorCache;
};

/// Shared conversion helper (also used by the agent tool provider bridge).
sicnu::processing::PortDescriptor manifestPortToPortDescriptor( const exprs::ManifestPort &port );

/// Forwards an operator instance while holding the owner's execution lease
/// for the INSTANCE LIFETIME. Used by PluginRuntimeHost to wrap factories
/// registered into the direct RSOperatorRegistry path (JobEngine/workflow),
/// whose create-then-run pattern is invisible to adapter-level leases: the
/// lease is acquired by the WRAPPER FACTORY before the plugin factory runs
/// and released only when the operator instance is destroyed (after run()).
/// A refused acquire returns nullptr from the factory, which every registry
/// consumer already treats as a clean "operator unavailable" failure.
class LeaseHoldingOperator : public sicnu::operators::RSOperator
{
public:
    LeaseHoldingOperator( std::unique_ptr<sicnu::operators::RSOperator> inner,
                          PluginExecutionBarrier::LeasePtr lease )
        : mInner( std::move( inner ) )
        , mLease( std::move( lease ) )
    {
    }

    std::string name() const override { return mInner->name(); }
    std::string displayName() const override { return mInner->displayName(); }
    std::string group() const override { return mInner->group(); }
    std::string description() const override { return mInner->description(); }
    std::string determinismGrade() const override { return mInner->determinismGrade(); }
    sicnu::operators::RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return mInner->memoryPolicy();
    }
    Json::Value schema() const override { return mInner->schema(); }
    Json::Value metadata() const override { return mInner->metadata(); }
    Json::Value executionEstimate() const override { return mInner->executionEstimate(); }
    Json::Value estimateExecution( const Json::Value &params ) const override
    {
        return mInner->estimateExecution( params );
    }
    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context ) override
    {
        return mInner->run( params, context );
    }

private:
    std::unique_ptr<sicnu::operators::RSOperator> mInner;
    PluginExecutionBarrier::LeasePtr mLease;
};

} // namespace sicnu::plugins
