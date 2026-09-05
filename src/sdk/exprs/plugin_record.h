/***************************************************************************
 * exprs/plugin_record.h — lifecycle state of one discovered plugin
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_manifest.h"

namespace exprs {

enum class PluginState
{
    Discovered,   ///< manifest found and parsed; validation result in diagnostics
    Validated,    ///< passed validation; not yet loaded
    Incompatible, ///< API/ABI/platform gate failed
    Broken,       ///< structural/manifest errors
    Disabled,     ///< disabled by the user (plugins.index.json) or policy
    Blocked,      ///< blocked by host policy (allow/block list, trust)
    Loading,
    Loaded,
    Failed,       ///< load/initialize/registration failed at runtime
    Unloaded,     ///< was loaded, then unloaded cleanly
};

const char *pluginStateName( PluginState state );
bool pluginStateFromName( const std::string &name, PluginState &out );

/// Where the plugin was found.
enum class PluginOrigin
{
    Builtin,  ///< shipped with the application (always trusted)
    System,   ///< system-wide install root
    User,     ///< user plugin root (install/uninstall target)
    External, ///< ad-hoc directory (SICNU_PLUGIN_PATH / validate <path>)
};

const char *pluginOriginName( PluginOrigin origin );
bool pluginOriginFromName( const std::string &name, PluginOrigin &out );

/// One plugin tracked by the registry.
struct PluginRecord
{
    PluginManifest manifest;
    std::string directory;      ///< absolute path of the plugin directory
    std::string manifestPath;
    PluginState state = PluginState::Discovered;
    PluginOrigin origin = PluginOrigin::External;
    PluginDiagnosticLog diagnostics;

    const std::string &id() const { return manifest.id; }
    bool loadable() const
    {
        return state == PluginState::Validated || state == PluginState::Unloaded
               || state == PluginState::Loaded;
    }
    Json::Value toJson( bool includeDiagnostics = true ) const;
};

} // namespace exprs
