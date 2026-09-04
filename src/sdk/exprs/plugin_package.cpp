/***************************************************************************
 * exprs/plugin_package.cpp
 ***************************************************************************/
#include "exprs/plugin_package.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_validator.h"

namespace exprs {

namespace {

bool isDirectory( const std::string &path )
{
    struct stat info {};
    return ::lstat( path.c_str(), &info ) == 0 && S_ISDIR( info.st_mode );
}

/// True when @p candidate is strictly inside @p root (lexical containment,
/// no ".." components — protects against symlink/zip-slip escapes).
bool contained( const std::string &root, const std::string &candidate )
{
    if ( candidate.size() <= root.size() )
        return false;
    if ( candidate.compare( 0, root.size(), root ) != 0 )
        return false;
    if ( candidate[root.size()] != '/' )
        return false;
    return candidate.find( "..", root.size() + 1 ) == std::string::npos;
}

bool copyTree( const std::string &source, const std::string &target, std::string &error )
{
    DIR *dir = ::opendir( source.c_str() );
    if ( !dir )
    {
        error = "cannot open " + source;
        return false;
    }
    if ( ::mkdir( target.c_str(), 0755 ) != 0 && errno != EEXIST )
    {
        error = "cannot create " + target;
        ::closedir( dir );
        return false;
    }
    bool ok = true;
    struct dirent *entry = nullptr;
    while ( ok && ( entry = ::readdir( dir ) ) != nullptr )
    {
        const std::string name( entry->d_name );
        if ( name == "." || name == ".." || name.empty() || name.front() == '.' )
            continue; // skip cache indexes and hidden files
        const std::string childSource = source + "/" + name;
        const std::string childTarget = target + "/" + name;
        if ( !contained( source, childSource ) || !contained( target, childTarget ) )
        {
            error = "path escape refused: " + childSource;
            ok = false;
            break;
        }
        struct stat info {};
        if ( ::lstat( childSource.c_str(), &info ) != 0 )
        {
            error = "cannot stat " + childSource;
            ok = false;
            break;
        }
        if ( S_ISDIR( info.st_mode ) )
        {
            ok = copyTree( childSource, childTarget, error );
        }
        else if ( S_ISREG( info.st_mode ) )
        {
            std::ifstream input( childSource, std::ios::binary );
            std::ofstream output( childTarget, std::ios::binary | std::ios::trunc );
            if ( !input || !output )
            {
                error = "cannot copy " + childSource;
                ok = false;
                break;
            }
            output << input.rdbuf();
            ::chmod( childTarget.c_str(), info.st_mode & 0777 );
        }
        else
        {
            // Symlinks/devices/fifos are refused, not followed.
            error = "refusing non-regular entry in package: " + childSource;
            ok = false;
            break;
        }
    }
    ::closedir( dir );
    return ok;
}

bool removeTree( const std::string &path )
{
    DIR *dir = ::opendir( path.c_str() );
    if ( !dir )
        return ::remove( path.c_str() ) == 0;
    bool ok = true;
    while ( struct dirent *entry = ::readdir( dir ) )
    {
        const std::string name( entry->d_name );
        if ( name == "." || name == ".." )
            continue;
        const std::string child = path + "/" + name;
        struct stat info {};
        if ( ::lstat( child.c_str(), &info ) != 0 )
            continue;
        if ( S_ISDIR( info.st_mode ) )
            ok = removeTree( child ) && ok;
        else
            ok = ( ::unlink( child.c_str() ) == 0 ) && ok;
    }
    ::closedir( dir );
    return ::rmdir( path.c_str() ) == 0 && ok;
}

} // namespace

bool PluginPackage::install( const std::string &sourceDir, std::string &installedDir,
                             PluginDiagnosticLog &log )
{
    PluginDiagnostic diagnostic;
    diagnostic.file = sourceDir;
    PluginManifest manifest;
    if ( !loadManifestFromFile( sourceDir + "/plugin.json", manifest, diagnostic ) )
    {
        log.add( diagnostic );
        return false;
    }

    PluginValidationRequest request;
    request.pluginDir = sourceDir;
    if ( !PluginManifestValidator::validate( manifest, request, log ) )
        return false;

    const std::string userRoot = PluginDiscovery::userPluginRoot();
    const std::string target = userRoot + "/" + manifest.id;

    // Guard against id takeover by a different payload (an existing
    // directory with a manifest declaring a different id).
    PluginDiagnostic existingError;
    PluginManifest existing;
    if ( loadManifestFromFile( target + "/plugin.json", existing, existingError )
         && existing.id != manifest.id )
    {
        PluginDiagnostic conflict;
        conflict.code = PluginDiagnosticCode::ManifestDuplicateId;
        conflict.pluginId = manifest.id;
        conflict.file = target;
        conflict.message = "target directory hosts a different plugin id '" + existing.id + "'";
        log.add( conflict );
        return false;
    }

    // Create the user root chain.
    std::string current;
    size_t start = 0;
    while ( start <= userRoot.size() )
    {
        const size_t next = userRoot.find( '/', start );
        current = userRoot.substr( 0, next == std::string::npos ? userRoot.size() : next );
        if ( !current.empty() )
            ::mkdir( current.c_str(), 0755 );
        if ( next == std::string::npos )
            break;
        start = next + 1;
    }

    // Refresh the install: remove our previous payload first.
    if ( isDirectory( target ) )
    {
        PluginDiagnostic previous;
        PluginManifest previousManifest;
        if ( loadManifestFromFile( target + "/plugin.json", previousManifest, previous )
             && previousManifest.id == manifest.id )
        {
            if ( !removeTree( target ) )
            {
                PluginDiagnostic failure;
                failure.code = PluginDiagnosticCode::EntrypointMissing;
                failure.severity = PluginDiagnosticSeverity::Error;
                failure.pluginId = manifest.id;
                failure.message = "cannot remove previous install at " + target;
                log.add( failure );
                return false;
            }
        }
    }

    std::string copyError;
    if ( !copyTree( sourceDir, target, copyError ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ResourceMissing;
        failure.pluginId = manifest.id;
        failure.message = copyError;
        log.add( failure );
        removeTree( target );
        return false;
    }

    installedDir = target;
    PluginDiagnostic success;
    success.code = PluginDiagnosticCode::None;
    success.severity = PluginDiagnosticSeverity::Info;
    success.pluginId = manifest.id;
    success.message = "installed " + manifest.id + " " + manifest.version + " into " + target;
    log.add( success );
    return true;
}

bool PluginPackage::uninstall( const std::string &pluginId, PluginDiagnosticLog &log )
{
    const std::string userRoot = PluginDiscovery::userPluginRoot();
    const std::string target = userRoot + "/" + pluginId;
    if ( !isDirectory( target ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::EntrypointMissing;
        failure.pluginId = pluginId;
        failure.message = "plugin is not installed in the user root (" + target + ")";
        log.add( failure );
        return false;
    }
    PluginDiagnostic existingError;
    PluginManifest existing;
    if ( loadManifestFromFile( target + "/plugin.json", existing, existingError )
         && existing.id != pluginId )
    {
        PluginDiagnostic conflict;
        conflict.code = PluginDiagnosticCode::ManifestDuplicateId;
        conflict.pluginId = pluginId;
        conflict.message = "refusing to remove: directory hosts plugin '" + existing.id + "'";
        log.add( conflict );
        return false;
    }
    if ( !removeTree( target ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ResourceMissing;
        failure.pluginId = pluginId;
        failure.message = "cannot remove " + target;
        log.add( failure );
        return false;
    }
    return true;
}

std::vector<std::string> PluginPackage::installedIds()
{
    std::vector<std::string> ids;
    const std::string userRoot = PluginDiscovery::userPluginRoot();
    DIR *dir = ::opendir( userRoot.c_str() );
    if ( !dir )
        return ids;
    while ( struct dirent *entry = ::readdir( dir ) )
    {
        const std::string name( entry->d_name );
        if ( name.empty() || name.front() == '.' )
            continue;
        PluginDiagnostic error;
        PluginManifest manifest;
        if ( loadManifestFromFile( userRoot + "/" + name + "/plugin.json", manifest, error ) )
            ids.push_back( manifest.id );
    }
    ::closedir( dir );
    std::sort( ids.begin(), ids.end() );
    return ids;
}

} // namespace exprs
