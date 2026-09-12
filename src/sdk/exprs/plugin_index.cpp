/***************************************************************************
 * exprs/plugin_index.cpp
 ***************************************************************************/
#include "exprs/plugin_index.h"

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_package.h"
#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_manifest.h"
#include "exprs/version.h"

#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace exprs {

namespace {

std::string currentPlatform()
{
#ifdef _WIN32
    return "windows";
#elif defined( __APPLE__ )
    return "macos";
#else
    return "linux";
#endif
}

} // namespace

bool PluginIndex::isCompatible( const PluginManifest &manifest, std::string &reason )
{
    if ( manifest.apiVersion != EXP_RS_PLUGIN_API_VERSION )
    {
        reason = "api_version " + manifest.apiVersion + " does not match host "
                 + EXP_RS_PLUGIN_API_VERSION;
        return false;
    }
    if ( manifest.abiVersion != pluginAbiVersion() )
    {
        reason = "abi_version " + std::to_string( manifest.abiVersion )
                 + " does not match host " + std::to_string( pluginAbiVersion() );
        return false;
    }
    if ( !manifest.platforms.empty()
         && std::find( manifest.platforms.begin(), manifest.platforms.end(),
                       currentPlatform() )
                == manifest.platforms.end() )
    {
        reason = "platform " + currentPlatform() + " not in the declared platforms list";
        return false;
    }
    return true;
}

std::vector<PluginIndexEntry> PluginIndex::scan( const std::vector<std::string> &directories,
                                                 int *skipped )
{
    std::vector<PluginIndexEntry> entries;
    for ( const std::string &directory : directories )
    {
        std::error_code iteratorError;
        fs::directory_iterator iterator( fs::path( directory ), iteratorError );
        if ( iteratorError )
        {
            if ( skipped )
                ++*skipped;
            continue;
        }
        for ( const fs::directory_entry &entry : iterator )
        {
            if ( !entry.is_directory( iteratorError ) || iteratorError )
                continue;
            const std::string name = entry.path().filename().generic_string();
            if ( name.empty() || name.front() == '.' )
                continue;
            const std::string manifestPath = entry.path().generic_string() + "/plugin.json";
            PluginDiagnostic parseError;
            PluginManifest manifest;
            if ( !loadManifestFromFile( manifestPath, manifest, parseError ) )
            {
                if ( skipped )
                    ++*skipped;
                continue;
            }
            PluginIndexEntry item;
            item.id = manifest.id;
            item.version = manifest.version;
            item.title = manifest.name;
            item.path = entry.path().generic_string();
            item.compatible = isCompatible( manifest, item.reason );
            entries.push_back( std::move( item ) );
        }
    }
    std::sort( entries.begin(), entries.end(),
               []( const PluginIndexEntry &a, const PluginIndexEntry &b ) { return a.id < b.id; } );
    return entries;
}

Json::Value PluginIndex::build( const std::vector<std::string> &directories,
                                const std::string &generatedAt )
{
    int skipped = 0;
    const std::vector<PluginIndexEntry> entries = scan( directories, &skipped );

    Json::Value index( Json::objectValue );
    index["generatedAt"] = generatedAt;
    Json::Value host( Json::objectValue );
    host["apiVersion"] = EXP_RS_PLUGIN_API_VERSION;
    host["abiVersion"] = pluginAbiVersion();
    host["platform"] = currentPlatform();
    index["host"] = host;
    Json::Value sources( Json::arrayValue );
    for ( const std::string &directory : directories )
        sources.append( directory );
    index["sources"] = sources;
    Json::Value plugins( Json::arrayValue );
    for ( const PluginIndexEntry &entry : entries )
    {
        Json::Value item( Json::objectValue );
        item["id"] = entry.id;
        item["version"] = entry.version;
        item["title"] = entry.title;
        item["path"] = entry.path;
        item["compatible"] = entry.compatible;
        item["reason"] = entry.reason;
        plugins.append( item );
    }
    index["plugins"] = plugins;
    index["skipped"] = skipped;
    return index;
}

Json::Value PluginIndex::applyPins( const Json::Value &index,
                                    const std::map<std::string, std::string> &pinnedVersions )
{
    Json::Value annotated = index;
    Json::Value plugins = annotated["plugins"];
    for ( Json::Value &plugin : plugins )
    {
        if ( !plugin.isObject() || !plugin["id"].isString() || !plugin["version"].isString() )
            continue; // hand-edited index entry: leave untouched, never throw
        const std::string id = plugin["id"].asString();
        const auto pin = pinnedVersions.find( id );
        if ( pin == pinnedVersions.end() )
        {
            plugin["pin"] = "unpinned";
            continue;
        }
        std::string reason;
        // Reuse the compatibility reason slot? No — pins get their own key.
        const std::string pinnedVersion = pin->second;
        const std::string available = plugin["version"].asString();
        // Reuse versionSatisfiesRange for the ordering decision: an exact
        // range means "is this version that exact version".
        const bool matches = available == pinnedVersion;
        plugin["pin"] = matches ? "pinned"
                        : exprs::PluginPackage::versionSatisfiesRange( pinnedVersion, ">=" + available )
                            ? "upgrade"
                            : "downgrade";
    }
    annotated["plugins"] = plugins;
    return annotated;
}

} // namespace exprs
