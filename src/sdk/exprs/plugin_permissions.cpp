/***************************************************************************
 * exprs/plugin_permissions.cpp
 ***************************************************************************/
#include "exprs/plugin_permissions.h"

#include <algorithm>

#include <cstdlib>

namespace exprs {

namespace {
struct PermissionEntry
{
    PluginPermission permission;
    const char *name;
};

const PermissionEntry kPermissions[] = {
    { PluginPermission::FilesystemRead, "filesystem_read" },
    { PluginPermission::FilesystemWrite, "filesystem_write" },
    { PluginPermission::Network, "network" },
    { PluginPermission::ExternalProcess, "external_process" },
    { PluginPermission::Python, "python" },
    { PluginPermission::Gpu, "gpu" },
    { PluginPermission::ProjectMutation, "project_mutation" },
};

bool envFlag( const char *name )
{
    const char *value = std::getenv( name );
    if ( !value )
        return false;
    const std::string text( value );
    return text == "1" || text == "true" || text == "yes" || text == "on";
}
} // namespace

std::string pluginPermissionName( PluginPermission permission )
{
    for ( const PermissionEntry &entry : kPermissions )
    {
        if ( entry.permission == permission )
            return entry.name;
    }
    return "unknown";
}

bool pluginPermissionFromName( const std::string &name, PluginPermission &out )
{
    for ( const PermissionEntry &entry : kPermissions )
    {
        if ( name == entry.name )
        {
            out = entry.permission;
            return true;
        }
    }
    return false;
}

std::vector<PluginPermission> requiredPermissionsForCapability( const std::string &capability )
{
    if ( capability == "operator" )
    {
        // Operators are executable code, but only external-process-capable
        // operators touch processes; that permission is declared per operator
        // contribution, not per plugin.
        return {};
    }
    if ( capability == "data_provider" )
        return { PluginPermission::FilesystemRead };
    if ( capability == "model_runtime" )
        return { PluginPermission::Gpu, PluginPermission::FilesystemRead };
    if ( capability == "agent_tool" )
        return { PluginPermission::FilesystemRead };
    if ( capability == "external_tools" )
        return { PluginPermission::ExternalProcess, PluginPermission::FilesystemRead };
    if ( capability == "python_processing" )
        return { PluginPermission::Python };
    return {};
}

PluginPolicy PluginPolicy::fromEnvironment()
{
    PluginPolicy policy;
    const char *mode = std::getenv( "SICNU_PLUGIN_POLICY" );
    if ( mode && std::string( mode ) == "enforce" )
        policy.mode = PluginPolicyMode::Enforce;

    auto splitIds = []( const char *raw ) {
        std::vector<std::string> ids;
        if ( !raw )
            return ids;
        std::string text( raw );
        size_t start = 0;
        while ( start <= text.size() )
        {
            size_t comma = text.find( ',', start );
            if ( comma == std::string::npos )
                comma = text.size();
            std::string id = text.substr( start, comma - start );
            if ( !id.empty() )
                ids.push_back( id );
            start = comma + 1;
        }
        return ids;
    };

    policy.blockedPluginIds = splitIds( std::getenv( "SICNU_PLUGIN_BLOCK" ) );
    policy.allowedPluginIds = splitIds( std::getenv( "SICNU_PLUGIN_ALLOW" ) );
    policy.allowThirdPartyNative = !envFlag( "SICNU_PLUGIN_DISABLE_NATIVE_THIRD_PARTY" );
    return policy;
}

Json::Value PluginPolicy::toJson() const
{
    Json::Value json( Json::objectValue );
    json["mode"] = mode == PluginPolicyMode::Enforce ? "enforce" : "audit";
    Json::Value blocked( Json::arrayValue );
    for ( const std::string &id : blockedPluginIds )
        blocked.append( id );
    json["blocked"] = blocked;
    Json::Value allowed( Json::arrayValue );
    for ( const std::string &id : allowedPluginIds )
        allowed.append( id );
    json["allowed"] = allowed;
    json["allow_third_party_native"] = allowThirdPartyNative;
    return json;
}

std::vector<PluginPermission> parsePermissions( const Json::Value &manifestPermissions,
                                                std::vector<std::string> &warnings )
{
    std::vector<PluginPermission> result;
    if ( !manifestPermissions.isArray() )
        return result;
    for ( const Json::Value &entry : manifestPermissions )
    {
        if ( !entry.isString() )
        {
            warnings.push_back( "permissions: non-string entry ignored" );
            continue;
        }
        PluginPermission permission;
        if ( pluginPermissionFromName( entry.asString(), permission ) )
        {
            if ( std::find( result.begin(), result.end(), permission ) == result.end() )
                result.push_back( permission );
        }
        else
        {
            warnings.push_back( "permissions: unknown permission '" + entry.asString()
                                + "' ignored" );
        }
    }
    return result;
}

} // namespace exprs
