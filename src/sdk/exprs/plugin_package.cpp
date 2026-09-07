/***************************************************************************
 * exprs/plugin_package.cpp
 ***************************************************************************/
#include "exprs/plugin_package.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "exprs/plugin_discovery.h"
#include "exprs/plugin_validator.h"

namespace exprs {

namespace {

namespace fs = std::filesystem;

/// Directory test that does NOT follow symlinks (install payloads must not
/// gain directories through symlink hops).
bool isDirectory( const std::string &path )
{
    std::error_code error;
    return fs::symlink_status( fs::path( path ), error ).type() == fs::file_type::directory;
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
    std::error_code iteratorError;
    fs::directory_iterator iterator( fs::path( source ), iteratorError );
    if ( iteratorError )
    {
        error = "cannot open " + source;
        return false;
    }
    std::error_code createError;
    fs::create_directory( fs::path( target ), createError );
    if ( createError && !fs::is_directory( fs::path( target ) ) )
    {
        error = "cannot create " + target;
        return false;
    }
    bool ok = true;
    for ( const fs::directory_entry &entry : iterator )
    {
        if ( !ok )
            break;
        const std::string name = entry.path().filename().generic_string();
        if ( name.empty() || name.front() == '.' )
            continue; // skip cache indexes and hidden files
        const std::string childSource = source + "/" + name;
        const std::string childTarget = target + "/" + name;
        if ( !contained( source, childSource ) || !contained( target, childTarget ) )
        {
            error = "path escape refused: " + childSource;
            ok = false;
            break;
        }
        // symlink_status: symlinks/devices/fifos are refused, not followed.
        std::error_code statusError;
        const fs::file_status status = fs::symlink_status( entry.path(), statusError );
        if ( statusError )
        {
            error = "cannot stat " + childSource;
            ok = false;
            break;
        }
        if ( status.type() == fs::file_type::directory )
        {
            ok = copyTree( childSource, childTarget, error );
        }
        else if ( status.type() == fs::file_type::regular )
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
            // Preserve the source mode where the platform supports it.
            std::error_code permissionsError;
            fs::permissions( fs::path( childTarget ), status.permissions(),
                             fs::perm_options::replace, permissionsError );
            (void)permissionsError;
        }
        else
        {
            error = "refusing non-regular entry in package: " + childSource;
            ok = false;
            break;
        }
    }
    return ok;
}

bool removeTree( const std::string &path )
{
    std::error_code error;
    // remove_all on a symlink removes the link itself, not the target.
    fs::remove_all( fs::path( path ), error );
    return !error && !fs::exists( fs::path( path ) );
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

    if ( !PluginManifestValidator::isValidPluginId( manifest.id ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ManifestInvalidField;
        failure.pluginId = manifest.id;
        failure.message = "invalid plugin id — refused";
        log.add( failure );
        return false;
    }
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
    std::error_code createError;
    fs::create_directories( fs::path( userRoot ), createError );
    if ( createError && !fs::is_directory( fs::path( userRoot ) ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ResourceMissing;
        failure.pluginId = manifest.id;
        failure.message = "cannot create user plugin root " + userRoot;
        log.add( failure );
        return false;
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
    // The id comes from the CLI/SDK surface and becomes a path: gate it on
    // the manifest id grammar BEFORE any path arithmetic, and verify the
    // result stays inside the user root (defence in depth against '..').
    if ( !PluginManifestValidator::isValidPluginId( pluginId ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ManifestInvalidField;
        failure.pluginId = pluginId;
        failure.message = "invalid plugin id — refused";
        log.add( failure );
        return false;
    }
    const std::string userRoot = PluginDiscovery::userPluginRoot();
    const std::string target = userRoot + "/" + pluginId;
    if ( !contained( userRoot, target ) )
    {
        PluginDiagnostic failure;
        failure.code = PluginDiagnosticCode::ManifestInvalidField;
        failure.pluginId = pluginId;
        failure.message = "resolved path escapes the user plugin root — refused";
        log.add( failure );
        return false;
    }
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
    std::error_code error;
    fs::directory_iterator iterator( fs::path( userRoot ), error );
    if ( error )
        return ids;
    for ( const fs::directory_entry &entry : iterator )
    {
        const std::string name = entry.path().filename().generic_string();
        if ( name.empty() || name.front() == '.' )
            continue;
        PluginDiagnostic manifestError;
        PluginManifest manifest;
        if ( loadManifestFromFile( userRoot + "/" + name + "/plugin.json", manifest,
                                   manifestError ) )
            ids.push_back( manifest.id );
    }
    std::sort( ids.begin(), ids.end() );
    return ids;
}

} // namespace exprs
