/***************************************************************************
 * exprs/plugin_discovery.cpp
 ***************************************************************************/
#include "exprs/plugin_discovery.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "exprs/plugin_validator.h"

namespace exprs {

namespace {

bool isDirectory( const std::string &path )
{
    struct stat info {};
    return ::stat( path.c_str(), &info ) == 0 && S_ISDIR( info.st_mode );
}

bool isRegularFile( const std::string &path )
{
    struct stat info {};
    return ::stat( path.c_str(), &info ) == 0 && S_ISREG( info.st_mode );
}

std::vector<std::string> listSubdirectories( const std::string &root )
{
    std::vector<std::string> result;
    DIR *dir = ::opendir( root.c_str() );
    if ( !dir )
        return result;
    while ( struct dirent *entry = ::readdir( dir ) )
    {
        const std::string name( entry->d_name );
        if ( name.empty() || name.front() == '.' )
            continue; // skip hidden + . / ..
        const std::string full = root + "/" + name;
        if ( isDirectory( full ) )
            result.push_back( full );
    }
    ::closedir( dir );
    std::sort( result.begin(), result.end() );
    return result;
}

// Manifest index cache -------------------------------------------------------

struct IndexEntry
{
    long long mtime = 0;
    Json::Value manifest;
};

Json::Value loadIndex( const std::string &root )
{
    std::ifstream input( root + "/.exprs-manifest-index.json" );
    if ( !input )
        return Json::Value( Json::nullValue );
    std::stringstream buffer;
    buffer << input.rdbuf();
    Json::Value parsed;
    Json::Reader reader;
    if ( !reader.parse( buffer.str(), parsed, false ) || !parsed.isObject() )
        return Json::Value( Json::nullValue );
    return parsed;
}

void storeIndex( const std::string &root, const Json::Value &index )
{
    const std::string path = root + "/.exprs-manifest-index.json";
    const std::string temp = path + ".tmp";
    {
        std::ofstream output( temp, std::ios::trunc );
        if ( !output )
            return;
        Json::StyledWriter writer;
        output << writer.write( index );
    }
    std::rename( temp.c_str(), path.c_str() );
}

// Parses a manifest from an embedded index value without touching the file.
bool manifestFromIndexValue( const Json::Value &root, PluginManifest &out,
                             PluginDiagnostic &error )
{
    if ( !root.isObject() )
        return false;
    return PluginManifest::fromJson( root, out, error );
}

bool tryCachedManifest( const Json::Value &index, const std::string &dir,
                        const std::string &manifestPath, PluginManifest &out )
{
    if ( !index.isObject() || !index.isMember( dir ) )
        return false;
    const Json::Value &entry = index[dir];
    if ( !entry.isObject() || !entry.isMember( "mtime" ) || !entry.isMember( "manifest" ) )
        return false;
    struct stat info {};
    if ( ::stat( manifestPath.c_str(), &info ) != 0 )
        return false;
    if ( static_cast<long long>( info.st_mtime ) != entry["mtime"].asInt64() )
        return false;
    PluginDiagnostic ignored;
    return manifestFromIndexValue( entry["manifest"], out, ignored );
}

void appendToIndex( Json::Value &index, const std::string &dir, const std::string &manifestPath,
                    const PluginManifest &manifest )
{
    struct stat info {};
    if ( ::stat( manifestPath.c_str(), &info ) != 0 )
        return;
    Json::Value entry( Json::objectValue );
    entry["mtime"] = static_cast<Json::Int64>( info.st_mtime );
    entry["manifest"] = manifest.toJson();
    index[dir] = entry;
}

/// E2xxx (API/ABI/platform) failures are the loader compatibility gate ->
/// Incompatible; every other structural failure -> Broken.
PluginState incompatibleStateFor( const std::string &pluginId, const PluginDiagnosticLog &log )
{
    for ( const PluginDiagnostic &item : log.items() )
    {
        if ( item.pluginId == pluginId && item.severity == PluginDiagnosticSeverity::Error
             && item.code >= PluginDiagnosticCode::ApiVersionMismatch
             && item.code <= PluginDiagnosticCode::PlatformUnsupported )
            return PluginState::Incompatible;
    }
    return PluginState::Broken;
}

PluginOrigin originForRootIndex( size_t index, size_t userRootPosition )
{
    if ( index == 0 )
        return PluginOrigin::Builtin;
    if ( userRootPosition != static_cast<size_t>( -1 ) && index == userRootPosition )
        return PluginOrigin::User;
    return PluginOrigin::System;
}

} // namespace

std::string PluginDiscovery::userPluginRoot()
{
    // Overridable so tests, sandboxes and multi-instance setups can redirect
    // the user plugin root (and the enable/disable index next to it).
    const char *overrideRoot = std::getenv( "SICNU_PLUGIN_USER_ROOT" );
    if ( overrideRoot && *overrideRoot )
        return overrideRoot;
    const char *home = std::getenv( "HOME" );
    const std::string homeDir = home ? home : "/tmp";
    return homeDir + "/.local/share/sicnu_geo_rs/plugins";
}

std::vector<std::string> PluginDiscovery::defaultRoots( const std::string &appDir,
                                                        const std::string &installDataDir )
{
    std::vector<std::string> roots;
    if ( !appDir.empty() )
        roots.push_back( appDir + "/../plugins" );
    if ( !installDataDir.empty() )
        roots.push_back( installDataDir + "/plugins" );
    roots.push_back( userPluginRoot() );
    const char *extra = std::getenv( "SICNU_PLUGIN_PATH" );
    if ( extra )
    {
        std::string text( extra );
        size_t start = 0;
        while ( start <= text.size() )
        {
            const size_t colon = text.find( ':', start );
            const size_t end = colon == std::string::npos ? text.size() : colon;
            const std::string candidate = text.substr( start, end - start );
            if ( !candidate.empty() )
                roots.push_back( candidate );
            if ( colon == std::string::npos )
                break;
            start = colon + 1;
        }
    }
    return roots;
}

std::vector<PluginRecord> PluginDiscovery::scan( const PluginDiscoveryOptions &options,
                                                 PluginDiagnosticLog &log )
{
    std::vector<PluginRecord> records;
    std::map<std::string, size_t> idOwner; // plugin id -> records index (first root wins)

    const std::string userRoot = userPluginRoot();
    for ( size_t rootIndex = 0; rootIndex < options.roots.size(); ++rootIndex )
    {
        const std::string &root = options.roots[rootIndex];
        if ( root.empty() || !isDirectory( root ) )
            continue;

        Json::Value indexCache = options.useCache ? loadIndex( root ) : Json::Value( Json::nullValue );
        Json::Value indexFresh( Json::objectValue );
        bool indexDirty = false;

        for ( const std::string &dir : listSubdirectories( root ) )
        {
            const std::string manifestPath = dir + "/plugin.json";
            if ( !isRegularFile( manifestPath ) )
                continue;

            PluginRecord record;
            record.directory = dir;
            record.manifestPath = manifestPath;
            record.origin = root == userRoot ? PluginOrigin::User
                                             : originForRootIndex( rootIndex, static_cast<size_t>( -1 ) );

            bool parsed = false;
            if ( options.useCache && tryCachedManifest( indexCache, dir, manifestPath,
                                                        record.manifest ) )
            {
                parsed = true;
                indexFresh[dir] = indexCache[dir];
            }
            else
            {
                PluginDiagnostic error;
                error.file = manifestPath;
                if ( loadManifestFromFile( manifestPath, record.manifest, error ) )
                {
                    parsed = true;
                    appendToIndex( indexFresh, dir, manifestPath, record.manifest );
                    indexDirty = true;
                }
                else
                {
                    record.diagnostics.add( error );
                    record.state = PluginState::Broken;
                }
            }

            if ( parsed )
            {
                record.state = PluginState::Discovered;
                if ( idOwner.count( record.manifest.id ) )
                {
                    const size_t ownerIndex = idOwner[record.manifest.id];
                    PluginDiagnostic duplicate;
                    duplicate.code = PluginDiagnosticCode::ManifestDuplicateId;
                    duplicate.severity = PluginDiagnosticSeverity::Error;
                    duplicate.pluginId = record.manifest.id;
                    duplicate.message = "duplicate plugin id; '" + records[ownerIndex].directory
                                        + "' has priority over '" + dir + "'";
                    record.diagnostics.add( duplicate );
                    record.state = PluginState::Broken;
                }
                else
                {
                    idOwner[record.manifest.id] = records.size();
                }
            }
            records.push_back( std::move( record ) );
        }

        if ( options.useCache && indexDirty )
            storeIndex( root, indexFresh );
    }
    return records;
}

PluginRecord PluginDiscovery::inspectDirectory( const std::string &directory,
                                                PluginDiagnosticLog &log )
{
    PluginRecord record;
    record.directory = directory;
    record.manifestPath = directory + "/plugin.json";
    record.origin = PluginOrigin::External;

    PluginDiagnostic error;
    error.file = record.manifestPath;
    if ( !loadManifestFromFile( record.manifestPath, record.manifest, error ) )
    {
        record.diagnostics.add( error );
        record.state = PluginState::Broken;
        log.merge( record.diagnostics );
        return record;
    }
    record.state = PluginState::Discovered;

    PluginValidationRequest request;
    request.pluginDir = directory;
    if ( PluginManifestValidator::validate( record.manifest, request, record.diagnostics ) )
        record.state = PluginState::Validated;
    else
        record.state = incompatibleStateFor( record.manifest.id, record.diagnostics );

    log.merge( record.diagnostics );
    return record;
}

} // namespace exprs
