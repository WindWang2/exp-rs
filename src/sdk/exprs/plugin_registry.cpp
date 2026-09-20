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
#include <sstream>
#include <utility>

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

/// WP4 snapshot helper: copies every regular file under @p pluginDir into
/// @p snapshotDir, preserving the relative layout. Symlinks are REFUSED
/// (never followed), mirroring PluginPackage::install's containment rules —
/// a dev-mode reload must not chase a link outside the package. Every
/// filesystem call is error_code-based; failures return false with a
/// message instead of throwing out of the reload path.
bool snapshotPluginFiles( const std::string &pluginDir, const std::string &snapshotDir,
                          std::string &error )
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path source( pluginDir );
    const fs::path target( snapshotDir );
    if ( !fs::is_directory( source, ec ) || ec )
    {
        error = "plugin directory is not readable";
        return false;
    }
    fs::create_directories( target, ec );
    if ( ec )
    {
        error = "cannot create the reload snapshot directory: " + ec.message();
        return false;
    }
    for ( fs::recursive_directory_iterator iterator( source, fs::directory_options::skip_permission_denied, ec ), end; !ec && iterator != end; iterator.increment( ec ) )
    {
        const fs::path &entry = iterator->path();
        const fs::path relative = fs::relative( entry, source, ec );
        if ( ec )
        {
            error = "cannot relativize snapshot entry: " + ec.message();
            return false;
        }
        const fs::path destination = target / relative;
        if ( fs::is_symlink( entry, ec ) )
        {
            error = "symlink '" + entry.generic_string() + "' refused in the reload snapshot";
            return false;
        }
        if ( ec )
        {
            error = "cannot inspect snapshot entry: " + ec.message();
            return false;
        }
        if ( fs::is_directory( entry, ec ) )
        {
            if ( ec )
            {
                error = "cannot inspect snapshot entry: " + ec.message();
                return false;
            }
            fs::create_directories( destination, ec );
            if ( ec )
            {
                error = "cannot create snapshot subdirectory: " + ec.message();
                return false;
            }
            continue;
        }
        fs::create_directories( destination.parent_path(), ec );
        if ( ec )
        {
            error = "cannot create snapshot parent directory: " + ec.message();
            return false;
        }
        fs::copy_file( entry, destination, fs::copy_options::overwrite_existing, ec );
        if ( ec )
        {
            error = "cannot copy '" + entry.generic_string() + "' into the snapshot: "
                    + ec.message();
            return false;
        }
    }
    if ( ec )
    {
        error = "cannot walk the plugin directory: " + ec.message();
        return false;
    }
    return true;
}

/// WP4 rollback helper: restores @p snapshotDir over @p pluginDir so the
/// on-disk bytes are exactly the version that last loaded. Files present in
/// the target but absent from the snapshot are REMOVED (the new version may
/// have added files), and the snapshot copy itself is deleted afterwards.
bool restorePluginFromSnapshot( const std::string &snapshotDir, const std::string &pluginDir,
                                std::string &error )
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path source( snapshotDir );
    const fs::path target( pluginDir );
    // Replace the payload: remove the target's files/subdirs, copy back.
    for ( fs::directory_iterator iterator( target, ec ), end; !ec && iterator != end; )
    {
        const fs::path entry = iterator->path();
        iterator.increment( ec );
        if ( ec )
            break;
        fs::remove_all( entry, ec );
        if ( ec )
        {
            error = "cannot clear the plugin directory for rollback: " + ec.message();
            return false;
        }
    }
    if ( ec )
    {
        error = "cannot walk the plugin directory for rollback: " + ec.message();
        return false;
    }
    if ( !snapshotPluginFiles( snapshotDir, pluginDir, error ) )
        return false;
    // The manifest index cache (plugin_discovery) is keyed by mtime, and a
    // file copy PRESERVES the source timestamps — so a byte-identical restore
    // would keep the failed version's cache entry alive and the rollback
    // would rescan the very manifest it just replaced. Stamping the restored
    // files with the current time is both honest (this IS new content on
    // disk) and what makes the cache re-parse.
    for ( fs::recursive_directory_iterator iterator( target, fs::directory_options::skip_permission_denied, ec ), end; !ec && iterator != end; iterator.increment( ec ) )
    {
        if ( fs::is_regular_file( iterator->path(), ec ) && !ec )
            fs::last_write_time( iterator->path(), fs::file_time_type::clock::now(), ec );
    }
    if ( ec )
    {
        error = "cannot stamp the restored files: " + ec.message();
        return false;
    }
    fs::remove_all( source, ec );
    if ( ec )
    {
        error = "cannot remove the reload snapshot: " + ec.message();
        return false;
    }
    return true;
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
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    mOptions = options;
    if ( mOptions.roots.empty() )
        mOptions.roots = PluginDiscovery::defaultRoots( mOptions.appDir, mOptions.installDataDir );
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

void PluginRegistry::applyPolicyAndIndex()
{
    for ( PluginRecord &record : mRecords )
    {
        if ( record.state != PluginState::Validated )
            continue;
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
            continue;
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
            continue;
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
            if ( record.state == PluginState::Blocked )
                continue;
        }

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

std::string PluginRegistry::lastGoodSnapshotPath( const std::string &pluginId ) const
{
    // mOptions is shared state: read it under the registry mutex (callers of
    // this const helper are reload paths that do not hold it).
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    if ( !mOptions.policy.devMode )
        return {};
    const std::string base = mOptions.tempDirectory.empty()
                                 ? std::filesystem::temp_directory_path().generic_string()
                                 : mOptions.tempDirectory;
    return base + "/plugin-last-good-" + pluginId;
}

void PluginRegistry::refreshLastGoodSnapshot( const std::string &pluginId,
                                              const std::string &pluginDir )
{
    // Dev mode only: production hosts keep no second copy of any plugin.
    if ( !mOptions.policy.devMode )
        return;
    const std::string snapshotDir = lastGoodSnapshotPath( pluginId );
    if ( snapshotDir.empty() )
        return;
    std::error_code ec;
    std::filesystem::remove_all( snapshotDir, ec );
    std::string error;
    snapshotPluginFiles( pluginDir, snapshotDir, error );
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
        mReloading.insert( pluginId );
    }
    // Scope guard so every early return below clears the in-flight marker.
    struct ReloadGuard
    {
        PluginRegistry *registry;
        std::string id;
        ~ReloadGuard()
        {
            std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
            registry->mReloading.erase( id );
        }
    } reloadGuard{ this, pluginId };

    std::string pluginDir;
    bool loaded = false;
    bool hostedOop = false;
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
    const std::string snapshotDir = lastGoodSnapshotPath( pluginId );
    bool haveSnapshot = false;
    if ( snapshotDir.empty() || !std::filesystem::exists( snapshotDir ) )
    {
        addDiagnostic( PluginDiagnosticCode::ResourceMissing, PluginDiagnosticSeverity::Warning,
                       "hot reload has no last-known-good snapshot; a failed reload "
                           "will leave the plugin unloaded instead of rolled back" );
    }
    else
    {
        // Trust a snapshot only when it is COMPLETE: a partial copy (disk
        // full mid-snapshot, unreadable subdirectory) would restore a broken
        // package and destroy the working bytes it is supposed to protect.
        std::error_code ec;
        const bool hasManifest =
            std::filesystem::exists( snapshotDir + "/plugin.json", ec ) && !ec;
        std::size_t entries = 0;
        for ( const std::filesystem::directory_entry &ignored :
              std::filesystem::directory_iterator( snapshotDir, ec ) )
        {
            (void)ignored;
            ++entries;
        }
        if ( ec || !hasManifest || entries < 2 )
        {
            addDiagnostic( PluginDiagnosticCode::ResourceMissing,
                           PluginDiagnosticSeverity::Warning,
                           "hot reload found an incomplete last-known-good snapshot "
                               "(missing plugin.json or payload); no rollback is possible" );
        }
        else
        {
            haveSnapshot = true;
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
        if ( restorePluginFromSnapshot( snapshotDir, pluginDir, restoreError ) )
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

void PluginRegistry::unloadAll()
{
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
