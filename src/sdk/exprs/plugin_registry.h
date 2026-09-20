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
 * Thread safety: public methods serialize record mutation on an internal
 * mutex. load() marks the record Loading, copies what the runtime needs,
 * and drops that mutex across spawn / plugin.load / dlopen so a slow plugin
 * cannot stall record()/refresh() of other plugins. Contribution-sink
 * callbacks (pluginLoaded/revokePlugin) run WITHOUT the registry mutex;
 * they must not assume it is held, and must never take a host mutex while a
 * caller still holds the registry lock (AB-BA with PluginRuntimeHost::bootstrap).
 ***************************************************************************/
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_host_runtime.h"
#include "exprs/plugin_loader.h"
#include "exprs/plugin_permissions.h"
#include "exprs/plugin_snapshot.h"

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
    /// Explicit exprs_plugin_host_worker path (empty: locate next to the
    /// executable). Isolation runtime 5.0.
    std::string hostProcessWorkerPath;
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

    /// Installs the out-of-process hosting strategy (isolation runtime 5.0).
    /// When installed, native plugins whose manifest declares
    /// runtime "host-process" are delegated to it; when not installed such
    /// plugins are refused typed (E6006) instead of silently loading
    /// in-process. Not owned; must outlive the registry's use.
    void setHostProcessRuntime( HostProcessRuntime *runtime ) { mHostProcessRuntime = runtime; }
    HostProcessRuntime *hostProcessRuntime() const { return mHostProcessRuntime; }

    /// Rescans roots and re-evaluates validation + policy. Loaded plugins
    /// are untouched (their records keep state Loaded).
    void refresh();

    /// Snapshot of every record under the registry lock (by value). Prefer
    /// pluginIds() + copyRecord() for callers that only need a subset — a
    /// full copy is fine for small registries / one-shot CLI dumps.
    /// NEVER hold a reference/pointer into mRecords across refresh() (#943).
    std::vector<PluginRecord> records() const;
    const PluginRecord *record( const std::string &pluginId ) const;

    /// Plugin-platform 9.0: COPY accessors. record() hands out a pointer
    /// into the mRecords vector WITHOUT holding the registry lock on the
    /// caller's side, so a concurrent refresh (which reallocates the vector)
    /// makes later dereferences a use-after-free. Gates that only need the
    /// access declaration or the install directory must take copies through
    /// these instead of holding the raw pointer. copyRecord() snapshots the
    /// whole record under the lock for callers that need operators/tools.
    Json::Value accessDeclarationFor( const std::string &pluginId ) const;
    std::string pluginDirectoryFor( const std::string &pluginId ) const;
    bool copyRecord( const std::string &pluginId, PluginRecord &out ) const;
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
    /// Unloads a loaded plugin (quiesce → revoke contributions → shutdown →
    /// dlclose). Waits up to @p timeoutMs (0 = SICNU_PLUGIN_UNLOAD_TIMEOUT_MS,
    /// default 30 s) for in-flight executions; a busy plugin is REFUSED with
    /// a PluginInUse diagnostic and stays loaded. Returns false when the
    /// plugin was not loaded or the unload was refused.
    bool unload( const std::string &pluginId, int timeoutMs = 0 );
    /// Unloads every loaded plugin (host shutdown path).
    void unloadAll();
    /// Loads the plugin if not loaded yet; returns true when loaded.
    bool ensureLoaded( const std::string &pluginId );
    bool isLoaded( const std::string &pluginId ) const;

    // -- hot reload (plugin-platform 12.0, dev mode only) ----------------------
    /// Options for reload(). Everything defaults to the safest answer.
    struct ReloadOptions
    {
        /// Must be true for reload() to run any step. The GUI shell sets it
        /// only in dev builds/flag combos (SICNU_PLUGIN_DEV=1 also enables
        /// it through PluginPolicy); production never flips this.
        bool devMode = false;
        /// Optional state-migration seam (WP4). Invoked AFTER the new
        /// manifest validated but BEFORE the new code loads, with the
        /// plugin directory, so a host that persists plugin state can
        /// transform it (old schema -> new schema). Returning false aborts
        /// the reload with the OLD version still loaded. Default: identity
        /// (nothing to migrate) — v1 ships the seam, not a migration engine.
        std::function<bool( const std::string &pluginDir )> migrateState;
    };

    /// Development-mode hot reload of one loaded plugin:
    ///   1. re-validate the package directory (manifest gate) WITHOUT
    ///      touching the mapped code — a broken new manifest refuses the
    ///      reload and the old version stays loaded;
    ///   2. consult the last-known-good snapshot (marker-verified; a
    ///      still-in-flight capture is awaited bounded first);
    ///   3. drain + unload (existing barrier; a busy plugin is refused and
    ///      stays loaded);
    ///   4. run the optional state migration and load the new code;
    ///   5. on a load failure: restore the snapshot over the plugin
    ///      directory and load THAT instead (true rollback), or leave the
    ///      record Failed with diagnostics when even the rollback fails.
    /// Refused typed (dev mode off / not loaded / busy / broken manifest)
    /// — every refusal keeps the old version usable.
    bool reload( const std::string &pluginId, const ReloadOptions &options );

    // -- install-time upgrade (plugin-lifecycle 13.0) --------------------------
    /// Outcome vocabulary for installOrUpgrade(). Every refusal keeps the
    /// previously installed version running (or on disk); RolledBack means
    /// the drain/swap was attempted and the previous install was restored;
    /// Failed means the rollback itself could not restore the old bytes —
    /// the kept snapshot is named in diagnostics.
    enum class PluginUpgradeStatus
    {
        Installed,   ///< no previous install — PluginPackage::install ran
        Upgraded,    ///< new version committed (loaded iff it was loaded)
        Refused,     ///< gated before any change (bad manifest, policy,
                     ///< migration, busy drain, snapshot budget, concurrency)
        RolledBack,  ///< drain/swap attempted; previous install restored
        Failed,      ///< rollback could not restore the previous install
    };
    struct PluginUpgradeResult
    {
        PluginUpgradeStatus status = PluginUpgradeStatus::Refused;
        std::string pluginId;
        std::string installedDir;
    };
    struct PluginUpgradeOptions
    {
        /// Same seam as ReloadOptions::migrateState: runs while the OLD
        /// version is still loaded, before the drain. Returning false
        /// aborts the upgrade with nothing changed.
        std::function<bool( const std::string &pluginDir )> migrateState;
        /// Drain budget for the old version (0 = SICNU_PLUGIN_UNLOAD_TIMEOUT_MS
        /// default). Only consulted when the plugin is currently loaded.
        int drainTimeoutMs = 0;
    };

    /// Atomic install-time upgrade over an existing plugin:
    ///   stage-validate → policy/capability gate → snapshot the CURRENT
    ///   install (bounded, marker-verified) → migrate → drain →
    ///   PluginPackage::install's staged atomic swap → refresh → load →
    ///   commit (snapshot GC'd) or roll back to the snapshot.
    /// Works in BOTH production and dev mode — never consults
    /// SICNU_PLUGIN_DEV. One lifecycle operation per plugin at a time
    /// (shares the reload() guard); every failure path returns a typed
    /// status + diagnostics, never a half-upgraded install.
    PluginUpgradeResult installOrUpgrade( const std::string &sourceDir,
                                          const PluginUpgradeOptions &options );
    /// Convenience overload with default options (a default member
    /// initializer of a nested struct cannot serve as a default argument
    /// inside its own enclosing class).
    PluginUpgradeResult installOrUpgrade( const std::string &sourceDir )
    {
        return installOrUpgrade( sourceDir, PluginUpgradeOptions() );
    }

    /// Unload (drain-gated) then remove the package AND its last-good
    /// snapshot — the uninstall half of snapshot lifecycle (WP3). Returns
    /// false when the drain is refused (plugin stays loaded AND installed)
    /// or the package removal fails.
    bool uninstallPlugin( const std::string &pluginId, int timeoutMs = 0 );

    // -- user enable/disable -------------------------------------------------
    /// Persists enable/disable in the user plugin index. Returns false when
    /// the plugin is unknown.
    bool setEnabled( const std::string &pluginId, bool enabled );
    /// Snapshot of the persisted disabled-id list (conformance kit uses it
    /// to run round-trip checks without mutating the user's choices).
    std::vector<std::string> userDisabledIds() const;
    /// Replaces the persisted disabled-id list (restore counterpart).
    bool setUserDisabledIds( const std::vector<std::string> &ids );
    /// True when the plugin is neither user-disabled nor policy-blocked.
    bool isEnabled( const std::string &pluginId ) const;

private:
    PluginRegistry() = default;
    ~PluginRegistry();

    void applyPolicyAndIndex();
    /// WP2 (plugin-platform 12.0): records one Info audit event per declared
    /// permission on the registry diagnostics when a plugin ends up Loaded,
    /// so the granted path carries the same structured evidence as the
    /// denied one (E5001). No declared permissions = no events.
    void auditGrantedPermissions( const std::string &pluginId );
    /// Track 13.0: deterministic snapshot root inside the configured temp
    /// directory (<temp>/sicnu-plugin-snapshots).
    std::string snapshotRoot() const;
    /// WP4 (plugin-platform 12.0): directory holding the last-known-good
    /// copy of @p pluginId's package. Dev mode refreshes it after every
    /// successful load; reload() rolls back to it when the new bytes fail
    /// to load.
    std::string lastGoodSnapshotPath( const std::string &pluginId ) const;
    /// Per-upgrade rollback copy of the CURRENT install (both modes):
    /// <root>/upgrade-<id>-<pid>. Removed on commit/rollback, kept + named
    /// on a failed rollback, swept as crash residue by sweepPluginSnapshots.
    std::string upgradeSnapshotPath( const std::string &pluginId ) const;
    /// Replaces the last-known-good snapshot for @p pluginId with the bytes
    /// currently in @p pluginDir. No-op outside dev mode (production keeps no
    /// copies). Track 13.0: ASYNC — starts a bounded PluginSnapshotJob so the
    /// load() publish path never blocks on a whole-directory copy; reload()
    /// awaits the job (bounded) before consulting the snapshot.
    void refreshLastGoodSnapshot( const std::string &pluginId, const std::string &pluginDir );
    /// Waits (bounded) for an in-flight snapshot capture of @p pluginId and
    /// reaps the finished job. True = nothing left in flight.
    bool waitForSnapshotJob( const std::string &pluginId, int timeoutMs );
    /// Cancels + joins every in-flight snapshot job (teardown/unloadAll).
    void cancelSnapshotJobs();
    /// RAII release of this thread's lifecycle ownership of one plugin
    /// (the mReloading entry). Shared by reload()/installOrUpgrade()/
    /// uninstallPlugin() so every exit path clears the owner.
    struct LifecycleOwnerGuard
    {
        LifecycleOwnerGuard( PluginRegistry *owner, std::string id )
            : registry( owner ), pluginId( std::move( id ) )
        {
        }
        ~LifecycleOwnerGuard();
        LifecycleOwnerGuard( const LifecycleOwnerGuard & ) = delete;
        LifecycleOwnerGuard &operator=( const LifecycleOwnerGuard & ) = delete;
        PluginRegistry *registry;
        std::string pluginId;
    };
    /// Applies blocked/allowed/third-party-native/enforce-capability policy
    /// to one record's manifest (extracted from applyPolicyAndIndex so the
    /// upgrade gate runs the identical checks on a manifest that has no
    /// record yet). Mutates record.state/diagnostics on refusal.
    void applyPolicyGate( PluginRecord &record );
    /// Scan+validate+policy pass; caller must hold the registry mutex.
    void refreshUnlocked();
    std::string userIndexPath() const;
    void loadUserIndex();
    void saveUserIndex() const;

    PluginRegistryOptions mOptions;
    std::vector<PluginRecord> mRecords;
    PluginDiagnosticLog mDiagnostics;
    PluginContributionSink *mSink = nullptr;
    HostProcessRuntime *mHostProcessRuntime = nullptr;
    std::vector<std::string> mHostProcessLoaded; // pluginIds hosted out-of-process
    std::unique_ptr<PluginLoader> mLoader;
    std::vector<LoadedPlugin> mLoaded; // parallel to nothing; lookup by pluginId
    std::unique_ptr<HostServicesV1> mServices;
    std::vector<std::string> mDisabledIds; // persisted user index
    /// WP4 (plugin-platform 12.0): pluginIds with a reload() in flight;
    /// track 13.0 shares it with installOrUpgrade and records the OWNING
    /// thread — load()/unload() calls from any other thread are refused
    /// while an owner holds the entry, so a concurrent caller can never
    /// publish or tear down a generation mid-transaction. The owner's own
    /// re-entrant calls pass through. Guarded by gRegistryMutex.
    std::map<std::string, std::thread::id> mReloading;
    /// Track 13.0: in-flight async last-good captures, one slot per plugin.
    /// Replaced on supersede (the dropped job's dtor cancels + joins);
    /// reaped by waitForSnapshotJob / cancelSnapshotJobs.
    std::map<std::string, std::shared_ptr<PluginSnapshotJob>> mSnapshotJobs;
};

} // namespace exprs
