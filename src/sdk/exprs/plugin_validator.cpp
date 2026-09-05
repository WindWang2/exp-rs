/***************************************************************************
 * exprs/plugin_validator.cpp
 ***************************************************************************/
#include "exprs/plugin_validator.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace exprs {

namespace {

bool fileExists( const std::string &path )
{
    struct stat info {};
    return ::stat( path.c_str(), &info ) == 0 && S_ISREG( info.st_mode );
}

bool isLibraryPath( const std::string &fileName )
{
    return fileName.size() > 3 && ( fileName.rfind( ".so" ) == fileName.size() - 3
                                    || fileName.rfind( ".dll" ) == fileName.size() - 4
                                    || fileName.rfind( ".dylib" ) == fileName.size() - 6 );
}

bool labelOk( const std::string &label )
{
    if ( label.empty() || label.size() > 63 )
        return false;
    for ( char c : label )
    {
        const bool ok = std::isdigit( static_cast<unsigned char>( c ) )
                        || ( c >= 'a' && c <= 'z' ) || c == '-';
        if ( !ok )
            return false;
    }
    // no leading/trailing '-'
    return label.front() != '-' && label.back() != '-';
}

std::vector<std::string> split( const std::string &text, char separator )
{
    std::vector<std::string> parts;
    size_t start = 0;
    while ( true )
    {
        const size_t pos = text.find( separator, start );
        if ( pos == std::string::npos )
        {
            parts.push_back( text.substr( start ) );
            break;
        }
        parts.push_back( text.substr( start, pos - start ) );
        start = pos + 1;
    }
    return parts;
}

void add( PluginDiagnosticLog &log, PluginDiagnosticCode code, PluginDiagnosticSeverity severity,
          const std::string &message, const std::string &pluginId, const std::string &field,
          const std::string &file = {} )
{
    PluginDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.message = message;
    diagnostic.pluginId = pluginId;
    diagnostic.field = field;
    diagnostic.file = file;
    log.add( diagnostic );
}

const char *kKnownCapabilities[] = {
    "operator", "data_provider", "model_runtime", "agent_tool", "ui", "cartography",
    "external_tools", "python_processing",
};

} // namespace

bool PluginManifestValidator::isValidPluginId( const std::string &id )
{
    if ( id.empty() || id.size() > 253 )
        return false;
    const std::vector<std::string> labels = split( id, '.' );
    if ( labels.size() < 2 )
        return false;
    for ( const std::string &label : labels )
    {
        if ( !labelOk( label ) )
            return false;
    }
    return true;
}

bool PluginManifestValidator::isValidContributionId( const std::string &id )
{
    const size_t colon = id.find( ':' );
    if ( colon == std::string::npos || colon == 0 || colon + 1 >= id.size() )
        return false;
    if ( id.find( ':', colon + 1 ) != std::string::npos )
        return false;
    for ( char c : id )
    {
        const bool ok = std::isdigit( static_cast<unsigned char>( c ) )
                        || ( c >= 'a' && c <= 'z' ) || c == '_' || c == '-' || c == ':';
        if ( !ok )
            return false;
    }
    return true;
}

bool PluginManifestValidator::isValidSemver( const std::string &version )
{
    const std::vector<std::string> parts = split( version, '.' );
    if ( parts.empty() || parts.size() > 3 )
        return false;
    for ( const std::string &part : parts )
    {
        if ( part.empty() )
            return false;
        for ( char c : part )
        {
            if ( !std::isdigit( static_cast<unsigned char>( c ) ) )
                return false;
        }
    }
    return true;
}

bool PluginManifestValidator::isValidDependencySpec( const std::string &spec, std::string &outId,
                                                     std::string &outRange )
{
    const size_t at = spec.find( '@' );
    if ( at == std::string::npos )
    {
        outId = spec;
        outRange.clear();
        return isValidPluginId( spec );
    }
    outId = spec.substr( 0, at );
    outRange = spec.substr( at + 1 );
    return isValidPluginId( outId ) && !outRange.empty();
}

bool PluginManifestValidator::validate( const PluginManifest &manifest,
                                        const PluginValidationRequest &request,
                                        PluginDiagnosticLog &diagnostics )
{
    const std::string &id = manifest.id;
    bool ok = true;

    auto fail = [&]( PluginDiagnosticCode code, const std::string &field,
                     const std::string &message ) {
        add( diagnostics, code, PluginDiagnosticSeverity::Error, message, id, field );
        ok = false;
    };
    auto warn = [&]( const std::string &field, const std::string &message ) {
        add( diagnostics, PluginDiagnosticCode::ManifestInvalidField,
             PluginDiagnosticSeverity::Warning, message, id, field );
    };

    if ( manifest.manifestVersion != 1 )
    {
        fail( PluginDiagnosticCode::ManifestUnknownVersion, "manifest_version",
              "only manifest_version 1 is supported by this host" );
        return false;
    }
    if ( id.empty() )
        fail( PluginDiagnosticCode::ManifestMissingField, "id", "manifest is missing 'id'" );
    else if ( !isValidPluginId( id ) )
        fail( PluginDiagnosticCode::ManifestInvalidField, "id",
              "plugin id '" + id + "' is not a valid reverse-DNS name" );

    if ( manifest.name.empty() )
        fail( PluginDiagnosticCode::ManifestMissingField, "name", "manifest is missing 'name'" );
    if ( manifest.version.empty() )
        fail( PluginDiagnosticCode::ManifestMissingField, "version", "manifest is missing 'version'" );
    else if ( !isValidSemver( manifest.version ) )
        fail( PluginDiagnosticCode::ManifestInvalidField, "version",
              "version '" + manifest.version + "' is not semver (MAJOR.MINOR[.PATCH])" );
    if ( manifest.apiVersion.empty() )
        fail( PluginDiagnosticCode::ManifestMissingField, "api_version",
              "manifest is missing 'api_version'" );
    if ( manifest.abiVersion == 0 )
        fail( PluginDiagnosticCode::ManifestMissingField, "abi_version",
              "manifest is missing 'abi_version'" );

    // Compatibility gate -----------------------------------------------------
    if ( !manifest.apiVersion.empty() )
    {
        const std::vector<std::string> parts = split( manifest.apiVersion, '.' );
        int declaredMajor = 0;
        int declaredMinor = 0;
        bool parsed = parts.size() == 2 && !parts[0].empty() && !parts[1].empty();
        if ( parsed )
        {
            try
            {
                size_t consumed = 0;
                declaredMajor = std::stoi( parts[0], &consumed );
                parsed = consumed == parts[0].size();
                declaredMinor = std::stoi( parts[1], &consumed );
                parsed = parsed && consumed == parts[1].size();
            }
            catch ( const std::exception & )
            {
                parsed = false;
            }
        }
        if ( !parsed )
        {
            fail( PluginDiagnosticCode::ManifestInvalidField, "api_version",
                  "api_version '" + manifest.apiVersion + "' must be MAJOR.MINOR" );
        }
        else
        {
            const PluginApiVersion declared{ declaredMajor, declaredMinor };
            if ( !isPluginApiCompatible( request.hostApi, declared ) )
            {
                fail( PluginDiagnosticCode::ApiVersionMismatch, "api_version",
                      "plugin requires API " + manifest.apiVersion + " but host provides "
                          + std::to_string( request.hostApi.major ) + "."
                          + std::to_string( request.hostApi.minor )
                          + " (same major, plugin minor must be <= host minor)" );
            }
        }
    }
    if ( manifest.abiVersion != 0 && manifest.abiVersion != request.hostAbi )
    {
        fail( PluginDiagnosticCode::AbiVersionMismatch, "abi_version",
              "plugin was built for ABI " + std::to_string( manifest.abiVersion )
                  + " but this host provides ABI " + std::to_string( request.hostAbi )
                  + "; rebuild the plugin against this SDK" );
    }

    if ( !manifest.platforms.empty() )
    {
#if defined( _WIN32 )
        const std::string currentPlatform = "windows";
#elif defined( __APPLE__ )
        const std::string currentPlatform = "macos";
#else
        const std::string currentPlatform = "linux";
#endif
        if ( std::find( manifest.platforms.begin(), manifest.platforms.end(), currentPlatform )
             == manifest.platforms.end() )
        {
            fail( PluginDiagnosticCode::PlatformUnsupported, "platforms",
                  "plugin does not declare support for platform '" + currentPlatform + "'" );
        }
    }

    // Capabilities ------------------------------------------------------------
    if ( manifest.capabilities.empty() )
        warn( "capabilities", "manifest declares no capabilities" );
    std::set<std::string> knownCaps;
    for ( const char *capability : kKnownCapabilities )
        knownCaps.insert( capability );
    for ( const std::string &capability : manifest.capabilities )
    {
        if ( !knownCaps.count( capability ) )
            warn( "capabilities", "unknown capability '" + capability + "' ignored" );
        for ( PluginPermission permission : requiredPermissionsForCapability( capability ) )
        {
            if ( std::find( manifest.permissions.begin(), manifest.permissions.end(), permission )
                 == manifest.permissions.end() )
            {
                add( diagnostics, PluginDiagnosticCode::PermissionDenied,
                     PluginDiagnosticSeverity::Warning,
                     "capability '" + capability + "' implies permission '"
                         + pluginPermissionName( permission )
                         + "' which the manifest does not declare",
                     id, "permissions" );
            }
        }
    }

    // Entrypoint --------------------------------------------------------------
    switch ( manifest.entrypointKind )
    {
    case PluginEntrypointKind::Native:
    {
        if ( manifest.entrypoint.empty() )
        {
            fail( PluginDiagnosticCode::ManifestMissingField, "entrypoint",
                  "native plugin is missing 'entrypoint'" );
        }
        else if ( !isLibraryPath( manifest.entrypoint ) )
        {
            fail( PluginDiagnosticCode::EntrypointNotLibrary, "entrypoint",
                  "entrypoint '" + manifest.entrypoint + "' is not a shared library" );
        }
        else if ( !request.pluginDir.empty()
                  && !fileExists( request.pluginDir + "/" + manifest.entrypoint ) )
        {
            fail( PluginDiagnosticCode::EntrypointMissing, "entrypoint",
                  "entrypoint file not found: " + request.pluginDir + "/" + manifest.entrypoint );
        }
        break;
    }
    case PluginEntrypointKind::Python:
        if ( manifest.python.module.empty() )
            fail( PluginDiagnosticCode::ManifestMissingField, "python.module",
                  "python plugin is missing python.module" );
        break;
    case PluginEntrypointKind::Manifest:
        if ( manifest.entrypoint.empty() )
        {
            // pure-manifest: fine, but every operator must be external
            for ( const ManifestOperator &op : manifest.operators )
            {
                if ( !op.hasExternalTool )
                    fail( PluginDiagnosticCode::ManifestEntrypointMismatch, "entrypoint",
                          "manifest-kind plugin operator '" + op.id
                              + "' must declare an 'external' section" );
            }
        }
        else
        {
            warn( "entrypoint", "entrypoint is ignored for manifest-kind plugins" );
        }
        break;
    }

    // Contributions -------------------------------------------------------------
    std::set<std::string> operatorIds;
    for ( const ManifestOperator &op : manifest.operators )
    {
        if ( !isValidContributionId( op.id ) )
            fail( PluginDiagnosticCode::ManifestInvalidField, "operators",
                  "operator id '" + op.id + "' must be 'vendor:name' (lowercase)" );
        if ( !operatorIds.insert( op.id ).second )
            fail( PluginDiagnosticCode::ManifestInvalidField, "operators",
                  "duplicate operator id '" + op.id + "'" );
        if ( manifest.entrypointKind == PluginEntrypointKind::Manifest && !op.hasExternalTool )
            fail( PluginDiagnosticCode::ManifestEntrypointMismatch, "operators",
                  "operator '" + op.id
                      + "' has no 'external' section but the plugin ships no binary" );
        if ( op.hasExternalTool && op.external.argv.empty() )
            fail( PluginDiagnosticCode::ManifestInvalidField, "operators",
                  "operator '" + op.id + "' declares an empty external argv" );
    }
    if ( manifest.operators.empty() && manifest.hasCapability( "operator" ) )
        warn( "operators", "capability 'operator' declared but no operators listed" );

    std::set<std::string> providerIds;
    for ( const ManifestDataProvider &provider : manifest.dataProviders )
    {
        if ( !isValidContributionId( provider.id ) )
            fail( PluginDiagnosticCode::ManifestInvalidField, "data_providers",
                  "data provider id '" + provider.id + "' must be 'vendor:name'" );
        if ( !providerIds.insert( provider.id ).second )
            fail( PluginDiagnosticCode::ManifestInvalidField, "data_providers",
                  "duplicate data provider id '" + provider.id + "'" );
        if ( provider.schemes.empty() )
            warn( "data_providers", "provider '" + provider.id + "' declares no schemes" );
    }
    for ( const ManifestModelRuntime &runtime : manifest.modelRuntimes )
    {
        if ( runtime.framework.empty() )
            fail( PluginDiagnosticCode::ManifestInvalidField, "model_runtimes",
                  "model runtime entry is missing 'framework'" );
    }
    std::set<std::string> toolIds;
    for ( const ManifestAgentTool &tool : manifest.agentTools )
    {
        if ( !isValidContributionId( tool.id ) )
            fail( PluginDiagnosticCode::ManifestInvalidField, "agent_tools",
                  "agent tool id '" + tool.id + "' must be 'vendor:name'" );
        if ( !toolIds.insert( tool.id ).second )
            fail( PluginDiagnosticCode::ManifestInvalidField, "agent_tools",
                  "duplicate agent tool id '" + tool.id + "'" );
        if ( !tool.inputSchema.isObject() )
            fail( PluginDiagnosticCode::ManifestInvalidField, "agent_tools",
                  "agent tool '" + tool.id + "' needs an object input_schema" );
    }

    // Dependencies --------------------------------------------------------------
    for ( const std::string &spec : manifest.dependencies )
    {
        std::string depId;
        std::string depRange;
        if ( !isValidDependencySpec( spec, depId, depRange ) )
            fail( PluginDiagnosticCode::ManifestInvalidField, "dependencies",
                  "dependency spec '" + spec + "' must be '<plugin-id>' or '<plugin-id>@<range>'" );
    }

    return ok;
}

} // namespace exprs
