/***************************************************************************
 * exprs/plugin_capabilities.cpp
 ***************************************************************************/
#include "exprs/plugin_capabilities.h"

#include "exprs/path_policy.h"

#include <algorithm>
#include <filesystem>

namespace exprs {

namespace {

bool asBool( const Json::Value &value, bool &out )
{
    if ( value.isBool() )
    {
        out = value.asBool();
        return true;
    }
    return false;
}

std::vector<std::string> parsePathList( const Json::Value &value, bool &ok )
{
    std::vector<std::string> paths;
    ok = value.isArray();
    if ( !ok )
        return paths;
    for ( const Json::Value &item : value )
    {
        if ( !item.isString() || item.asString().empty() )
        {
            ok = false;
            return paths;
        }
        paths.push_back( item.asString() );
    }
    return paths;
}

} // namespace

Json::Value PluginCapabilities::toJson() const
{
    Json::Value json( Json::objectValue );
    Json::Value read( Json::arrayValue );
    for ( const std::string &root : fsReadRoots )
        read.append( root );
    json["filesystemRead"] = read;
    Json::Value write( Json::arrayValue );
    for ( const std::string &root : fsWriteRoots )
        write.append( root );
    json["filesystemWrite"] = write;
    json["network"] = network;
    json["externalProcess"] = externalProcess;
    if ( !gpuHint.empty() )
        json["gpuHint"] = gpuHint;
    json["workspaceMutation"] = workspaceMutation;
    json["projectMutation"] = projectMutation;
    Json::Value frameworks( Json::arrayValue );
    for ( const std::string &framework : modelFrameworks )
        frameworks.append( framework );
    json["modelFrameworks"] = frameworks;
    json["uiContribution"] = uiContribution;
    json["destructive"] = destructive;
    return json;
}

bool expandCapabilityPath( const std::string &declared, const std::string &pluginDir,
                           const std::string &workspaceRoot, const std::string &tempDir,
                           std::string &resolved, std::string &error )
{
    std::string expanded = declared;
    auto replaceAll = []( std::string &text, const std::string &from, const std::string &to ) {
        if ( from.empty() )
            return;
        size_t pos = 0;
        while ( ( pos = text.find( from, pos ) ) != std::string::npos )
        {
            text.replace( pos, from.size(), to );
            pos += to.size();
        }
    };
    if ( declared.find( "${workspace}" ) != std::string::npos && workspaceRoot.empty() )
    {
        error = "capability path uses ${workspace} but no workspace is configured: " + declared;
        return false;
    }
    replaceAll( expanded, "${plugin}", pluginDir );
    replaceAll( expanded, "${workspace}", workspaceRoot );
    replaceAll( expanded, "${temp}", tempDir );

    if ( expanded.find( "${" ) != std::string::npos )
    {
        error = "unknown placeholder in capability path: " + declared;
        return false;
    }
    if ( expanded.empty() )
    {
        error = "capability path expands to an empty string: " + declared;
        return false;
    }
    if ( !PathPolicy::isAbsolute( expanded ) )
    {
        // Declared paths must be anchored to a known root: relative fragments
        // would silently resolve against the host's cwd.
        error = "capability path is not rooted at ${plugin}/${workspace}/${temp}: " + declared;
        return false;
    }

    // Canonicalize what exists; for not-yet-existing tails (planned output
    // directories), canonicalize the deepest existing ancestor and re-attach
    // the remainder lexically so validation stays deterministic.
    resolved = PathPolicy::canonical( expanded );
    if ( resolved.empty() )
    {
        const std::filesystem::path original( expanded );
        std::filesystem::path parent = original.parent_path();
        while ( !parent.empty() )
        {
            const std::string canonicalParent =
                PathPolicy::canonical( parent.generic_string() );
            if ( !canonicalParent.empty() )
            {
                const std::filesystem::path rel = original.lexically_relative( parent );
                if ( !rel.empty() && rel.generic_string() != "." )
                {
                    resolved = ( std::filesystem::path( canonicalParent ) / rel )
                                   .lexically_normal()
                                   .generic_string();
                }
                else
                {
                    resolved = canonicalParent;
                }
                break;
            }
            const std::filesystem::path next = parent.parent_path();
            if ( next == parent )
                break;
            parent = next;
        }
    }
    if ( resolved.empty() )
    {
        error = "capability path cannot be canonicalized: " + declared;
        return false;
    }
    return true;
}

bool pathIsWithinRoot( const std::string &candidate, const std::string &root,
                       std::string &resolvedCandidate )
{
    if ( candidate.empty() || root.empty() )
        return false;
    // Canonicalize what exists; anchor not-yet-existing tails lexically at
    // the deepest existing ancestor (mirrors expandCapabilityPath).
    std::string resolved = PathPolicy::canonical( candidate );
    if ( resolved.empty() )
    {
        std::string anchorError;
        if ( !expandCapabilityPath( candidate, candidate, "", "", resolved, anchorError ) )
            return false;
        if ( resolved.empty() )
            return false;
    }
    resolvedCandidate = resolved;
    const std::string canonicalRoot = PathPolicy::canonical( root );
    if ( canonicalRoot.empty() )
        return false;
    if ( resolved == canonicalRoot )
        return true;
    std::string prefix = canonicalRoot;
    if ( prefix.back() != '/' )
        prefix += '/';
    return resolved.rfind( prefix, 0 ) == 0;
}

PluginCapabilityParseResult parsePluginAccess(
    const Json::Value &access, const std::string &pluginDir,
    const std::string &workspaceRoot, const std::string &tempDir )
{
    PluginCapabilityParseResult result;
    const Json::Value &json = access;
    if ( json.isNull() )
        return result; // no capabilities declared: defaults (deny-all) apply
    if ( !json.isObject() )
    {
        result.errors.push_back( "manifest access must be an object" );
        return result;
    }

    PluginCapabilities &caps = result.capabilities;

    const Json::Value &filesystem = json[ "filesystem" ];
    if ( !filesystem.isNull() )
    {
        if ( !filesystem.isObject() )
        {
            result.errors.push_back( "capabilities.filesystem must be an object" );
            return result;
        }
        for ( const auto &[ key, target ] : { std::pair<const char *, std::vector<std::string> *>{ "read", &caps.fsReadRoots },
                                              { "write", &caps.fsWriteRoots } } )
        {
            bool ok = false;
            if ( filesystem[ key ].isNull() )
                continue; // key not declared: no grant for that mode
            const std::vector<std::string> declared = parsePathList( filesystem[ key ], ok );
            if ( !ok )
            {
                result.errors.push_back(
                    std::string( "capabilities.filesystem." ) + key + " must be an array of paths" );
                return result;
            }
            for ( const std::string &path : declared )
            {
                std::string resolved;
                std::string error;
                if ( !expandCapabilityPath( path, pluginDir, workspaceRoot, tempDir, resolved, error ) )
                {
                    // Fail closed: an unresolvable root never becomes a grant.
                    result.errors.push_back( error );
                    continue;
                }
                target->push_back( resolved );
            }
        }
    }

    if ( !asBool( json.get( "network", Json::Value() ), caps.network )
         && !json[ "network" ].isNull() )
        result.errors.push_back( "capabilities.network must be a boolean" );

    if ( !asBool( json.get( "externalProcess", Json::Value() ), caps.externalProcess )
         && !json[ "externalProcess" ].isNull() )
        result.errors.push_back( "capabilities.externalProcess must be a boolean" );

    const Json::Value &gpu = json[ "gpu" ];
    if ( !gpu.isNull() )
    {
        if ( gpu.isString() )
        {
            caps.gpuHint = gpu.asString();
        }
        else if ( gpu.isObject() )
        {
            const Json::Value &hint = gpu[ "hint" ];
            if ( hint.isString() )
                caps.gpuHint = hint.asString();
            else
                result.errors.push_back( "capabilities.gpu.hint must be a string" );
        }
        else
        {
            result.errors.push_back( "capabilities.gpu must be a string or object" );
        }
    }

    const Json::Value &workspace = json[ "workspace" ];
    if ( !workspace.isNull() )
    {
        if ( workspace.isObject() )
        {
            if ( !asBool( workspace.get( "mutate", Json::Value() ), caps.workspaceMutation )
                 && !workspace[ "mutate" ].isNull() )
                result.errors.push_back( "capabilities.workspace.mutate must be a boolean" );
        }
        else
        {
            result.errors.push_back( "capabilities.workspace must be an object" );
        }
    }

    const Json::Value &project = json[ "project" ];
    if ( !project.isNull() )
    {
        if ( project.isObject() )
        {
            if ( !asBool( project.get( "mutate", Json::Value() ), caps.projectMutation )
                 && !project[ "mutate" ].isNull() )
                result.errors.push_back( "capabilities.project.mutate must be a boolean" );
        }
        else
        {
            result.errors.push_back( "capabilities.project must be an object" );
        }
    }

    const Json::Value &modelProvider = json[ "modelProvider" ];
    if ( !modelProvider.isNull() )
    {
        if ( modelProvider.isObject() )
        {
            const Json::Value &frameworks = modelProvider[ "frameworks" ];
            if ( !frameworks.isNull() )
            {
                bool ok = false;
                caps.modelFrameworks = parsePathList( frameworks, ok );
                if ( !ok )
                    result.errors.push_back(
                        "capabilities.modelProvider.frameworks must be an array of strings" );
            }
        }
        else
        {
            result.errors.push_back( "capabilities.modelProvider must be an object" );
        }
    }

    if ( !asBool( json.get( "ui", Json::Value() ), caps.uiContribution )
         && !json[ "ui" ].isNull() )
        result.errors.push_back( "capabilities.ui must be a boolean" );

    if ( !asBool( json.get( "destructive", Json::Value() ), caps.destructive )
         && !json[ "destructive" ].isNull() )
        result.errors.push_back( "capabilities.destructive must be a boolean" );

    return result;
}

} // namespace exprs
