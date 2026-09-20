/***************************************************************************
 * exprs/plugin_registry.cpp
 ***************************************************************************/
#include "exprs/plugin_registry.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#include "exprs/plugin_package.h"
#include "exprs/plugin_validator.h"

namespace {
exprs::PluginState incompatibleStateFor( const std::string &pluginId,
                                         const exprs::PluginDiagnosticLog &log )
{
    for ( const exprs::PluginDiagnostic &item : log.items() )
    {
        if ( item.pluginId == pluginId && item.severity == exprs::PluginDiagnosticSeverity::Error
             && item.code >= exprs::PluginDiagnosticCode::ApiVersionMismatch
             && item.code <= exprs::PluginDiagnosticCode::PlatformUnsupported )
            return exprs::PluginState::Incompatible;
    }
    return exprs::PluginState::Broken;
}

/// Bounded wait for the quiescence barrier (issue #747): unload must drain
/// in-flight plugin executions or refuse — never unmap code under a running
/// thread. Overridable for tests and time-constrained callers.
int unloadTimeoutMs()
{
    const char *raw = std::getenv( "SICNU_PLUGIN_UNLOAD_TIMEOUT_MS" );
    if ( !raw || !*raw )
        return 30000;
    const long parsed = std::strtol( raw, nullptr, 10 );
    return parsed > 0 ? static_cast<int>( std::min<long>( parsed, 600000 ) ) : 30000;
}

/// Track 13.0: how long reload() waits for an in-flight async snapshot
/// capture before deciding there is no usable rollback source. The copy is
/// budget-bounded, so the default only matters for genuinely stuck I/O.
int snapshotWaitMs()
{
    const char *raw = std::getenv( "SICNU_PLUGIN_SNAPSHOT_WAIT_MS" );
    if ( !raw || !*raw )
        return 30000;
    const long parsed = std::strtol( raw, nullptr, 10 );
    return parsed > 0
               ? static_cast<int>( std::min<long>( std::max<long>( parsed, 1000 ), 600000 ) )
               : 30000;
}

} // namespace

namespace exprs {

namespace {
// Recursive: locked public accessors (record/records/...) are also used
// internally from paths that already hold the lock.
std::recursive_mutex gRegistryMutex;
bool gDestructing = false;

const LoadedPlugin *findLoaded( const std::vector<LoadedPlugin> &loaded,
                                const std::string &pluginId )
{
    for ( const LoadedPlugin &entry : loaded )
    {
        if ( entry.pluginId == pluginId )
            return &entry;
    }
    return nullptr;
}
} // namespace

PluginRegistry &PluginRegistry::instance()
{
    static PluginRegistry registry;
    return registry;
}

PluginRegistry::~PluginRegistry()
{
    // Static destruction: drain executor handles but never issue virtual
    // calls into a possibly-destroyed sink.
    gDestructing = true;
    try
    {
        unloadAll();
    }
    catch ( ... )
    {
    }
    gDestructing = false;
}

void PluginRegistry::configure( const PluginRegistryOptions &options )
{
    // Reconfiguration may redirect the snapshot root: captures in flight
    // against the OLD root must not publish there after the switch.
    // Cancel+join runs before the lock below (WP2 shutdown safety).
    cancelSnapshotJobs();
    std::string tempDirectory;
    std::vector<std::string> liveIds;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        mOptions = options;
        if ( mOptions.roots.empty() )
            mOptions.roots =
                PluginDiscovery::defaultRoots( mOptions.appDir, mOptions.installDataDir );
        if ( mOptions.tempDirectory.empty() )
        {
            const char *temp = std::getenv( "TMPDIR" );
            mOptions.tempDirectory = ( temp ? temp : "/tmp" );
        }
        if ( !mOptions.logSink )
        {
            mOptions.logSink = []( const char *level, const std::string &message ) {
                (void)level;
                (void)message;
            };
        }
        mServices = PluginLoader::createDefaultHostServices(
            mOptions.tempDirectory, mOptions.workspaceRoot, mOptions.dataDirectory,
            mOptions.logSink );
        loadUserIndex();
        refreshUnlocked();
        tempDirectory = mOptions.tempDirectory;
        liveIds = pluginIds();
        for ( const std::string &id : mHostProcessLoaded )
            liveIds.push_back( id );
        for ( const auto &entry : mSnapshotJobs )
            liveIds.push_back( entry.first );
    }
    // WP3: reconcile snapshot residue left by a crashed/killed PREVIOUS
    // process (and by reconfiguration): dead staging, parked dests,
    // upgrade backups, orphaned last-good trees. Runs AFTER the lock so a
    // bounded tree delete never holds the registry mutex. Same-pid
    // artifacts (a live capture/upgrade of this process) are never
    // touched — sweepPluginSnapshots owns that distinction.
    sweepPluginSnapshots( tempDirectory, liveIds );
    // Same reconcile for the PACKAGE staging root (<userRoot>/.staging): a
    // crashed swap that left the install dir missing is restored from its
    // parked .old backup here at startup rather than waiting for the next
    // install (which might never come).
    PluginPackage::reconcileStaging( PluginDiscovery::userPluginRoot() );
}

void PluginRegistry::refresh()
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    refreshUnlocked();
}

void PluginRegistry::refreshUnlocked()
{
    // Caller holds gRegistryMutex.
    mDiagnostics.clear();

    PluginDiscoveryOptions discoveryOptions;
    discoveryOptions.roots = mOptions.roots;
    std::vector<PluginRecord> scanned = PluginDiscovery::scan( discoveryOptions, mDiagnostics );

    for ( PluginRecord &record : scanned )
    {
        // Keep Loaded records untouched across refresh.
        if ( record.state == PluginState::Broken || record.state == PluginState::Discovered )
        {
            if ( const LoadedPlugin *loaded = findLoaded( mLoaded, record.id() ) )
            {
                (void)loaded;
                record.state = PluginState::Loaded;
                continue;
            }
        }
        if ( record.state != PluginState::Discovered )
            continue; // Broken (manifest parse/duplicate) stays Broken

        PluginValidationRequest request;
        request.pluginDir = record.directory;
        // ${temp}-rooted access declarations resolve against the configured
        // plugin temp directory; without this the validator failed closed
        // on every manifest that documented the pattern (baseline gap).
        request.tempDirectory = mOptions.tempDirectory;
        const bool valid =
            PluginManifestValidator::validate( record.manifest, request, record.diagnostics );
        if ( !valid )
        {
            record.state = incompatibleStateFor( record.id(), record.diagnostics );
            continue;
        }
        record.state = PluginState::Validated;
    }

    mRecords = std::move( scanned );
    applyPolicyAndIndex();
}

const PluginRecord *PluginRegistry::record( const std::string &pluginId ) const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    for ( const PluginRecord &entry : mRecords )
    {
        if ( entry.id() == pluginId )
            return &entry;
    }
    return nullptr;
}

Json::Value PluginRegistry::accessDeclarationFor( const std::string &pluginId ) const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    for ( const PluginRecord &candidate : mRecords )
    {
        if ( candidate.id() == pluginId )
            return candidate.manifest.access; // deep copy under the lock
    }
    return Json::Value();
}

std::string PluginRegistry::pluginDirectoryFor( const std::string &pluginId ) const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    for ( const PluginRecord &candidate : mRecords )
    {
        if ( candidate.id() == pluginId )
            return candidate.directory;
    }
    return {};
}

std::vector<PluginRecord> PluginRegistry::records() const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    return mRecords;
}

bool PluginRegistry::copyRecord( const std::string &pluginId, PluginRecord &out ) const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    for ( const PluginRecord &candidate : mRecords )
    {
        if ( candidate.id() == pluginId )
        {
            out = candidate;
            return true;
        }
    }
    return false;
}

PluginRecord *PluginRegistry::record( const std::string &pluginId )
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    for ( PluginRecord &entry : mRecords )
    {
        if ( entry.id() == pluginId )
            return &entry;
    }
    return nullptr;
}

std::vector<std::string> PluginRegistry::pluginIds() const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    std::vector<std::string> ids;
    ids.reserve( mRecords.size() );
    for ( const PluginRecord &entry : mRecords )
        ids.push_back( entry.id() );
    return ids;
}

void PluginRegistry::applyPolicyGate( PluginRecord &record )
{
    // Host-policy gate (track 13.0: extracted from applyPolicyAndIndex so
    // installOrUpgrade runs the IDENTICAL checks on a manifest that has no
    // registry record yet — the user-disabled index is deliberately NOT
    // part of the gate: disabling is an operator state, not a refusal).
    if ( record.state != PluginState::Validated )
        return;
    const std::string &id = record.id();

    const bool blocklisted =
        std::find( mOptions.policy.blockedPluginIds.begin(),
                   mOptions.policy.blockedPluginIds.end(), id )
        != mOptions.policy.blockedPluginIds.end();
    const bool notAllowed =
        !mOptions.policy.allowedPluginIds.empty()
        && std::find( mOptions.policy.allowedPluginIds.begin(),
                      mOptions.policy.allowedPluginIds.end(), id )
               == mOptions.policy.allowedPluginIds.end();
    if ( blocklisted || notAllowed )
    {
        record.state = PluginState::Blocked;
        record.diagnostics.add(
            PluginDiagnosticCode::PolicyBlocklisted, PluginDiagnosticSeverity::Error,
            blocklisted ? "plugin is on the host block list (SICNU_PLUGIN_BLOCK)"
                        : "plugin is not on the host allow list (SICNU_PLUGIN_ALLOW)",
            id );
        return;
    }

    const bool nativeThirdParty =
        record.manifest.entrypointKind == PluginEntrypointKind::Native
        && record.origin != PluginOrigin::Builtin;
    if ( nativeThirdParty && !mOptions.policy.allowThirdPartyNative )
    {
        record.state = PluginState::Blocked;
        record.diagnostics.add(
            PluginDiagnosticCode::TrustRejected, PluginDiagnosticSeverity::Error,
            "third-party native plugins are disabled "
            "(SICNU_PLUGIN_DISABLE_NATIVE_THIRD_PARTY=1)",
            id );
        return;
    }

    if ( mOptions.policy.mode == PluginPolicyMode::Enforce )
    {
        for ( const std::string &capability : record.manifest.capabilities )
        {
            for ( PluginPermission permission :
                  requiredPermissionsForCapability( capability ) )
            {
                if ( std::find( record.manifest.permissions.begin(),
                                record.manifest.permissions.end(), permission )
                     == record.manifest.permissions.end() )
                {
                    record.state = PluginState::Blocked;
                    record.diagnostics.add(
                        PluginDiagnosticCode::PermissionDenied,
                        PluginDiagnosticSeverity::Error,
                        "enforce policy: capability '" + capability
                            + "' requires undeclared permission '"
                            + pluginPermissionName( permission ) + "'",
                        id );
                    break;
                }
            }
            if ( record.state == PluginState::Blocked )
                break;
        }
    }
}

void PluginRegistry::applyPolicyAndIndex()
{
    for ( PluginRecord &record : mRecords )
    {
        if ( record.state != PluginState::Validated )
            continue;
        const std::string &id = record.id();
        applyPolicyGate( record );
        if ( record.state != PluginState::Validated )
            continue;

        if ( std::find( mDisabledIds.begin(), mDisabledIds.end(), id ) != mDisabledIds.end() )
        {
            record.state = PluginState::Disabled;
            record.diagnostics.add( PluginDiagnosticCode::PluginDisabled,
                                    PluginDiagnosticSeverity::Info,
                                    "plugin is disabled by the user", id );
        }
    }
}

void PluginRegistry::auditGrantedPermissions( const std::string &pluginId )
{
    // One Info audit event per declared permission, recorded when the plugin
    // ends up Loaded. The whole body runs under the registry mutex: it reads
    // mOptions.policy and appends to mDiagnostics, both shared with every
    // other load/unload/refresh path (PluginDiagnosticLog::add is an
    // unsynchronized push_back). copyRecord() is re-entrant under this
    // recursive mutex, and the caller must NOT hold the lock across the
    // contribution-sink callbacks that follow it.
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    PluginRecord snapshot;
    if ( !copyRecord( pluginId, snapshot ) )
        return;
    const char *mode = mOptions.policy.mode == PluginPolicyMode::Enforce ? "enforce" : "audit";
    for ( PluginPermission permission : snapshot.manifest.permissions )
    {
        mDiagnostics.add( PluginDiagnosticCode::PermissionGranted,
                          PluginDiagnosticSeverity::Info,
                          std::string( "permission '" ) + pluginPermissionName( permission )
                              + "' granted under policy mode '" + mode + "'",
                          pluginId, "permissions" );
    }
}

std::string PluginRegistry::snapshotRoot() const
{
    // mOptions is shared state: read it under the registry mutex (callers of
    // this const helper are reload/upgrade paths that do not hold it).
    // pluginSnapshotRoot owns the temp-dir resolution — the error_code
    // overload + /tmp fallback, never the throwing one.
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    return pluginSnapshotRoot( mOptions.tempDirectory );
}

std::string PluginRegistry::lastGoodSnapshotPath( const std::string &pluginId ) const
{
    // Path computation is unconditional: reload() consults it, the sweep
    // reconciles it, and uninstallPlugin() removes it. The devMode policy
    // gate lives in refreshLastGoodSnapshot (production captures nothing).
    return snapshotRoot() + "/last-good-" + pluginId;
}

std::string PluginRegistry::upgradeSnapshotPath( const std::string &pluginId ) const
{
    // Per-upgrade rollback copy of the CURRENT install, named so
    // sweepPluginSnapshots can attribute it to this process and reclaim it
    // as crash residue on the next configure() (upgrade-<id>-<pid>).
    return snapshotRoot() + "/upgrade-" + pluginId + "-"
           + std::to_string( snapshotOwnerPid() );
}

void PluginRegistry::refreshLastGoodSnapshot( const std::string &pluginId,
                                              const std::string &pluginDir )
{
    // Dev mode only: production keeps no second copy (install-time upgrades
    // snapshot their own way via upgradeSnapshotPath). ASYNC + bounded: a
    // large/slow plugin must not stall the load() publish path — the copy
    // only has to exist before a LATER reload() consults it, and reload()
    // awaits this job (bounded) + verifies the completeness marker.
    std::shared_ptr<PluginSnapshotJob> previous;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        if ( !mOptions.policy.devMode )
            return;
        auto job = PluginSnapshotJob::start( pluginDir,
                                             lastGoodSnapshotPath( pluginId ), pluginId,
                                             PluginSnapshotBudget::fromEnvironment() );
        // Supersede an in-flight capture for this plugin: the newer bytes
        // are last-known-good by definition. The dropped job is cancelled +
        // joined OUTSIDE the lock below — joining under the registry mutex
        // would hold it across the worker's current file.
        auto &slot = mSnapshotJobs[ pluginId ];
        previous = std::move( slot );
        slot = std::move( job );
    }
    if ( previous )
        previous->cancel(); // dtor joins the worker
}

bool PluginRegistry::waitForSnapshotJob( const std::string &pluginId, int timeoutMs )
{
    std::shared_ptr<PluginSnapshotJob> job;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        const auto it = mSnapshotJobs.find( pluginId );
        if ( it == mSnapshotJobs.end() )
            return true; // nothing in flight
        job = it->second;
    }
    if ( !job->wait( timeoutMs ) )
    {
        // A straggler past its deadline must not publish stale bytes
        // later: cancel it (the worker exits at its next file boundary and
        // the shared_ptr dtor joins). The slot is released only when it
        // still points at THIS job — a newer capture may have superseded
        // it while we waited, and that one is not ours to cancel.
        {
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            const auto it = mSnapshotJobs.find( pluginId );
            if ( it != mSnapshotJobs.end() && it->second == job )
                mSnapshotJobs.erase( it );
        }
        job->cancel();
        return false;
    }
    const PluginSnapshotResult outcome = job->result();
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        // A newer capture may have superseded this job while we waited:
        // only erase the slot if it still points at THIS job.
        const auto it = mSnapshotJobs.find( pluginId );
        if ( it != mSnapshotJobs.end() && it->second == job )
            mSnapshotJobs.erase( it );
        if ( outcome.status != PluginSnapshotStatus::Ok )
        {
            const char *what =
                outcome.status == PluginSnapshotStatus::Cancelled     ? "cancelled"
                : outcome.status == PluginSnapshotStatus::BudgetExceeded ? "budget exceeded"
                : outcome.status == PluginSnapshotStatus::Unsafe      ? "unsafe entry refused"
                                                                      : "copy failed";
            mDiagnostics.add( PluginDiagnosticCode::ResourceMissing,
                              PluginDiagnosticSeverity::Warning,
                              std::string( "last-good snapshot capture " ) + what + ": "
                                  + outcome.message,
                              pluginId );
        }
    }
    return true;
}

PluginRegistry::LifecycleOwnerGuard::~LifecycleOwnerGuard()
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    registry->mReloading.erase( pluginId );
}

std::shared_ptr<PluginSnapshotJob> PluginRegistry::takePendingSnapshotJob(
    const std::string &pluginId )
{
    std::shared_ptr<PluginSnapshotJob> pending;
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    const auto it = mSnapshotJobs.find( pluginId );
    if ( it != mSnapshotJobs.end() )
    {
        pending = std::move( it->second );
        mSnapshotJobs.erase( it );
    }
    return pending;
}

void PluginRegistry::cancelSnapshotJobs()
{
    std::vector<std::shared_ptr<PluginSnapshotJob>> jobs;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        for ( auto &entry : mSnapshotJobs )
            jobs.push_back( std::move( entry.second ) );
        mSnapshotJobs.clear();
    }
    // Cancel + join outside the registry lock: a worker finishing its
    // current file must not serialize against unrelated registry traffic.
    for ( const auto &job : jobs )
        job->cancel(); // dtor joins
}

bool PluginRegistry::load( const std::string &pluginId )
{
    // Lock-drop protocol (issue #928), mirroring unload(): mark Loading,
    // copy what the runtime needs, drop gRegistryMutex across spawn /
    // plugin.load / dlopen, re-acquire to publish, then talk to the sink
    // WITHOUT the registry lock (pluginLoaded takes PluginRuntimeHost::mMutex;
    // bootstrap holds that mutex then configure() wants gRegistryMutex).
    enum class Kind
    {
        Manifest,
        HostProcess,
        InProcess,
    };

    Kind kind = Kind::Manifest;
    PluginRecord snapshot;
    HostProcessRuntime *runtime = nullptr;
    PluginContributionSink *sink = nullptr;
    HostServicesV1 *services = nullptr;

    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        PluginRecord *entry = record( pluginId );
        if ( !entry )
        {
            mDiagnostics.add( PluginDiagnosticCode::EntrypointMissing,
                              PluginDiagnosticSeverity::Error, "unknown plugin", pluginId );
            return false;
        }
        // A lifecycle operation (reload/upgrade) owned by another thread
        // holds this plugin: a concurrent load could publish the wrong
        // generation inside the transaction's unload/load window.
        const auto ownerIt = mReloading.find( pluginId );
        if ( ownerIt != mReloading.end()
             && ownerIt->second != std::this_thread::get_id() )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                              PluginDiagnosticSeverity::Error,
                              "load refused: a lifecycle operation is in progress "
                              "for this plugin",
                              pluginId );
            return false;
        }
        if ( entry->state == PluginState::Loaded )
            return true;
        if ( entry->state == PluginState::Loading )
            return false;
        if ( !entry->loadable() )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected, PluginDiagnosticSeverity::Error,
                              "plugin is not loadable in state "
                                  + std::string( pluginStateName( entry->state ) ),
                              pluginId );
            return false;
        }
        if ( std::find( mDisabledIds.begin(), mDisabledIds.end(), pluginId )
             != mDisabledIds.end() )
        {
            mDiagnostics.add( PluginDiagnosticCode::PluginDisabled,
                              PluginDiagnosticSeverity::Error, "plugin is disabled by the user",
                              pluginId );
            return false;
        }
        if ( entry->manifest.entrypointKind == PluginEntrypointKind::Python )
        {
            // Python plugins are hosted by the Python worker host (PluginHost /
            // PythonPluginHost, metadata.txt + classFactory), which owns the
            // out-of-process pool. The manifest is the discovery/doctor index;
            // pretending to "load" here would hide the real hosting path.
            mDiagnostics.add( PluginDiagnosticCode::EntrypointMissing,
                              PluginDiagnosticSeverity::Warning,
                              "python plugins are hosted by the Python worker host "
                              "(PluginHost); registry load is a no-op",
                              pluginId );
            entry->state = PluginState::Validated;
            return false;
        }
        if ( entry->manifest.entrypointKind == PluginEntrypointKind::Manifest )
        {
            // Manifest-kind plugins: every contribution is pure-manifest
            // (external tools); nothing to dlopen. Publish under the lock,
            // then pluginLoaded AFTER dropping it (#755 restore + #928).
            LoadedPlugin hosted;
            hosted.pluginId = pluginId;
            mLoaded.push_back( std::move( hosted ) );
            entry->state = PluginState::Loaded;
            sink = mSink;
        }
        else if ( entry->manifest.runtime == PluginRuntimeKind::HostProcess )
        {
            if ( !mHostProcessRuntime )
            {
                mDiagnostics.add( PluginDiagnosticCode::HostProcessUnavailable,
                                  PluginDiagnosticSeverity::Error,
                                  "manifest requests runtime 'host-process' but no "
                                  "host-process runtime is installed in this process",
                                  pluginId );
                entry->state = PluginState::Failed;
                return false;
            }
            if ( !mSink )
            {
                mDiagnostics.add( PluginDiagnosticCode::RegistrationFailed,
                                  PluginDiagnosticSeverity::Error,
                                  "no contribution sink installed", pluginId );
                return false;
            }
            entry->state = PluginState::Loading;
            snapshot = *entry;
            runtime = mHostProcessRuntime;
            sink = mSink;
            services = mServices.get();
            kind = Kind::HostProcess;
        }
        else
        {
            if ( !mSink )
            {
                mDiagnostics.add( PluginDiagnosticCode::RegistrationFailed,
                                  PluginDiagnosticSeverity::Error,
                                  "no contribution sink installed", pluginId );
                return false;
            }
            entry->state = PluginState::Loading;
            snapshot = *entry;
            sink = mSink;
            services = mServices.get();
            kind = Kind::InProcess;
        }
    }

    if ( kind == Kind::Manifest )
    {
        auditGrantedPermissions( pluginId );
        refreshLastGoodSnapshot( pluginId, pluginDirectoryFor( pluginId ) );
        if ( sink )
            sink->pluginLoaded( pluginId );
        return true;
    }

    bool ok = false;
    LoadedPlugin hosted;
    PluginDiagnosticLog localLog;
    PluginLoader localLoader;
    if ( kind == Kind::HostProcess )
        ok = runtime->loadPlugin( snapshot, *services, *sink, localLog );
    else
    {
        ok = localLoader.load( snapshot, *services, *sink, localLog );
        if ( ok )
            hosted = localLoader.take();
    }

    bool publish = false;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        mDiagnostics.merge( localLog );
        PluginRecord *entry = record( pluginId );
        const bool stillLoading = entry && entry->state == PluginState::Loading;
        if ( stillLoading )
        {
            if ( ok )
            {
                if ( kind == Kind::HostProcess )
                    mHostProcessLoaded.push_back( pluginId );
                else
                    mLoaded.push_back( std::move( hosted ) );
                entry->state = PluginState::Loaded;
                publish = true;
            }
            else
            {
                entry->state = PluginState::Failed;
            }
        }
    }

    if ( publish )
    {
        auditGrantedPermissions( pluginId );
        refreshLastGoodSnapshot( pluginId, pluginDirectoryFor( pluginId ) );
        if ( sink )
            sink->pluginLoaded( pluginId );
        // Unload may have won between publish and pluginLoaded: drop any
        // contributions the host just reinstalled for a plugin that is gone.
        if ( !isLoaded( pluginId ) && sink )
            sink->revokePlugin( pluginId );
        return isLoaded( pluginId );
    }

    if ( sink )
        sink->revokePlugin( pluginId );
    if ( ok )
    {
        PluginDiagnosticLog teardownLog;
        if ( kind == Kind::HostProcess && runtime )
            runtime->unloadPlugin( pluginId, teardownLog );
        else if ( kind == Kind::InProcess )
            localLoader.unload( hosted, teardownLog );
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        mDiagnostics.merge( teardownLog );
    }
    return false;
}

std::vector<std::string> PluginRegistry::loadAllValidated()
{
    std::vector<std::string> candidates;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        for ( const PluginRecord &entry : mRecords )
        {
            if ( entry.state == PluginState::Validated )
                candidates.push_back( entry.id() );
        }
    }
    std::vector<std::string> loadedIds;
    for ( const std::string &id : candidates )
    {
        if ( load( id ) )
            loadedIds.push_back( id );
    }
    return loadedIds;
}

std::vector<std::string> PluginRegistry::loadedPluginIds() const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    std::vector<std::string> ids;
    for ( const LoadedPlugin &entry : mLoaded )
        ids.push_back( entry.pluginId );
    return ids;
}

const LoadedPlugin *PluginRegistry::loaded( const std::string &pluginId ) const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    return findLoaded( mLoaded, pluginId );
}

bool PluginRegistry::unload( const std::string &pluginId, int timeoutMs )
{
    // Host-process plugins (isolation runtime 5.0) follow the SAME
    // barrier-protected sequence, but the teardown step delegates to the
    // runtime (shutdown request, kill ladder, session close) instead of
    // dlclose. No plugin code is ever mapped here, so the UAF window the
    // in-process ordering guards against cannot exist; the barrier still
    // guards against contributions outliving their session.
    bool hostedOop = false;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        // A lifecycle operation (reload/upgrade) owned by another thread
        // holds this plugin: a foreign unload would tear down the
        // generation the transaction is orchestrating.
        const auto ownerIt = mReloading.find( pluginId );
        if ( ownerIt != mReloading.end()
             && ownerIt->second != std::this_thread::get_id() )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                              PluginDiagnosticSeverity::Error,
                              "unload refused: a lifecycle operation is in progress "
                              "for this plugin",
                              pluginId );
            return false;
        }
        hostedOop = std::find( mHostProcessLoaded.begin(), mHostProcessLoaded.end(), pluginId )
                        != mHostProcessLoaded.end()
                    && std::none_of( mLoaded.begin(), mLoaded.end(),
                                     [&]( const LoadedPlugin &e ) {
                                         return e.pluginId == pluginId;
                                     } );
    }
    if ( hostedOop )
    {
        {
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            if ( PluginRecord *entry = record( pluginId ) )
                entry->state = PluginState::Quiescing;
        }
        const int budget = timeoutMs > 0 ? timeoutMs : unloadTimeoutMs();
        if ( mSink )
            mSink->beginPluginDrain( pluginId );
        const bool idle = mSink ? mSink->waitPluginIdle( pluginId, budget ) : true;
        if ( !idle )
        {
            if ( mSink )
                mSink->cancelPluginDrain( pluginId );
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            if ( PluginRecord *entry = record( pluginId ) )
            {
                if ( entry->state == PluginState::Quiescing )
                    entry->state = PluginState::Loaded;
            }
            mDiagnostics.add( PluginDiagnosticCode::PluginInUse,
                              PluginDiagnosticSeverity::Error,
                              "unload refused: plugin is in use (execution still active after "
                                  + std::to_string( budget )
                                  + " ms); stop the running task first",
                              pluginId );
            return false;
        }
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        if ( mSink && !gDestructing )
            mSink->revokePlugin( pluginId );
        if ( mHostProcessRuntime && !gDestructing )
        {
            if ( !mHostProcessRuntime->unloadPlugin( pluginId, mDiagnostics ) )
                mDiagnostics.add( PluginDiagnosticCode::LibraryLoadFailed,
                                  PluginDiagnosticSeverity::Warning,
                                  "host-process worker did not shut down cleanly; "
                                  "process was killed",
                                  pluginId );
        }
        mHostProcessLoaded.erase(
            std::remove( mHostProcessLoaded.begin(), mHostProcessLoaded.end(), pluginId ),
            mHostProcessLoaded.end() );
        if ( PluginRecord *entry = record( pluginId ) )
            entry->state = PluginState::Unloaded;
        return true;
    }

    // Unload sequence (issue #747), in order:
    //   1. arm the drain (new dispatch refused) and mark the record Quiescing;
    //   2. wait bounded for in-flight executions — refusing (state restored)
    //      on timeout; the registry lock is NOT held while waiting, because
    //      draining executors may still call back into the registry;
    //   3. revoke host-side contributions (UI release, operator/tool/model/
    //      data-provider unregistration) while the code is still mapped;
    //   4. shutdown + delete the plugin instance, then dlclose LAST.
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        auto iterator = std::find_if( mLoaded.begin(), mLoaded.end(),
                                      [&]( const LoadedPlugin &entry ) {
                                          return entry.pluginId == pluginId;
                                      } );
        if ( iterator == mLoaded.end() )
        {
            // In-flight load() dropped the lock after marking Loading: abort
            // so the loader will not publish Loaded on re-acquire (#928).
            if ( PluginRecord *entry = record( pluginId ) )
            {
                if ( entry->state == PluginState::Loading )
                    entry->state = PluginState::Unloaded;
            }
            return false;
        }
        if ( PluginRecord *entry = record( pluginId ) )
            entry->state = PluginState::Quiescing;
    }

    const int budget = timeoutMs > 0 ? timeoutMs : unloadTimeoutMs();
    if ( mSink )
        mSink->beginPluginDrain( pluginId );
    const bool idle = mSink ? mSink->waitPluginIdle( pluginId, budget ) : true;
    if ( !idle )
    {
        if ( mSink )
            mSink->cancelPluginDrain( pluginId );
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        // A concurrent unloader may have completed the unload while we
        // waited: only restore when the entry is still loaded.
        const bool stillLoaded =
            std::find_if( mLoaded.begin(), mLoaded.end(), [&]( const LoadedPlugin &e ) {
                return e.pluginId == pluginId;
            } ) != mLoaded.end();
        if ( stillLoaded )
        {
            if ( PluginRecord *entry = record( pluginId ) )
            {
                if ( entry->state == PluginState::Quiescing )
                    entry->state = PluginState::Loaded;
            }
        }
        mDiagnostics.add( PluginDiagnosticCode::PluginInUse, PluginDiagnosticSeverity::Error,
                          "unload refused: plugin is in use (execution still active after "
                              + std::to_string( budget ) + " ms); stop the running task first",
                          pluginId );
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    auto iterator = std::find_if( mLoaded.begin(), mLoaded.end(),
                                  [&]( const LoadedPlugin &entry ) {
                                      return entry.pluginId == pluginId;
                                  } );
    if ( iterator == mLoaded.end() )
        return true; // drained, then unloaded by a concurrent caller
    // Revoke host-side contributions BEFORE dlclose: std::function targets,
    // executors and providers created by the plugin must be released while
    // its code is still mapped. The host closes its barrier entry here so
    // stale adapters fail with a typed refusal instead of calling into
    // unmapped code.
    if ( mSink )
        mSink->revokePlugin( pluginId );
    if ( !mLoader )
        mLoader = std::make_unique<PluginLoader>();
    mLoader->unload( *iterator, mDiagnostics );
    mLoaded.erase( iterator );
    if ( PluginRecord *entry = record( pluginId ) )
        entry->state = PluginState::Unloaded;
    return true;
}

bool PluginRegistry::reload( const std::string &pluginId, const ReloadOptions &options )
{
    // Every diagnostic this function records goes through addDiagnostic():
    // PluginDiagnosticLog::add is an unsynchronized push_back, and the rest
    // of the registry mutates mDiagnostics under gRegistryMutex, so an
    // unlocked append here would race with a concurrent load/unload/refresh
    // (dev-mode reloads run on the GUI thread by design).
    const auto addDiagnostic = [this, &pluginId]( PluginDiagnosticCode code,
                                                  PluginDiagnosticSeverity severity,
                                                  const std::string &message,
                                                  const std::string &field = std::string() ) {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        mDiagnostics.add( code, severity, message, pluginId, field );
    };

    // WP4 gate (dev mode only): the caller's flag can only LOWER authority,
    // never raise it - both the request and the host policy must allow it, so
    // production (devMode=false everywhere) can never reach the reload path
    // even through a hostile caller that hardcodes devMode=true.
    bool devMode = false;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        devMode = options.devMode && mOptions.policy.devMode;
    }
    if ( !devMode )
    {
        bool callerAsked = options.devMode;
        bool policyAllows = false;
        {
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            policyAllows = mOptions.policy.devMode;
        }
        addDiagnostic( PluginDiagnosticCode::TrustRejected, PluginDiagnosticSeverity::Error,
                       "hot reload refused: dev mode is off (caller devMode="
                           + std::string( callerAsked ? "true" : "false" )
                           + ", host policy devMode="
                           + std::string( policyAllows ? "true" : "false" )
                           + "; SICNU_PLUGIN_DEV=1 enables it)" );
        return false;
    }

    // One reload per plugin at a time: a second concurrent reload would race
    // the first's unload/load window and could publish the WRONG generation.
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        if ( mReloading.count( pluginId ) != 0 )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                              PluginDiagnosticSeverity::Error,
                              "hot reload refused: another reload of this plugin is in progress",
                              pluginId );
            return false;
        }
        mReloading.emplace( pluginId, std::this_thread::get_id() );
    }
    // Scope guard so every early return below clears the in-flight marker.
    LifecycleOwnerGuard reloadGuard{ this, pluginId };

    std::string pluginDir;
    bool loaded = false;
    bool hostedOop = false;
    PluginManifest loadedManifest;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        const PluginRecord *entry = record( pluginId );
        if ( !entry )
        {
            mDiagnostics.add( PluginDiagnosticCode::EntrypointMissing,
                              PluginDiagnosticSeverity::Error, "unknown plugin", pluginId );
            return false;
        }
        pluginDir = entry->directory;
        loadedManifest = entry->manifest;
        loaded = std::find_if( mLoaded.begin(), mLoaded.end(), [&]( const LoadedPlugin &e ) {
                     return e.pluginId == pluginId;
                 } ) != mLoaded.end();
        hostedOop = std::find( mHostProcessLoaded.begin(), mHostProcessLoaded.end(), pluginId )
                    != mHostProcessLoaded.end();
        if ( !loaded && !hostedOop )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                              PluginDiagnosticSeverity::Error,
                              "hot reload refused: plugin is not loaded "
                                  "(state "
                                  + std::string( pluginStateName( entry->state ) ) + ")",
                              pluginId );
            return false;
        }
    }

    // Step 1: validate the package on disk WITHOUT touching mapped code. A
    // broken new manifest refuses the reload and the old version stays loaded.
    {
        PluginManifest fresh;
        PluginDiagnostic parseError;
        if ( !loadManifestFromFile( pluginDir + "/plugin.json", fresh, parseError )
             || fresh.id != pluginId )
        {
            if ( parseError.code != PluginDiagnosticCode::None )
                addDiagnostic( parseError.code, PluginDiagnosticSeverity::Error,
                               "hot reload refused: " + parseError.message, parseError.field );
            else
                addDiagnostic( PluginDiagnosticCode::ManifestInvalidField,
                               PluginDiagnosticSeverity::Error,
                               "hot reload refused: manifest id no longer matches '" + pluginId
                                   + "'",
                               "id" );
            return false;
        }
        PluginValidationRequest request;
        request.pluginDir = pluginDir;
        request.tempDirectory = mOptions.tempDirectory;
        PluginDiagnosticLog freshLog;
        if ( !PluginManifestValidator::validate( fresh, request, freshLog )
             || freshLog.hasErrorsFor( pluginId ) )
        {
            {
                std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
                mDiagnostics.merge( freshLog );
            }
            addDiagnostic( PluginDiagnosticCode::TrustRejected, PluginDiagnosticSeverity::Error,
                           "hot reload refused: the new manifest does not validate; "
                               "the running version stays loaded",
                           "plugin.json" );
            return false;
        }
    }

    // Step 2: the rollback source is the LAST-KNOWN-GOOD snapshot - the
    // bytes as they were when this plugin last loaded successfully (dev mode
    // keeps one per plugin). A snapshot taken HERE would capture the dev's
    // in-place edit, i.e. the version that is about to fail, and could never
    // restore anything useful. No snapshot (never loaded under dev mode)
    // means no rollback, reported honestly.
    //
    // Track 13.0: captures are ASYNC now, so a capture started by the most
    // recent load() may still be in flight — wait for it bounded first
    // (SICNU_PLUGIN_SNAPSHOT_WAIT_MS, default 30 s; the copy is itself
    // budget-bounded, so a wait that expires means something is genuinely
    // stuck, in which case the honest answer is "no usable snapshot").
    waitForSnapshotJob( pluginId, snapshotWaitMs() );
    const std::string snapshotDir = lastGoodSnapshotPath( pluginId );
    bool haveSnapshot = false;
    if ( !std::filesystem::exists( snapshotDir ) )
    {
        addDiagnostic( PluginDiagnosticCode::ResourceMissing, PluginDiagnosticSeverity::Warning,
                       "hot reload has no last-known-good snapshot; a failed reload "
                           "will leave the plugin unloaded instead of rolled back" );
    }
    else
    {
        // Trust a snapshot only when it is COMPLETE and verified: the
        // marker is written last during capture, and a bounded re-walk
        // must reproduce its declared file/byte counts. A partial copy
        // (killed mid-write, disk full, tampered payload) fails closed —
        // restoring it would destroy the working bytes it protects.
        std::string verifyError;
        if ( !verifyPluginSnapshot( snapshotDir, pluginId, verifyError ) )
        {
            addDiagnostic( PluginDiagnosticCode::ResourceMissing,
                           PluginDiagnosticSeverity::Warning,
                           "hot reload found an unusable last-known-good snapshot ("
                               + verifyError + "); no rollback is possible" );
        }
        else
        {
            // Track 13.0: the capture runs ASYNC against the live plugin
            // dir, so a dev edit that landed while the worker was still
            // walking would be packaged as a COMPLETE, marker-valid
            // snapshot — of the WRONG bytes. Identity gate: the snapshot's
            // manifest must still be the one this registry loaded; a
            // mismatch means a torn capture, never last-known-good.
            // (A non-manifest file edited mid-capture can still slip
            // through — the restore's own load() re-validates it.)
            PluginManifest snapManifest;
            PluginDiagnostic snapParse;
            if ( !loadManifestFromFile( snapshotDir + "/plugin.json", snapManifest,
                                        snapParse )
                 || snapManifest.id != loadedManifest.id
                 || snapManifest.version != loadedManifest.version
                 || snapManifest.entrypoint != loadedManifest.entrypoint )
            {
                addDiagnostic( PluginDiagnosticCode::ResourceMissing,
                               PluginDiagnosticSeverity::Warning,
                               "hot reload's last-known-good snapshot does not match "
                                   "the loaded version (captured during an edit?); "
                                   "no rollback is possible" );
            }
            else
            {
                haveSnapshot = true;
            }
        }
    }

    // Step 3: state migration runs while the OLD code is still loaded, so a
    // failed migration aborts the reload with nothing changed.
    if ( options.migrateState && !options.migrateState( pluginDir ) )
    {
        addDiagnostic( PluginDiagnosticCode::InitializationFailed,
                       PluginDiagnosticSeverity::Error,
                       "hot reload refused: state migration failed; "
                           "the running version stays loaded" );
        return false;
    }

    // Step 4: drain + unload (barrier-protected; a busy plugin is refused and
    // stays loaded), then load the new bytes. A refused unload is reported:
    // silently returning false here would leave the caller with no reason.
    if ( !unload( pluginId ) )
    {
        addDiagnostic( PluginDiagnosticCode::PluginInUse, PluginDiagnosticSeverity::Error,
                       "hot reload aborted: the plugin could not be unloaded "
                           "(still executing?); the running version stays loaded" );
        return false;
    }
    refresh();
    bool ok = false;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        const PluginRecord *entry = record( pluginId );
        ok = entry
             && ( entry->state == PluginState::Validated || entry->state == PluginState::Unloaded
                  || entry->state == PluginState::Loaded );
    }
    if ( ok )
        ok = load( pluginId );
    if ( ok )
        return true;

    // refreshUnlocked() clears the diagnostic log, which would erase the
    // evidence for WHY the new version failed (E4002 from load() above).
    // Capture it HERE - after the failure - and re-add it across every
    // refresh the rollback performs, so a reload that reports false still
    // explains itself in diagnostics(). Only THIS plugin's records are kept:
    // the pre-reload log may hold stale entries from earlier passes.
    std::vector<PluginDiagnostic> reloadEvidence;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        for ( const PluginDiagnostic &item : mDiagnostics.items() )
        {
            if ( item.pluginId == pluginId )
                reloadEvidence.push_back( item );
        }
    }
    const auto preserveEvidence = [this, &reloadEvidence]() {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        for ( const PluginDiagnostic &item : reloadEvidence )
            mDiagnostics.add( item );
    };

    // Step 5: the new version failed to load - restore the last-known-good
    // bytes and load those instead. The reload itself still reports false:
    // success means the NEW version is running, never a silent downgrade.
    // Without a usable snapshot the plugin simply stays unloaded (typed),
    // which is the honest outcome for a dev edit that cannot be recovered.
    addDiagnostic( PluginDiagnosticCode::RegistrationFailed,
                   PluginDiagnosticSeverity::Warning,
                   "hot reload failed to load the new version" );
    if ( haveSnapshot )
    {
        std::string restoreError;
        if ( restorePluginSnapshot( snapshotDir, pluginDir, pluginId, restoreError ) )
        {
            refresh();
            preserveEvidence();
            if ( load( pluginId ) )
            {
                addDiagnostic( PluginDiagnosticCode::PluginReloadRolledBack,
                               PluginDiagnosticSeverity::Warning,
                               "hot reload rolled back to the previous version (E4006); "
                                   "fix the plugin and reload again" );
                return false;
            }
        }
        else
        {
            addDiagnostic( PluginDiagnosticCode::ResourceMissing,
                           PluginDiagnosticSeverity::Error,
                           "hot reload rollback could not restore the snapshot: "
                               + restoreError );
        }
    }
    addDiagnostic( PluginDiagnosticCode::InitializationFailed, PluginDiagnosticSeverity::Error,
                   "hot reload failed and the rollback did not restore a working version; "
                       "the plugin stays failed until it is fixed" );
    return false;
}

PluginRegistry::PluginUpgradeResult PluginRegistry::installOrUpgrade(
    const std::string &sourceDir, const PluginUpgradeOptions &options )
{
    namespace fs = std::filesystem;
    PluginUpgradeResult result;

    // Every diagnostic this function records goes through addDiagnostic()
    // (same concurrency rule as reload(): mDiagnostics mutates under
    // gRegistryMutex everywhere else).
    const auto addDiagnostic = [this]( PluginDiagnosticCode code,
                                       PluginDiagnosticSeverity severity,
                                       const std::string &message,
                                       const std::string &pluginId = std::string(),
                                       const std::string &field = std::string() ) {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        mDiagnostics.add( code, severity, message, pluginId, field );
    };
    const auto mergeLog = [this]( PluginDiagnosticLog &log ) {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        mDiagnostics.merge( log );
    };

    // Step 1: stage-validate the SOURCE package before anything about the
    // existing install is touched — a bad manifest never reaches the drain.
    PluginManifest fresh;
    PluginDiagnostic parseError;
    if ( !loadManifestFromFile( sourceDir + "/plugin.json", fresh, parseError ) )
    {
        result.pluginId = fresh.id;
        if ( parseError.code != PluginDiagnosticCode::None )
            addDiagnostic( parseError.code, PluginDiagnosticSeverity::Error,
                           "install/upgrade refused: " + parseError.message,
                           fresh.id, parseError.field );
        else
            addDiagnostic( PluginDiagnosticCode::ManifestInvalidField,
                           PluginDiagnosticSeverity::Error,
                           "install/upgrade refused: manifest does not parse",
                           fresh.id, "plugin.json" );
        result.status = PluginUpgradeStatus::Refused;
        return result;
    }
    result.pluginId = fresh.id;
    if ( !PluginManifestValidator::isValidPluginId( fresh.id ) )
    {
        addDiagnostic( PluginDiagnosticCode::TrustRejected,
                       PluginDiagnosticSeverity::Error,
                       "install/upgrade refused: '" + fresh.id
                           + "' is not a valid plugin id",
                       fresh.id, "id" );
        result.status = PluginUpgradeStatus::Refused;
        return result;
    }
    {
        std::string tempDirectory;
        {
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            tempDirectory = mOptions.tempDirectory;
        }
        PluginValidationRequest request;
        request.pluginDir = sourceDir;
        request.tempDirectory = tempDirectory;
        PluginDiagnosticLog freshLog;
        if ( !PluginManifestValidator::validate( fresh, request, freshLog )
             || freshLog.hasErrorsFor( fresh.id ) )
        {
            mergeLog( freshLog );
            addDiagnostic( PluginDiagnosticCode::TrustRejected,
                           PluginDiagnosticSeverity::Error,
                           "install/upgrade refused: the new manifest does not "
                           "validate; the existing install is untouched",
                           fresh.id, "plugin.json" );
            result.status = PluginUpgradeStatus::Refused;
            return result;
        }
    }

    // Step 2: host-policy gate on the NEW manifest — the IDENTICAL checks
    // applyPolicyAndIndex runs, applied to a probe record (the manifest has
    // no registry record yet). A blocked/incompatible new version refuses
    // the upgrade before any drain or swap.
    {
        PluginRecord probe;
        probe.manifest = fresh;
        probe.directory = sourceDir;
        probe.manifestPath = sourceDir + "/plugin.json";
        probe.origin = PluginOrigin::User;
        probe.state = PluginState::Validated;
        {
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            applyPolicyGate( probe );
            if ( probe.state != PluginState::Validated )
                mDiagnostics.merge( probe.diagnostics );
        }
        if ( probe.state != PluginState::Validated )
        {
            addDiagnostic( PluginDiagnosticCode::TrustRejected,
                           PluginDiagnosticSeverity::Error,
                           "install/upgrade refused: the new version fails the "
                           "host policy gate; the existing install is untouched",
                           fresh.id );
            result.status = PluginUpgradeStatus::Refused;
            return result;
        }
    }

    // Step 3: is there an existing install of THIS id in the user root?
    const std::string userRoot = PluginDiscovery::userPluginRoot();
    const std::string target = userRoot + "/" + fresh.id;
    PluginManifest previous;
    PluginDiagnostic previousError;
    const bool hadPrevious =
        loadManifestFromFile( target + "/plugin.json", previous, previousError )
        && previous.id == fresh.id;

    // Step 4: one lifecycle operation per plugin at a time (the same guard
    // reload() takes — a concurrent reload/upgrade could otherwise publish
    // the wrong generation). Covers the fresh-install branch too: a foreign
    // load() must not publish a different-root copy while the install lands.
    const std::string &id = fresh.id;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        if ( mReloading.count( id ) != 0 )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                              PluginDiagnosticSeverity::Error,
                              "install/upgrade refused: another lifecycle "
                              "operation is in progress for this plugin",
                              id );
            result.status = PluginUpgradeStatus::Refused;
            return result;
        }
        mReloading.emplace( id, std::this_thread::get_id() );
    }
    LifecycleOwnerGuard lifecycleGuard{ this, id };

    // A load() that passed the owner check before this op armed is still
    // in flight (state Loading, not yet in mLoaded): isLoaded() is false
    // for it, so without this fence the swap/delete below would run under
    // an in-flight dlopen that then publishes the OLD generation. Refuse
    // typed; the caller retries once the load settles.
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        const PluginRecord *entry = record( id );
        if ( entry && entry->state == PluginState::Loading )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                              PluginDiagnosticSeverity::Error,
                              "install/upgrade refused: a load is in flight for "
                              "this plugin; retry once it settles",
                              id );
            result.status = PluginUpgradeStatus::Refused;
            return result;
        }
    }

    if ( !hadPrevious )
    {
        // Fresh install — but a plugin with this id LOADED from a different
        // root (a dev tree shadows the user root by root order) would leave
        // the running generation stale and produce duplicate-id records.
        // Refuse typed rather than silently shadowing.
        {
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            const PluginRecord *entry = record( id );
            if ( entry && entry->state == PluginState::Loaded
                 && entry->directory != target )
            {
                mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                                  PluginDiagnosticSeverity::Error,
                                  "install refused: '" + id + "' is loaded from "
                                      + entry->directory
                                      + " outside the user plugin root; unload "
                                        "it before installing",
                                  id );
                result.status = PluginUpgradeStatus::Refused;
                return result;
            }
        }
        PluginDiagnosticLog installLog;
        std::string installedDir;
        const bool installed =
            PluginPackage::install( sourceDir, installedDir, installLog );
        if ( installed )
            refresh();
        // Merge AFTER refresh(): refreshUnlocked() clears mDiagnostics —
        // merging earlier would erase the install evidence either way.
        mergeLog( installLog );
        if ( !installed )
        {
            result.status = PluginUpgradeStatus::Refused;
            return result;
        }
        result.status = PluginUpgradeStatus::Installed;
        result.installedDir = installedDir;
        return result;
    }

    const bool wasLoaded = isLoaded( id );

    // Step 5: snapshot the CURRENT install — the rollback source for the
    // whole transaction (bounded + marker-written; a plugin that cannot be
    // captured cannot be upgraded safely, so over-budget is a refusal).
    const std::string upgradeDir = upgradeSnapshotPath( id );
    const PluginSnapshotResult snap = capturePluginSnapshot(
        target, upgradeDir, id, PluginSnapshotBudget::fromEnvironment() );
    if ( !snap.ok() )
    {
        const PluginDiagnosticCode code =
            snap.status == PluginSnapshotStatus::BudgetExceeded
                ? PluginDiagnosticCode::QuotaExceeded
                : snap.status == PluginSnapshotStatus::Unsafe
                      ? PluginDiagnosticCode::EntrypointOutsideRoot
                      : PluginDiagnosticCode::ResourceMissing;
        addDiagnostic( code, PluginDiagnosticSeverity::Error,
                       "install/upgrade refused: cannot snapshot the current "
                       "install: " + snap.message,
                       id );
        result.status = PluginUpgradeStatus::Refused;
        return result;
    }
    // Every path below either removes upgradeDir explicitly or names it in
    // the failed-rollback diagnostic (the only kept-residue case).
    const auto dropUpgradeSnapshot = [&upgradeDir]() {
        std::error_code ec;
        fs::remove_all( fs::path( upgradeDir ), ec );
    };

    // Step 6: state migration while the OLD version is still loaded — a
    // failed migration aborts with nothing changed (same seam as reload()).
    if ( options.migrateState && !options.migrateState( target ) )
    {
        dropUpgradeSnapshot();
        addDiagnostic( PluginDiagnosticCode::InitializationFailed,
                       PluginDiagnosticSeverity::Error,
                       "install/upgrade refused: state migration failed; the "
                       "existing install is untouched",
                       id );
        result.status = PluginUpgradeStatus::Refused;
        return result;
    }

    // A pending async last-good capture of THIS plugin would read the tree
    // mid-swap — drop it; a successful (re)load recreates it.
    if ( const std::shared_ptr<PluginSnapshotJob> pending =
             takePendingSnapshotJob( id ) )
        pending->cancel();

    // Step 7: drain the old generation (barrier-protected; a busy plugin
    // refuses the upgrade and keeps running — PluginInUse diagnostic comes
    // from unload() itself).
    if ( wasLoaded && !unload( id, options.drainTimeoutMs ) )
    {
        dropUpgradeSnapshot();
        addDiagnostic( PluginDiagnosticCode::PluginInUse,
                       PluginDiagnosticSeverity::Error,
                       "install/upgrade aborted: the plugin could not be "
                       "unloaded (still executing?); the running version "
                       "stays loaded",
                       id );
        result.status = PluginUpgradeStatus::Refused;
        return result;
    }

    // Two shared tails for every rollback leg below:
    // - oldGenerationOk(): is the OLD generation usable after whatever the
    //   disk now holds — re-load iff it was loaded, publishable-state
    //   otherwise (the same bad-state set as the step-9 publish check).
    const auto oldGenerationOk = [&]() -> bool {
        if ( wasLoaded )
            return load( id );
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        const PluginRecord *entry = record( id );
        return entry && entry->state != PluginState::Broken
               && entry->state != PluginState::Incompatible
               && entry->state != PluginState::Blocked
               && entry->state != PluginState::Failed;
    };
    // - reportUnrecoverable(): the old generation does not come back. A
    //   restore that consumed upgradeDir may have left NO recovery copy —
    //   recapture the on-disk bytes (best effort, bounded) before claiming
    //   a kept snapshot so the diagnostic never names a missing path.
    const auto reportUnrecoverable = [&]() {
        std::error_code residueEc;
        if ( !fs::exists( fs::path( upgradeDir ), residueEc ) )
            capturePluginSnapshot( target, upgradeDir, id,
                                   PluginSnapshotBudget::fromEnvironment() );
        const bool snapshotKept =
            fs::exists( fs::path( upgradeDir ), residueEc ) && !residueEc;
        addDiagnostic( PluginDiagnosticCode::PluginUpgradeFailed,
                       PluginDiagnosticSeverity::Error,
                       std::string( "install/upgrade failed and the rollback "
                                    "did not restore a working version" )
                           + ( snapshotKept
                                   ? "; the recovery snapshot is kept at "
                                         + upgradeDir
                                   : " (no usable recovery snapshot remains)" ),
                       id );
        result.status = PluginUpgradeStatus::Failed;
    };

    // Step 8: the atomic swap — the package installer re-validates,
    // checksum-verifies, stages on the same filesystem and renames with
    // its own internal rollback.
    PluginDiagnosticLog installLog;
    std::string installedDir;
    bool installed =
        PluginPackage::install( sourceDir, installedDir, installLog );
    if ( installed )
    {
        // TOCTOU: the installer re-read the SOURCE at install time — the
        // gated manifest and the landed bytes can diverge if the source
        // was swapped mid-transaction. A mismatch means untrusted bytes
        // are on disk: take the install-failure path, not Upgraded.
        PluginManifest landed;
        PluginDiagnostic landedError;
        if ( !loadManifestFromFile( installedDir + "/plugin.json", landed,
                                    landedError )
             || landed.id != fresh.id || landed.version != fresh.version )
        {
            installed = false;
            addDiagnostic( PluginDiagnosticCode::TrustRejected,
                           PluginDiagnosticSeverity::Error,
                           "install/upgrade refused: the landed bytes do not "
                           "match the gated manifest (source changed "
                           "mid-transaction)",
                           id );
        }
    }
    if ( !installed )
    {
        refresh();
        // merge AFTER refresh(): refreshUnlocked() clears mDiagnostics, so
        // merging earlier would erase the very evidence for why the install
        // refused (checksum, staging, swap failure).
        mergeLog( installLog );
        // The installer's internal rollback is best-effort (a failed
        // promote restores the .old park, but THAT rename can fail too):
        // check whether the PREVIOUS install's bytes actually survive on
        // disk before claiming anything is "restored".
        PluginManifest after;
        PluginDiagnostic afterError;
        const bool intact =
            loadManifestFromFile( target + "/plugin.json", after, afterError )
            && after.id == previous.id && after.version == previous.version;
        if ( !intact )
        {
            std::string restoreError;
            if ( !restorePluginSnapshot( upgradeDir, target, id, restoreError ) )
                addDiagnostic( PluginDiagnosticCode::ResourceMissing,
                               PluginDiagnosticSeverity::Error,
                               "install/upgrade rollback could not restore "
                               "the snapshot: " + restoreError,
                               id );
            refresh();
        }
        if ( oldGenerationOk() )
        {
            dropUpgradeSnapshot();
            addDiagnostic( PluginDiagnosticCode::PluginUpgradeRolledBack,
                           PluginDiagnosticSeverity::Warning,
                           "install/upgrade rolled back: the package install "
                           "failed before commit; the previous version is "
                           "restored"
                               + std::string( wasLoaded ? " and running" : "" ),
                           id );
            result.status = PluginUpgradeStatus::RolledBack;
            result.installedDir = target;
            return result;
        }
        reportUnrecoverable();
        return result;
    }
    result.installedDir = installedDir;

    // Step 9: the swap committed — rescan the new bytes, then publish the
    // new generation (load iff it was loaded). An upgrade that leaves the
    // install Broken/Blocked/Incompatible has DESTROYED the previous
    // install — roll the bytes back even when nothing was running.
    refresh();
    mergeLog( installLog ); // same ordering rule as the failure branch
    bool ok = false;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        const PluginRecord *entry = record( id );
        if ( wasLoaded )
            ok = entry
                 && ( entry->state == PluginState::Validated
                      || entry->state == PluginState::Unloaded
                      || entry->state == PluginState::Loaded );
        else
            ok = entry && entry->state != PluginState::Broken
                 && entry->state != PluginState::Incompatible
                 && entry->state != PluginState::Blocked
                 && entry->state != PluginState::Failed;
    }
    if ( ok && wasLoaded )
        ok = load( id );

    if ( ok )
    {
        dropUpgradeSnapshot();
        addDiagnostic( PluginDiagnosticCode::PluginUpgraded,
                       PluginDiagnosticSeverity::Info,
                       "upgraded " + id + " " + previous.version + " -> "
                           + fresh.version,
                       id );
        result.status = PluginUpgradeStatus::Upgraded;
        return result;
    }

    // Step 10: rollback — restore the previous bytes from the verified
    // snapshot, rescan, reload iff it was loaded. refreshUnlocked() clears
    // mDiagnostics, so the new-version-failure evidence is captured first
    // and re-added across the refresh (same idiom as reload()).
    std::vector<PluginDiagnostic> evidence;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        for ( const PluginDiagnostic &item : mDiagnostics.items() )
        {
            if ( item.pluginId == id )
                evidence.push_back( item );
        }
    }
    const auto preserveEvidence = [this, &evidence]() {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        for ( const PluginDiagnostic &item : evidence )
            mDiagnostics.add( item );
    };

    addDiagnostic( PluginDiagnosticCode::RegistrationFailed,
                   PluginDiagnosticSeverity::Warning,
                   "install/upgrade failed to load/validate the new version; "
                   "rolling back to the previous install",
                   id );
    std::string restoreError;
    if ( restorePluginSnapshot( upgradeDir, target, id, restoreError ) )
    {
        // restorePluginSnapshot consumed the snapshot dir on success.
        refresh();
        preserveEvidence();
        if ( oldGenerationOk() )
        {
            addDiagnostic( PluginDiagnosticCode::PluginUpgradeRolledBack,
                           PluginDiagnosticSeverity::Warning,
                           "install/upgrade rolled back to the previous "
                           "version; fix the package and retry",
                           id );
            result.status = PluginUpgradeStatus::RolledBack;
            return result;
        }
    }
    else
    {
        addDiagnostic( PluginDiagnosticCode::ResourceMissing,
                       PluginDiagnosticSeverity::Error,
                       "install/upgrade rollback could not restore the "
                       "snapshot: " + restoreError,
                       id );
    }
    reportUnrecoverable();
    return result;
}

bool PluginRegistry::uninstallPlugin( const std::string &pluginId, int timeoutMs )
{
    // Lifecycle ownership (track 13.0): between the drain below and the
    // package removal, a foreign load() could otherwise re-publish the very
    // generation being deleted. One lifecycle op per plugin at a time —
    // the same rule reload()/installOrUpgrade() enforce (the guard's
    // unconditional erase cannot support nested same-thread ownership).
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        if ( mReloading.count( pluginId ) != 0 )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                              PluginDiagnosticSeverity::Error,
                              "uninstall refused: a lifecycle operation is in "
                              "progress for this plugin",
                              pluginId );
            return false;
        }
        mReloading.emplace( pluginId, std::this_thread::get_id() );
    }
    LifecycleOwnerGuard ownerGuard{ this, pluginId };

    // Same in-flight fence as installOrUpgrade: a load() mid-Loading is
    // invisible to isLoaded() but would publish into the deleted package.
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        const PluginRecord *entry = record( pluginId );
        if ( entry && entry->state == PluginState::Loading )
        {
            mDiagnostics.add( PluginDiagnosticCode::TrustRejected,
                              PluginDiagnosticSeverity::Error,
                              "uninstall refused: a load is in flight for this "
                              "plugin; retry once it settles",
                              pluginId );
            return false;
        }
    }

    // Drain-gated removal (WP3 uninstall leg): a loaded plugin unloads
    // first; a refused drain leaves the plugin loaded AND installed.
    if ( isLoaded( pluginId ) && !unload( pluginId, timeoutMs ) )
        return false; // PluginInUse diagnostic already recorded
    // Cancel this plugin's in-flight last-good capture BEFORE removing the
    // tree: otherwise its publish could land after the remove and recreate
    // an orphaned snapshot for a plugin that no longer exists.
    if ( const std::shared_ptr<PluginSnapshotJob> pending =
             takePendingSnapshotJob( pluginId ) )
        pending->cancel();
    PluginDiagnosticLog log;
    const bool ok = PluginPackage::uninstall( pluginId, log );
    std::string snapshotDir;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            mDiagnostics.merge( log );
        snapshotDir = lastGoodSnapshotPath( pluginId );
    }
    if ( !ok )
        return false;
    // The last-good snapshot belongs to this plugin's lifecycle — removing
    // the package must not leave it orphaned in the snapshot root.
    std::error_code ec;
    std::filesystem::remove_all( std::filesystem::path( snapshotDir ), ec );
    refresh();
    return true;
}

void PluginRegistry::unloadAll()
{
    // Cancel + join in-flight snapshot captures first: a worker mid-tree
    // must never outlive the teardown that follows (WP2 shutdown safety).
    // Cancel is cooperative — the worker exits at its next file boundary.
    cancelSnapshotJobs();
    // Lifecycle ops in flight (install/upgrade/uninstall/reload) hold
    // mReloading entries — tearing records down under a mid-transaction
    // upgrade would yank state the transaction still writes. Wait bounded
    // for owners to finish; unload()/load() refuse foreign-owned ids, so a
    // still-running op only shrinks what we may drain — never corrupts it.
    {
        const auto lifecycleDeadline = std::chrono::steady_clock::now()
                                       + std::chrono::milliseconds( unloadTimeoutMs() );
        for ( ;; )
        {
            {
                std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
                if ( mReloading.empty() )
                    break;
            }
            if ( std::chrono::steady_clock::now() >= lifecycleDeadline )
                break;
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }
    }
    // Shutdown path: drain every loaded plugin first (so a worker running
    // plugin code finishes against mapped code), then unload the drained
    // ones. A plugin that does not drain within the budget is left loaded —
    // its mapping is reclaimed by process exit; revoking/dlclosing it would
    // unmap code under a running thread.
    // Host-process plugins first, through the same barrier-protected
    // unload() branch (bounded by one shared deadline across them).
    if ( !mHostProcessLoaded.empty() )
    {
        std::vector<std::string> oopIds;
        {
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            oopIds = mHostProcessLoaded;
        }
        const auto sharedDeadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds( unloadTimeoutMs() );
        for ( const std::string &id : oopIds )
        {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                sharedDeadline - std::chrono::steady_clock::now() );
            if ( remaining.count() <= 0 )
                break;
            unload( id, static_cast<int>( remaining.count() ) );
        }
    }
    std::vector<std::string> ids;
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        for ( LoadedPlugin &entry : mLoaded )
            ids.push_back( entry.pluginId );
        for ( const std::string &id : ids )
        {
            if ( PluginRecord *entry = record( id ) )
                entry->state = PluginState::Quiescing;
        }
    }
    if ( mSink )
    {
        for ( const std::string &id : ids )
            mSink->beginPluginDrain( id );
    }

    std::vector<std::string> drained;
    // Shared deadline: shutdown latency is bounded ONCE, not per busy plugin
    // (P3 review finding — several busy plugins must not multiply the wait).
    const int budget = unloadTimeoutMs();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( budget );
    for ( const std::string &id : ids )
    {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now() );
        const bool idle =
            mSink ? mSink->waitPluginIdle( id, static_cast<int>( remaining.count() ) ) : true;
        if ( idle )
            drained.push_back( id );
        else if ( remaining.count() <= 0 )
            break;
    }

    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    if ( !mLoader )
    {
        // No native load ever happened; manifest-kind bookkeeping entries
        // have no library to unload, but their sink contributions still
        // need the revoke pass for symmetry.
        for ( const std::string &id : ids )
        {
            auto iterator = std::find_if( mLoaded.begin(), mLoaded.end(),
                                          [&]( const LoadedPlugin &entry ) {
                                              return entry.pluginId == id;
                                          } );
            if ( iterator == mLoaded.end() )
                continue;
            if ( mSink && !gDestructing )
                mSink->revokePlugin( id );
            mLoaded.erase( iterator );
            if ( PluginRecord *entry = record( id ) )
                entry->state = PluginState::Unloaded;
        }
        return;
    }
    for ( const std::string &id : drained )
    {
        auto iterator = std::find_if( mLoaded.begin(), mLoaded.end(),
                                      [&]( const LoadedPlugin &entry ) {
                                          return entry.pluginId == id;
                                      } );
        if ( iterator == mLoaded.end() )
            continue;
        if ( mSink && !gDestructing )
            mSink->revokePlugin( id );
        mLoader->unload( *iterator, mDiagnostics );
        mLoaded.erase( iterator );
        if ( PluginRecord *entry = record( id ) )
            entry->state = PluginState::Unloaded;
    }
    for ( const std::string &id : ids )
    {
        const bool stillLoaded =
            std::find_if( mLoaded.begin(), mLoaded.end(), [&]( const LoadedPlugin &entry ) {
                return entry.pluginId == id;
            } ) != mLoaded.end();
        if ( !stillLoaded )
            continue;
        if ( mSink )
            mSink->cancelPluginDrain( id );
        if ( PluginRecord *entry = record( id ) )
        {
            if ( entry->state == PluginState::Quiescing )
                entry->state = PluginState::Loaded;
            if ( !gDestructing )
            {
                entry->diagnostics.add( PluginDiagnosticCode::PluginInUse,
                                        PluginDiagnosticSeverity::Error,
                                        "unload skipped: plugin is in use at shutdown; its "
                                        "library stays mapped until process exit",
                                        id );
            }
        }
    }
}

bool PluginRegistry::ensureLoaded( const std::string &pluginId )
{
    {
        std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
        if ( findLoaded( mLoaded, pluginId ) )
            return true;
        if ( std::find( mHostProcessLoaded.begin(), mHostProcessLoaded.end(), pluginId )
             != mHostProcessLoaded.end() )
            return true;
    }
    return load( pluginId );
}

bool PluginRegistry::isLoaded( const std::string &pluginId ) const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    if ( findLoaded( mLoaded, pluginId ) )
        return true;
    return std::find( mHostProcessLoaded.begin(), mHostProcessLoaded.end(), pluginId )
           != mHostProcessLoaded.end();
}

std::string PluginRegistry::userIndexPath() const
{
    return PluginDiscovery::userPluginRoot() + "/../plugins.index.json";
}

void PluginRegistry::loadUserIndex()
{
    mDisabledIds.clear();
    std::ifstream input( userIndexPath() );
    if ( !input )
        return;
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string document = buffer.str();
    Json::Value root;
    // Bounded reader: the user index is a plain file on disk (anything can
    // rewrite it), and an unbounded parse of a deeply nested document
    // stack-overflows the caller instead of being ignored.
    Json::CharReaderBuilder builder;
    builder[ "allowComments" ] = true;
    builder[ "stackLimit" ] = 128;
    std::string parseError;
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    // Guarded: the reader THROWS when the depth bound is exceeded — a rewritten
    // index file must be ignored, not crash the caller.
    try
    {
        if ( !reader->parse( document.data(), document.data() + document.size(), &root,
                             &parseError )
             || !root.isObject() )
            return;
    }
    catch ( const Json::Exception & )
    {
        return;
    }
    for ( const Json::Value &id : root["disabled"] )
    {
        if ( id.isString() )
            mDisabledIds.push_back( id.asString() );
    }
}

void PluginRegistry::saveUserIndex() const
{
    const std::string path = userIndexPath();
    // The index lives one level above the user plugin root.
    const size_t slash = path.rfind( '/' );
    if ( slash != std::string::npos )
    {
        const std::string parent = path.substr( 0, slash );
        std::error_code error;
        std::filesystem::create_directories( parent, error );
    }
    const std::string temp = path + ".tmp";
    {
        std::ofstream output( temp, std::ios::trunc );
        if ( !output )
            return;
        Json::Value root( Json::objectValue );
        Json::Value disabled( Json::arrayValue );
        for ( const std::string &id : mDisabledIds )
            disabled.append( id );
        root["disabled"] = disabled;
        Json::StyledWriter writer;
        output << writer.write( root );
    }
    std::error_code renameError;
    std::filesystem::rename( temp, path, renameError );
    if ( renameError )
        std::remove( temp.c_str() );
}

bool PluginRegistry::setEnabled( const std::string &pluginId, bool enabled )
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    if ( !record( pluginId ) )
        return false;
    auto position = std::find( mDisabledIds.begin(), mDisabledIds.end(), pluginId );
    if ( enabled )
    {
        if ( position != mDisabledIds.end() )
            mDisabledIds.erase( position );
        if ( PluginRecord *entry = record( pluginId ) )
        {
            if ( entry->state == PluginState::Disabled )
                entry->state = PluginState::Validated;
        }
    }
    else
    {
        if ( position == mDisabledIds.end() )
            mDisabledIds.push_back( pluginId );
        if ( PluginRecord *entry = record( pluginId ) )
        {
            if ( entry->state == PluginState::Validated
                 || entry->state == PluginState::Unloaded
                 || entry->state == PluginState::Discovered )
            {
                entry->state = PluginState::Disabled;
                entry->diagnostics.add( PluginDiagnosticCode::PluginDisabled,
                                        PluginDiagnosticSeverity::Info,
                                        "plugin is disabled by the user", pluginId );
            }
        }
    }
    saveUserIndex();
    return true;
}

std::vector<std::string> PluginRegistry::userDisabledIds() const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    return mDisabledIds;
}

bool PluginRegistry::setUserDisabledIds( const std::vector<std::string> &ids )
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    mDisabledIds = ids;
    saveUserIndex();
    applyPolicyAndIndex();
    return true;
}

bool PluginRegistry::isEnabled( const std::string &pluginId ) const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    const PluginRecord *entry = record( pluginId );
    if ( !entry )
        return false;
    if ( entry->state == PluginState::Blocked )
        return false;
    // The persisted user index is the source of truth for enable/disable,
    // independent of the current lifecycle state.
    return std::find( mDisabledIds.begin(), mDisabledIds.end(), pluginId ) == mDisabledIds.end();
}

} // namespace exprs
