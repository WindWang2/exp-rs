/***************************************************************************
 * exprs/plugin_registry.h — process-wide plugin registry & lifecycle owner
 *
 * The registry is the single owner of plugin lifecycle state:
 *
 *   configure()/refresh()  scan roots, validate manifests, apply policy and
 *                          the user enable/disable index — no dlopen here,
 *                          so refresh is cheap and startup stays fast.
 *   load()/unload()        drive the PluginV1 lifecycle through a
 *                          PluginContributionSink provided by the host
 *                          runtime. Plugin failures produce diagnostics and
 *                          state transitions, never crashes in the host.
 *   ensureLoaded()         the lazy seam host-side factories call on first
 *                          use of a manifest-declared contribution.
 *
 * Thread safety: methods are serialized internally. Host-side registration
 * into application registries happens inside load() under the same lock, so
 * registration callbacks must not re-enter the registry.
 ***************************************************************************/
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_permissions.h"

namespace exprs {

struct PluginRegistryOptions
{
    /// Scan roots; when empty, defaultRoots(appDir, installDataDir) is used.
    std::vector<std::string> roots;
    std::string appDir;
    std::string installDataDir;
    PluginPolicy policy = PluginPolicy::fromEnvironment();
    std::string tempDirectory;
    std::string workspaceRoot;
    std::string dataDirectory;
    std::function<void( const char *, const std::string & )> logSink;
};

class PluginRegistry
{
public:
    static PluginRegistry &instance();

    /// Installs options and performs the first refresh. Safe to call again
    /// (re-configures and rescans).
    void configure( const PluginRegistryOptions &options );
    const PluginRegistryOptions &options() const { return mOptions; }

    /// The sink that receives contributions from loaded plugins (owned by
    /// the host runtime). Must be set before any load().
    void setContributionSink( PluginContributionSink *sink ) { mSink = sink; }

    /// Rescans roots and re-evaluates validation + policy. Loaded plugins
    /// are untouched (their records keep state Loaded).
    void refresh();

    const std::vector<PluginRecord> &records() const { return mRecords; }
    const PluginRecord *record( const std::string &pluginId ) const;
    PluginRecord *record( const std::string &pluginId );
    std::vector<std::string> pluginIds() const;
    const PluginDiagnosticLog &diagnostics() const { return mDiagnostics; }

    // -- lifecycle ----------------------------------------------------------
    /// Loads a plugin (native). Returns true when the plugin ends up in
    /// state Loaded. All failures land in diagnostics().
    bool load( const std::string &pluginId );
    /// Eager-load path used by the GUI shell: loads every native plugin in
    /// Validated state (CLI stays lazy). Returns the loaded ids.
    std::vector<std::string> loadAllValidated();
    /// Ids of currently loaded plugins (order = load order).
    std::vector<std::string> loadedPluginIds() const;
    /// Loaded handle for a plugin (nullptr when not loaded).
    const LoadedPlugin *loaded( const std::string &pluginId ) const;
    /// Unloads a loaded plugin (calls shutdown + dlclose).
    bool unload( const std::string &pluginId );
    /// Unloads every loaded plugin (host shutdown path).
    void unloadAll();
    /// Loads the plugin if not loaded yet; returns true when loaded.
    bool ensureLoaded( const std::string &pluginId );
    bool isLoaded( const std::string &pluginId ) const;

    // -- user enable/disable -------------------------------------------------
    /// Persists enable/disable in the user plugin index. Returns false when
    /// the plugin is unknown.
    bool setEnabled( const std::string &pluginId, bool enabled );
    /// True when the plugin is neither user-disabled nor policy-blocked.
    bool isEnabled( const std::string &pluginId ) const;

private:
    PluginRegistry() = default;
    ~PluginRegistry();

    void applyPolicyAndIndex();
    /// Core load path; caller must hold the registry mutex.
    bool loadUnlocked( const std::string &pluginId );
    std::string userIndexPath() const;
    void loadUserIndex();
    void saveUserIndex() const;

    PluginRegistryOptions mOptions;
    std::vector<PluginRecord> mRecords;
    PluginDiagnosticLog mDiagnostics;
    PluginContributionSink *mSink = nullptr;
    std::unique_ptr<PluginLoader> mLoader;
    std::vector<LoadedPlugin> mLoaded; // parallel to nothing; lookup by pluginId
    std::unique_ptr<HostServicesV1> mServices;
    std::vector<std::string> mDisabledIds; // persisted user index
};

} // namespace exprs
