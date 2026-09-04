/***************************************************************************
 * src/plugins/framework/plugin_runtime_host.cpp
 ***************************************************************************/
#include "plugin_runtime_host.h"

#include "data_provider_registry.h"
#include "external_tool_operator.h"
#include "plugin_agent_tool_provider.h"
#include "plugin_model_runtime_bridge.h"
#include "plugin_operator_adapter.h"

#include "agent/tool_catalog/agent_tool_catalog.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "operators/runtime/model_runtime.h"

#include <algorithm>

#include "processing/framework/atomic_algorithm_adapter.h"

namespace sicnu::plugins {

PluginRuntimeHost &PluginRuntimeHost::instance()
{
    static PluginRuntimeHost host;
    return host;
}

void bootstrapPluginRuntime( const exprs::PluginRegistryOptions &options )
{
    PluginRuntimeHost::instance().bootstrap( options );
}

void PluginRuntimeHost::bootstrap( const exprs::PluginRegistryOptions &options )
{
    std::lock_guard<std::mutex> lock( mMutex );
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    registry.setContributionSink( this );
    registry.configure( options );
    mBootstrapped = true;
    installManifestContributions();
}

void PluginRuntimeHost::installManifestContributions()
{
    // Caller holds mMutex (bootstrap) — the registries below are independent.
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();

    for ( const exprs::PluginRecord &record : registry.records() )
    {
        if ( record.state != exprs::PluginState::Validated && record.state != exprs::PluginState::Loaded )
            continue;
        for ( const exprs::ManifestOperator &op : record.manifest.operators )
            installPluginOperator( record.id(), op );
        installPluginAgentTools( record );
        installPluginModelRuntimes( record );
    }
}

void PluginRuntimeHost::installPluginOperator( const std::string &pluginId,
                                               const exprs::ManifestOperator &op )
{
    if ( mOperators.count( op.id ) )
        return; // first registration wins; duplicates are diagnosed at load

    OperatorEntry entry;
    entry.pluginId = pluginId;
    entry.manifest = op;
    entry.isExternalTool = op.hasExternalTool;
    if ( op.hasExternalTool )
    {
        const exprs::PluginRecord *record = exprs::PluginRegistry::instance().record( pluginId );
        const std::string pluginDir = record ? record->directory : std::string();
        entry.factory = [op, opId = op.id, pluginDir]() -> std::unique_ptr<sicnu::operators::RSOperator> {
            return std::make_unique<ExternalToolOperator>( opId, op, pluginDir );
        };
    }
    mOperators[op.id] = entry;

    // Lazy RSOperatorRegistry factory: registry consumers (JobEngine direct
    // path) instantiate without touching AtomicAlgorithmRegistry.
    if ( entry.factory )
    {
        auto factory = entry.factory;
        sicnu::operators::RSOperatorRegistry::instance().registerOperator(
            op.id, [factory]() { return factory(); } );
    }

    // Lazy AtomicAlgorithmRegistry adapter: descriptor from manifest, binary
    // loaded on first execute. The factory is resolved through the host at
    // call time: binary plugins register their factory via the sink during
    // load, which happens after this adapter was installed.
    std::function<bool()> ensureLoaded = [pluginId]() {
        return exprs::PluginRegistry::instance().ensureLoaded( pluginId );
    };
    std::function<std::unique_ptr<sicnu::operators::RSOperator>()> lazyFactory =
        [opId = op.id]() -> std::unique_ptr<sicnu::operators::RSOperator> {
            auto factory = PluginRuntimeHost::instance().resolveOperatorFactory( opId );
            return factory ? factory() : nullptr;
        };
    auto adapter = std::make_shared<PluginOperatorAdapter>( op, lazyFactory, ensureLoaded );
    sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter( adapter );
}

std::function<std::unique_ptr<sicnu::operators::RSOperator>()>
PluginRuntimeHost::resolveOperatorFactory( const std::string &operatorId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    auto iterator = mOperators.find( operatorId );
    if ( iterator == mOperators.end() )
        return nullptr;
    return iterator->second.factory;
}

void PluginRuntimeHost::installPluginAgentTools( const exprs::PluginRecord &record )
{
    if ( record.manifest.agentTools.empty() )
        return;
    if ( !mRegisteredAgentToolIds.empty()
         && std::find( mRegisteredAgentToolIds.begin(), mRegisteredAgentToolIds.end(),
                       record.id() )
                != mRegisteredAgentToolIds.end() )
        return;

    auto provider = std::make_shared<PluginAgentToolProvider>( record.id(),
                                                               record.manifest.agentTools );
    sicnu::agent::tool_catalog::AgentToolCatalog::instance().registerProvider( provider );
    mRegisteredAgentToolIds.push_back( record.id() );
}

void PluginRuntimeHost::installPluginModelRuntimes( const exprs::PluginRecord &record )
{
    for ( const exprs::ManifestModelRuntime &runtime : record.manifest.modelRuntimes )
    {
        if ( mModelRuntimeFactories.count( runtime.framework ) )
            continue;
        // Reserve the framework key; the executable factory arrives through
        // registerModelRuntime() when the plugin actually loads.
        mModelRuntimeFactories[runtime.framework] = nullptr;
#if defined( SICNU_HAS_OPENCV )
        registerPluginModelRuntime( runtime.framework, record.id() );
#endif
    }
}

void PluginRuntimeHost::revokePlugin( const std::string &pluginId )
{
    revokePluginContributions( pluginId );
}

void PluginRuntimeHost::revokePluginContributions( const std::string &pluginId )
{
    std::lock_guard<std::mutex> lock( mMutex );
    for ( auto iterator = mOperators.begin(); iterator != mOperators.end(); )
    {
        if ( iterator->second.pluginId == pluginId )
        {
            sicnu::operators::RSOperatorRegistry::instance().unregisterOperator( iterator->first );
            sicnu::processing::AtomicAlgorithmRegistry::instance().unregisterAdapter( iterator->first );
            iterator = mOperators.erase( iterator );
        }
        else
        {
            ++iterator;
        }
    }
    for ( const auto &entry : mModelRuntimeFactories )
        clearPluginModelRuntimeFactory( entry.first );
    PluginAgentToolProvider::unregisterPluginExecutors( pluginId );
    DataProviderRegistry::instance().unregisterPlugin( pluginId );
    mRegisteredAgentToolIds.erase(
        std::remove( mRegisteredAgentToolIds.begin(), mRegisteredAgentToolIds.end(), pluginId ),
        mRegisteredAgentToolIds.end() );
}

bool PluginRuntimeHost::isPluginOperator( const std::string &operatorId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    return mOperators.count( operatorId ) > 0;
}

const exprs::ManifestOperator *PluginRuntimeHost::manifestOperator(
    const std::string &operatorId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    auto iterator = mOperators.find( operatorId );
    if ( iterator == mOperators.end() )
        return nullptr;
    return &iterator->second.manifest;
}

bool PluginRuntimeHost::registerOperatorFactory(
    const std::string &pluginId, const std::string &operatorId,
    std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory )
{
    std::lock_guard<std::mutex> lock( mMutex );
    if ( operatorId.empty() || !factory )
        return false;

    auto entry = mOperators.find( operatorId );
    if ( entry == mOperators.end() )
    {
        // Binary-registered operator without a manifest declaration: build
        // its descriptor from a throwaway instance so the atomic catalog
        // entry is accurate (the conformance kit flags the manifest gap).
        sicnu::processing::AlgorithmDescriptor descriptor;
        try
        {
            auto probe = factory();
            if ( probe )
                descriptor = sicnu::processing::AlgorithmDescriptorBuilder::buildFromRsOperator( *probe );
        }
        catch ( ... )
        {
            return false;
        }
        if ( descriptor.id.empty() )
            descriptor.id = operatorId;

        OperatorEntry newEntry;
        newEntry.pluginId = pluginId;
        newEntry.factory = factory;
        mOperators[operatorId] = std::move( newEntry );
        sicnu::operators::RSOperatorRegistry::instance().registerOperator( operatorId,
                                                                           mOperators[operatorId].factory );
        sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
            std::make_shared<PluginOperatorAdapter>( std::move( descriptor ), factory, nullptr ) );
        return true;
    }
    entry->second.factory = factory;
    sicnu::operators::RSOperatorRegistry::instance().registerOperator( operatorId,
                                                                       entry->second.factory );
    return true;
}

bool PluginRuntimeHost::registerDataProvider(
    const std::string &pluginId, const std::string &providerId,
    std::shared_ptr<exprs::IPluginDataProviderV1> provider )
{
    const exprs::ManifestDataProvider *declaration = nullptr;
    exprs::ManifestDataProvider fallback;
    fallback.id = providerId;
    fallback.displayName = providerId;
    {
        std::lock_guard<std::mutex> lock( mMutex );
        const exprs::PluginRecord *record = exprs::PluginRegistry::instance().record( pluginId );
        if ( record )
        {
            for ( const exprs::ManifestDataProvider &candidate : record->manifest.dataProviders )
            {
                if ( candidate.id == providerId )
                {
                    declaration = &candidate;
                    break;
                }
            }
        }
    }
    const exprs::ManifestDataProvider &info = declaration ? *declaration : fallback;
    std::vector<std::string> schemes = info.schemes;
    return DataProviderRegistry::instance().registerProvider(
        pluginId, providerId, info.displayName, info.description, schemes, std::move( provider ) );
}

bool PluginRuntimeHost::registerModelRuntime(
    const std::string &pluginId, const std::string &framework,
    exprs::PluginModelRuntimeFactoryV1 factory )
{
    std::lock_guard<std::mutex> lock( mMutex );
    storePluginModelRuntimeFactory( framework, pluginId, factory );
    mModelRuntimeFactories[framework] = std::move( factory );
    return true;
}

bool PluginRuntimeHost::registerAgentTool( const std::string &pluginId, const std::string &toolId,
                                           std::shared_ptr<exprs::IPluginAgentToolV1> tool )
{
    if ( !tool || toolId.empty() )
        return false;
    // Manifest-declared tools surface through PluginAgentToolProvider; the
    // executor lookup happens by id at execute time.
    PluginAgentToolProvider::registerExecutor( pluginId, toolId, std::move( tool ) );
    return true;
}

} // namespace sicnu::plugins
