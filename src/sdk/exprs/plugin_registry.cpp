/***************************************************************************
 * exprs/plugin_registry.cpp
 ***************************************************************************/
#include "exprs/plugin_registry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

#include <sys/stat.h>
#include <sys/types.h>

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

bool PluginRegistry::load( const std::string &pluginId )
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    return loadUnlocked( pluginId );
}

bool PluginRegistry::loadUnlocked( const std::string &pluginId )
{
    // Caller holds gRegistryMutex.
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
    if ( std::find( mDisabledIds.begin(), mDisabledIds.end(), pluginId ) != mDisabledIds.end() )
    {
        mDiagnostics.add( PluginDiagnosticCode::PluginDisabled, PluginDiagnosticSeverity::Error,
                          "plugin is disabled by the user", pluginId );
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
        // (external tools); nothing to dlopen, so loading is a host-side
        // bookkeeping op.
        LoadedPlugin hosted;
        hosted.pluginId = pluginId;
        mLoaded.push_back( std::move( hosted ) );
        entry->state = PluginState::Loaded;
        return true;
    }
    if ( !mSink )
    {
        mDiagnostics.add( PluginDiagnosticCode::RegistrationFailed,
                          PluginDiagnosticSeverity::Error,
                          "no contribution sink installed", pluginId );
        return false;
    }
    entry->state = PluginState::Loading;
    if ( !mLoader )
        mLoader = std::make_unique<PluginLoader>();
    const bool ok = mLoader->load( *entry, *mServices, *mSink, mDiagnostics );
    if ( ok )
    {
        mLoaded.push_back( mLoader->take() );
        entry->state = PluginState::Loaded;
    }
    else
    {
        // Partial registrations must not outlive the failed load.
        if ( mSink )
            mSink->revokePlugin( pluginId );
        entry->state = PluginState::Failed;
    }
    return ok;
}

std::vector<std::string> PluginRegistry::loadAllValidated()
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    std::vector<std::string> loadedIds;
    for ( PluginRecord &entry : mRecords )
    {
        if ( entry.state != PluginState::Validated )
            continue;
        if ( loadUnlocked( entry.id() ) )
            loadedIds.push_back( entry.id() );
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

bool PluginRegistry::unload( const std::string &pluginId )
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    auto iterator = std::find_if( mLoaded.begin(), mLoaded.end(),
                                  [&]( const LoadedPlugin &entry ) {
                                      return entry.pluginId == pluginId;
                                  } );
    if ( iterator == mLoaded.end() )
        return false;
    // Revoke host-side contributions BEFORE dlclose: std::function targets,
    // executors and providers created by the plugin must be released while
    // its code is still mapped.
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

void PluginRegistry::unloadAll()
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    if ( !mLoader )
        return;
    for ( LoadedPlugin &entry : mLoaded )
    {
        if ( mSink && !gDestructing )
            mSink->revokePlugin( entry.pluginId );
    }
    for ( LoadedPlugin &entry : mLoaded )
        mLoader->unload( entry, mDiagnostics );
    mLoaded.clear();
    for ( PluginRecord &entry : mRecords )
    {
        if ( entry.state == PluginState::Loaded )
            entry.state = PluginState::Unloaded;
    }
}

bool PluginRegistry::ensureLoaded( const std::string &pluginId )
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    if ( findLoaded( mLoaded, pluginId ) )
        return true;
    return loadUnlocked( pluginId );
}

bool PluginRegistry::isLoaded( const std::string &pluginId ) const
{
    std::lock_guard<std::recursive_mutex> lock( gRegistryMutex );
    return findLoaded( mLoaded, pluginId ) != nullptr;
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
    Json::Value root;
    Json::Reader reader;
    if ( !reader.parse( buffer.str(), root, false ) || !root.isObject() )
        return;
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
        ::mkdir( parent.c_str(), 0755 );
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
    std::rename( temp.c_str(), path.c_str() );
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
