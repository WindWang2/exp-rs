/***************************************************************************
 * exprs/plugin_loader.h — native plugin binary loading (ABI-gated)
 *
 * The loader resolves and maps a plugin shared library (dlopen on POSIX,
 * LoadLibraryW on Windows) and drives the PluginV1 lifecycle. It never runs
 * manifest validation itself — the registry guarantees records handed here
 * are Validated; it does re-enforce entrypoint containment at load time
 * (exprs/path_policy.h) to close the validation→load TOCTOU window.
 * Qt-free: the SDK core stays independent of the Qt build.
 ***************************************************************************/
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "exprs/plugin_interface.h"
#include "exprs/plugin_record.h"

namespace exprs {

/// Sink that receives contributions registered by a loaded plugin. The host
/// (plugins/framework) implements this to forward into the application
/// registries (RSOperatorRegistry, AtomicAlgorithmRegistry, ...). The SDK
/// core stays dependency-free of the app libraries.
class PluginContributionSink
{
public:
    virtual ~PluginContributionSink() = default;

    virtual bool registerOperatorFactory(
        const std::string &pluginId, const std::string &operatorId,
        std::function<std::unique_ptr<sicnu::operators::RSOperator>()> factory ) = 0;
    virtual bool registerDataProvider( const std::string &pluginId, const std::string &providerId,
                                       std::shared_ptr<IPluginDataProviderV1> provider ) = 0;
    virtual bool registerModelRuntime( const std::string &pluginId, const std::string &framework,
                                       PluginModelRuntimeFactoryV1 factory ) = 0;
    virtual bool registerAgentTool( const std::string &pluginId, const std::string &toolId,
                                    std::shared_ptr<IPluginAgentToolV1> tool ) = 0;

    /// Revokes every contribution registered by @p pluginId. MUST be called
    /// before the plugin library is unloaded (dlclose) so no host-side
    /// callable survives the unmapping of its code. Appended in V1.0 with a
    /// no-op default.
    virtual void revokePlugin( const std::string &pluginId ) { (void)pluginId; }

    // -- quiescence barrier (appended with the lifecycle work, #747) --------
    // The registry drives the unload sequence through these hooks; the host
    // runtime implements them over its execution barrier so in-flight plugin
    // executions drain (or the unload is refused) BEFORE revoke+dlclose.

    /// Arms the drain for @p pluginId: host-side dispatch must start failing
    /// with a typed refusal. Called before waitPluginIdle.
    virtual void beginPluginDrain( const std::string &pluginId ) { (void)pluginId; }
    /// Bounded wait until no execution is inside @p pluginId's code.
    /// Returns false when still busy after @p timeoutMs.
    virtual bool waitPluginIdle( const std::string &pluginId, int timeoutMs )
    {
        (void)pluginId;
        (void)timeoutMs;
        return true;
    }
    /// Cancels an armed drain (unload refused; the plugin stays usable).
    virtual void cancelPluginDrain( const std::string &pluginId ) { (void)pluginId; }
    /// The plugin ended up Loaded again (fresh load after unload/refusal).
    /// The host reopens its execution-barrier entry and re-installs
    /// manifest-declared contributions revoked by a previous unload (#755).
    virtual void pluginLoaded( const std::string &pluginId ) { (void)pluginId; }
};

/// A loaded plugin instance plus its library handle.
struct LoadedPlugin
{
    std::string pluginId;
    PluginV1 *instance = nullptr;
    void *libraryHandle = nullptr;  ///< opaque dlopen/LoadLibrary handle
    /// Resolved from the optional second entry point
    /// EXPRS_createUiContributionV1 (exprs/plugin_ui.h). Owned by the
    /// plugin library — never deleted here.
    void *uiContribution = nullptr;
    /// initialize() succeeded and shutdown() has not run yet. Must survive
    /// take() so a transferred handle still honours the shutdown contract.
    bool initialized = false;
    bool shutdownCalled = false;
};

class PluginLoader
{
public:
    ~PluginLoader();

    /// Creates the default HostServices implementation (temp/workspace/data
    /// dirs plus a log sink). Exposed so hosts and tests can drive the
    /// loader without inventing their own HostServicesV1 subclass.
    static std::unique_ptr<HostServicesV1> createDefaultHostServices(
        std::string tempDirectory, std::string workspaceRoot, std::string dataDirectory,
        std::function<void( const char *, const std::string & )> logSink = {} );

    /// dlopens <record.directory>/<manifest.entrypoint>, resolves
    /// EXPRS_createPluginV1, instantiates the plugin and drives
    /// initialize() + registerContributions() into @p sink.
    /// On success returns true and stores the handle; the instance is
    /// owned by the loader (see take()).
    bool load( const PluginRecord &record, HostServicesV1 &services,
               PluginContributionSink &sink, PluginDiagnosticLog &log );

    /// Calls shutdown() and unloads the library.
    bool unload( LoadedPlugin &plugin, PluginDiagnosticLog &log );

    LoadedPlugin take();

    /// Symbol check without executing anything (used by `plugin doctor`):
    /// verifies the entrypoint symbol exists in the library.
    static bool probeEntrypoint( const std::string &libraryPath, std::string &error );

private:
    bool loadImpl( const PluginRecord &record, HostServicesV1 &services,
                   PluginContributionSink &sink, PluginDiagnosticLog &log );
    void markShutdownCalled( LoadedPlugin &plugin );

    LoadedPlugin mLoaded;
    bool mInstanceValid = false;
    /// Sink used during load — revoked again on unload as a safety net so
    /// plugin-originated callables never outlive the unmapped library.
    PluginContributionSink *mLoadedSink = nullptr;
};

} // namespace exprs
