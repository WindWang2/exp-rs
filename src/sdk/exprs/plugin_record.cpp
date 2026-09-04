/***************************************************************************
 * exprs/plugin_registry.cpp — record state helpers (names + JSON)
 *
 * The PluginRegistry singleton lives in plugin_registry.h/.cpp; these
 * free-standing helpers are split out so tests and CLI code can render
 * plugin states without pulling the registry in.
 ***************************************************************************/
#include "exprs/plugin_record.h"

namespace exprs {

const char *pluginStateName( PluginState state )
{
    switch ( state )
    {
    case PluginState::Discovered:
        return "discovered";
    case PluginState::Validated:
        return "validated";
    case PluginState::Incompatible:
        return "incompatible";
    case PluginState::Broken:
        return "broken";
    case PluginState::Disabled:
        return "disabled";
    case PluginState::Blocked:
        return "blocked";
    case PluginState::Loading:
        return "loading";
    case PluginState::Loaded:
        return "loaded";
    case PluginState::Failed:
        return "failed";
    case PluginState::Unloaded:
        return "unloaded";
    }
    return "unknown";
}

bool pluginStateFromName( const std::string &name, PluginState &out )
{
    struct Entry
    {
        PluginState state;
        const char *name;
    };
    static const Entry kStates[] = {
        { PluginState::Discovered, "discovered" }, { PluginState::Validated, "validated" },
        { PluginState::Incompatible, "incompatible" }, { PluginState::Broken, "broken" },
        { PluginState::Disabled, "disabled" },         { PluginState::Blocked, "blocked" },
        { PluginState::Loading, "loading" },           { PluginState::Loaded, "loaded" },
        { PluginState::Failed, "failed" },             { PluginState::Unloaded, "unloaded" },
    };
    for ( const Entry &entry : kStates )
    {
        if ( name == entry.name )
        {
            out = entry.state;
            return true;
        }
    }
    return false;
}

const char *pluginOriginName( PluginOrigin origin )
{
    switch ( origin )
    {
    case PluginOrigin::Builtin:
        return "builtin";
    case PluginOrigin::System:
        return "system";
    case PluginOrigin::User:
        return "user";
    case PluginOrigin::External:
        return "external";
    }
    return "external";
}

bool pluginOriginFromName( const std::string &name, PluginOrigin &out )
{
    if ( name == "builtin" )
    {
        out = PluginOrigin::Builtin;
        return true;
    }
    if ( name == "system" )
    {
        out = PluginOrigin::System;
        return true;
    }
    if ( name == "user" )
    {
        out = PluginOrigin::User;
        return true;
    }
    if ( name == "external" )
    {
        out = PluginOrigin::External;
        return true;
    }
    return false;
}

Json::Value PluginRecord::toJson( bool includeDiagnostics ) const
{
    Json::Value json( Json::objectValue );
    json["manifest"] = manifest.toJson();
    json["directory"] = directory;
    json["manifest_path"] = manifestPath;
    json["state"] = pluginStateName( state );
    json["origin"] = pluginOriginName( origin );
    if ( includeDiagnostics )
        json["diagnostics"] = diagnostics.toJson();
    return json;
}

} // namespace exprs
