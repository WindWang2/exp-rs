/***************************************************************************
 * src/plugins/framework/plugin_runtime_host.cpp
 ***************************************************************************/
#include "plugin_runtime_host.h"

#include "plugins/host/plugin_host_process_runtime.h"

#include <cstdlib>

#include "exprs/plugin_capabilities.h"

#include "data_provider_registry.h"
#include "external_tool_operator.h"
#include "plugin_agent_tool_provider.h"
#include "plugin_execution_barrier.h"
#include "plugin_model_runtime_bridge.h"
#include "plugin_operator_adapter.h"
#include "plugin_ui_host.h"
#include "plugin_ui_schema_host.h"

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

Json::Value PluginRuntimeHost::hostProcessSnapshot() const
{
    std::lock_guard<std::mutex> lock( mMutex );
    if ( !mHostProcessRuntime )
        return Json::Value();
    return mHostProcessRuntime->diagnosticsSnapshot();
}

Json::Value PluginRuntimeHost::describePluginUiSchema( const std::string &pluginId )
{
    Json::Value result( Json::objectValue );
    result["ok"] = false;
    result["error"] = "host-process runtime is not installed (E6006)";
    // Review A1: look the runtime up under the lock, then describe WITHOUT
    // holding mMutex — the describe blocks on a worker IPC round-trip, and
    // a wedged plugin must not stall the GUI thread's next invoke/bootstrap
    // call. The raw pointer is safe: the unique_ptr is only ever ASSIGNED
    // in bootstrap (never reset), so it cannot dangle.
    mMutex.lock();
    auto *runtime = mHostProcessRuntime.get();
    mMutex.unlock();
    if ( runtime )
    {
        exprs::PluginDiagnosticLog log;
        result = runtime->describeUiSchema( pluginId, log );
    }
    return result;
}

Json::Value PluginRuntimeHost::invokePluginUi( const std::string &pluginId,
                                               const Json::Value &event, int timeoutMs )
{
    Json::Value result( Json::objectValue );
    result["ok"] = false;
    result["error"] = "host-process runtime is not installed (E6006)";
    // Review A1: look the runtime up under the lock, then invoke WITHOUT
    // holding mMutex — the invoke blocks up to timeoutMs, and a wedged
    // plugin must not stall the GUI thread's next describe/bootstrap call.
    mMutex.lock();
    auto *runtime = mHostProcessRuntime.get();
    mMutex.unlock();
    if ( runtime )
    {
        exprs::PluginDiagnosticLog log;
        result = runtime->invokeUi( pluginId, event, timeoutMs, log );
    }
    return result;
}

void PluginRuntimeHost::bootstrap( const exprs::PluginRegistryOptions &options )
{
    std::lock_guard<std::mutex> lock( mMutex );
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    registry.setContributionSink( this );

    // Isolation runtime 5.0: install the out-of-process hosting strategy.
    // SICNU_PLUGIN_HOST_PROCESS=off refuses host-process plugins typed
    // instead (E6006); anything else installs the runtime so manifests may
    // opt into worker isolation.
    {
        const char *flag = std::getenv( "SICNU_PLUGIN_HOST_PROCESS" );
        const bool disabled = flag && std::string( flag ) == "off";
        if ( !disabled && !mHostProcessRuntime )
        {
            exprs::PluginDiagnosticLog setupLog;
            sicnu::plugins::PluginHostProcessRuntime::Options runtimeOptions;
            if ( !options.hostProcessWorkerPath.empty() )
                runtimeOptions.workerPath = options.hostProcessWorkerPath;
            mHostProcessRuntime =
                std::make_unique<sicnu::plugins::PluginHostProcessRuntime>( runtimeOptions );
        }
        registry.setHostProcessRuntime( disabled ? nullptr : mHostProcessRuntime.get() );
    }

    registry.configure( options );
    mBootstrapped = true;
    installManifestContributions();
}

void PluginRuntimeHost::installManifestContributions()
{
    // Caller holds mMutex (bootstrap) — the registries below are independent.
    // Snapshot ids then copy each record under the registry lock: never hold
    // a live pointer/reference into mRecords across refresh() (issue #932/#943).
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    for ( const std::string &pluginId : registry.pluginIds() )
    {
        exprs::PluginRecord snapshot;
        if ( !registry.copyRecord( pluginId, snapshot ) )
            continue;
        if ( snapshot.state != exprs::PluginState::Validated
             && snapshot.state != exprs::PluginState::Loaded )
            continue;
        installManifestContributionsFor( pluginId );
    }
}

void PluginRuntimeHost::installManifestContributionsFor( const std::string &pluginId )
{
    exprs::PluginRecord record;
    if ( !exprs::PluginRegistry::instance().copyRecord( pluginId, record ) )
        return;
    for ( const exprs::ManifestOperator &op : record.manifest.operators )
        installPluginOperator( pluginId, op );
    installPluginAgentTools( record );
    installPluginModelRuntimes( record );
}

void PluginRuntimeHost::installPluginOperator( const std::string &pluginId,
                                               const exprs::ManifestOperator &op )
{
    auto existing = mOperators.find( op.id );
    if ( existing != mOperators.end() && existing->second.pluginId != pluginId )
        return; // first registration wins; duplicates are diagnosed at load

    const bool created = existing == mOperators.end();
    if ( created )
    {
        OperatorEntry entry;
        entry.pluginId = pluginId;
        entry.manifest = op;
        entry.isExternalTool = op.hasExternalTool;
        if ( op.hasExternalTool )
        {
            // Copies under the registry lock (no raw record pointer held).
            const std::string pluginDir =
                exprs::PluginRegistry::instance().pluginDirectoryFor( pluginId );
            const Json::Value access =
                exprs::PluginRegistry::instance().accessDeclarationFor( pluginId );
            // Capability gate (9.0): explicit access.externalProcess:false
            // produces operators that refuse to spawn (typed PolicyRefused).
            const bool spawnAllowed =
                exprs::accessBool( access, "externalProcess" ) != 0;
            entry.factory =
                [op, opId = op.id, pluginDir, spawnAllowed]() -> std::unique_ptr<sicnu::operators::RSOperator> {
                return std::make_unique<ExternalToolOperator>( opId, op, pluginDir,
                                                               spawnAllowed );
            };
        }
        mOperators[op.id] = entry;

        // Lazy RSOperatorRegistry factory (JobEngine direct path): only on
        // entry creation — a reload lands here with the live proxy or
        // external-tool factory already in place and must not re-wrap it.
        // The wrapper acquires the execution lease at CREATE time and holds
        // it for the operator instance's lifetime (created -> run ->
        // destroyed), keeping the drain honest for #747.
        auto factory = mOperators[op.id].factory;
        if ( factory )
        {
            sicnu::operators::RSOperatorRegistry::instance().registerOperator(
                op.id,
                [pluginId, factory]() -> std::unique_ptr<sicnu::operators::RSOperator> {
                    auto lease = PluginExecutionBarrier::instance().acquire( pluginId );
                    if ( !lease )
                        return nullptr; // plugin unloading/unloaded: clean refusal
                    auto inner = factory();
                    if ( !inner )
                        return nullptr;
                    return std::make_unique<LeaseHoldingOperator>( std::move( inner ),
                                                                   std::move( lease ) );
                } );
        }
    }

    // Lazy AtomicAlgorithmRegistry adapter: descriptor from manifest, binary
    // loaded on first execute. The factory is resolved through the host at
    // call time: binary plugins register their factory via the sink during
    // load, which happens after this adapter was installed. Re-registered on
    // EVERY call (registerAdapter replaces by id): unload revokes the
    // adapter, and a reload after unload must restore it (#755 round-trip;
    // baseline regressed this for host-process proxies).
    std::function<bool()> ensureLoaded = [pluginId]() {
        return exprs::PluginRegistry::instance().ensureLoaded( pluginId );
    };
    std::function<std::unique_ptr<sicnu::operators::RSOperator>()> lazyFactory =
        [opId = op.id]() -> std::unique_ptr<sicnu::operators::RSOperator> {
            auto factory = PluginRuntimeHost::instance().resolveOperatorFactory( opId );
            return factory ? factory() : nullptr;
        };
    auto adapter = std::make_shared<PluginOperatorAdapter>( op, pluginId, lazyFactory, ensureLoaded );
    sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter( adapter );
}

std::function<std::unique_ptr<sicnu::operators::RSOperator>()>
PluginRuntimeHost::resolveOperatorFactory( const std::string &operatorId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    auto iterator = mOperators.find( operatorId );
    if ( iterator == mOperators.end() )
    {
        return nullptr;
    }
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
        // Capability gate (9.0): a manifest that declares an access object
        // with a modelProvider.frameworks list may only serve listed
        // frameworks. Refused entries are simply NOT installed — the
        // declared-but-not-registered diagnostic (loader/conformance)
        // surfaces the gap, never a silent usable surface.
        if ( !exprs::modelFrameworkAllowed( record.manifest.access, runtime.framework ) )
            continue;
        if ( mModelRuntimeFactories.count( runtime.framework ) )
            continue;
        // Reserve the framework key; the executable factory arrives through
        // registerModelRuntime() when the plugin actually loads.
        mModelRuntimeFactories[runtime.framework] = nullptr;
        mModelRuntimeOwners[runtime.framework] = record.id();
#if defined( SICNU_HAS_OPENCV )
        registerPluginModelRuntime( runtime.framework, record.id() );
#endif
    }
}

void PluginRuntimeHost::revokePlugin( const std::string &pluginId )
{
    revokePluginContributions( pluginId );
}

void PluginRuntimeHost::beginPluginDrain( const std::string &pluginId )
{
    PluginExecutionBarrier::instance().beginDrain( pluginId );
}

bool PluginRuntimeHost::waitPluginIdle( const std::string &pluginId, int timeoutMs )
{
    return PluginExecutionBarrier::instance().waitIdle( pluginId, timeoutMs );
}

void PluginRuntimeHost::cancelPluginDrain( const std::string &pluginId )
{
    PluginExecutionBarrier::instance().cancelDrain( pluginId );
}

void PluginRuntimeHost::pluginLoaded( const std::string &pluginId )
{
    // Fresh load after unload/refusal: reopen the barrier entry (new
    // generation — pre-unload handles stay invalid) and restore the
    // manifest-declared contributions the previous revoke removed (#755).
    PluginExecutionBarrier::instance().open( pluginId );
    std::lock_guard<std::mutex> lock( mMutex );
    installManifestContributionsFor( pluginId );
}

void PluginRuntimeHost::revokePluginContributions( const std::string &pluginId )
{
    // Contract (issue #747): runs while the plugin library is still mapped,
    // after the execution barrier drained. Order matters — UI contributions
    // first (widgets/actions created by the plugin are destroyed here while
    // its code can still service destructors and vtables), then registries.
    // Plugin-platform 8.0: the declarative schema rendering is host-owned,
    // but its events travel to the plugin — detach it before the worker
    // goes away, same lifecycle position as the in-process UI release.
    PluginUiSchemaRenderer::instance()->releasePluginUi( QString::fromStdString( pluginId ) );
    PluginUiHost::instance()->releasePluginUi( QString::fromStdString( pluginId ) );

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
    for ( auto iterator = mModelRuntimeFactories.begin();
          iterator != mModelRuntimeFactories.end(); )
    {
        const auto owner = mModelRuntimeOwners.find( iterator->first );
        if ( owner != mModelRuntimeOwners.end() && owner->second == pluginId )
        {
            clearPluginModelRuntimeFactory( iterator->first );
            mModelRuntimeOwners.erase( owner );
            iterator = mModelRuntimeFactories.erase( iterator );
        }
        else
        {
            ++iterator;
        }
    }
    PluginAgentToolProvider::unregisterPluginExecutors( pluginId );
    DataProviderRegistry::instance().unregisterPlugin( pluginId );
    mRegisteredAgentToolIds.erase(
        std::remove( mRegisteredAgentToolIds.begin(), mRegisteredAgentToolIds.end(), pluginId ),
        mRegisteredAgentToolIds.end() );
    // Model runtime sessions cache PluginModelRuntimeAdapter instances that
    // own raw plugin objects: release them BEFORE the library is unmapped
    // (P0 review finding — cached sessions would otherwise outlive dlclose).
#if defined( SICNU_HAS_OPENCV )
    sicnu::operators::runtime::ModelRuntimeRegistry::instance().releaseAll();
#endif
    // Permanent close: stale adapters/executors held elsewhere must fail
    // with a typed refusal, never call into the unmapped library. A later
    // successful load reopens the entry with a fresh generation.
    PluginExecutionBarrier::instance().close( pluginId );
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
        auto wrappedFactory = mOperators[operatorId].factory;
        const std::string owner = pluginId;
        sicnu::operators::RSOperatorRegistry::instance().registerOperator(
            operatorId,
            [owner, wrappedFactory]() -> std::unique_ptr<sicnu::operators::RSOperator> {
                auto lease = PluginExecutionBarrier::instance().acquire( owner );
                if ( !lease )
                    return nullptr;
                auto inner = wrappedFactory();
                if ( !inner )
                    return nullptr;
                return std::make_unique<LeaseHoldingOperator>( std::move( inner ),
                                                               std::move( lease ) );
            } );
        sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
            std::make_shared<PluginOperatorAdapter>( std::move( descriptor ), pluginId, factory,
                                                     nullptr ) );
        return true;
    }
    entry->second.factory = factory;
    // Lease-holding wrapper for the direct RSOperatorRegistry path (#747):
    // same contract as installPluginOperator above.
    {
        auto wrappedFactory = entry->second.factory;
        const std::string owner = pluginId;
        sicnu::operators::RSOperatorRegistry::instance().registerOperator(
            operatorId,
            [owner, wrappedFactory]() -> std::unique_ptr<sicnu::operators::RSOperator> {
                auto lease = PluginExecutionBarrier::instance().acquire( owner );
                if ( !lease )
                    return nullptr;
                auto inner = wrappedFactory();
                if ( !inner )
                    return nullptr;
                return std::make_unique<LeaseHoldingOperator>( std::move( inner ),
                                                               std::move( lease ) );
            } );
    }
    // A reload (enable round-trip, #755) re-registers through the sink AFTER
    // unload revoked the catalog adapter: restore it so the atomic catalog
    // reflects the contribution again.
    if ( !sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( operatorId ) )
    {
        sicnu::processing::AlgorithmDescriptor descriptor;
        if ( entry->second.manifest.id.empty() )
        {
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
            sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
                std::make_shared<PluginOperatorAdapter>( std::move( descriptor ), pluginId,
                                                         entry->second.factory, nullptr ) );
        }
        else
        {
            sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
                std::make_shared<PluginOperatorAdapter>( entry->second.manifest, pluginId,
                                                         entry->second.factory, nullptr ) );
        }
    }
    return true;
}

bool PluginRuntimeHost::registerDataProvider(
    const std::string &pluginId, const std::string &providerId,
    std::shared_ptr<exprs::IPluginDataProviderV1> provider )
{
    exprs::ManifestDataProvider info;
    info.id = providerId;
    info.displayName = providerId;
    // LOCK ORDER: copy the record BEFORE mMutex — same inversion registerModelRuntime
    // documents. copyRecord snapshots under the registry lock; a raw
    // record() pointer would dangle across a concurrent refresh (#932).
    {
        exprs::PluginRecord snapshot;
        if ( exprs::PluginRegistry::instance().copyRecord( pluginId, snapshot ) )
        {
            for ( const exprs::ManifestDataProvider &candidate : snapshot.manifest.dataProviders )
            {
                if ( candidate.id == providerId )
                {
                    info = candidate;
                    break;
                }
            }
        }
    }
    std::vector<std::string> schemes = info.schemes;
    return DataProviderRegistry::instance().registerProvider(
        pluginId, providerId, info.displayName, info.description, schemes, std::move( provider ) );
}

bool PluginRuntimeHost::registerModelRuntime(
    const std::string &pluginId, const std::string &framework,
    exprs::PluginModelRuntimeFactoryV1 factory )
{
    // Capability gate (9.0): a binary-registered runtime for a framework the
    // manifest's access object does not list is refused at the sink — the
    // registration "failure" path is the established honest signal.
    // LOCK ORDER: the registry lookup (and its copy) happens BEFORE mMutex —
    // the load path holds the registry lock while entering this sink, so
    // taking mMutex first would risk an AB-BA inversion.
    {
        const Json::Value access =
            exprs::PluginRegistry::instance().accessDeclarationFor( pluginId );
        if ( !exprs::modelFrameworkAllowed( access, framework ) )
            return false;
    }
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
