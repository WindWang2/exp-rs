/***************************************************************************
 * exprs/plugin_package.h — local plugin package management (Phase V)
 *
 * A plugin package is a directory containing plugin.json plus its payload
 * (native library, python package, resources, licenses). v1 packages are
 * plain directories (zip/tar bundles are a future format bump: the manifest
 * declares the format, so existing installs stay valid).
 *
 * install(): validates the manifest, then copies the package into the user
 * plugin root under <root>/<plugin-id>. Security:
 *   - every copied path is verified to stay inside source and target
 *     directories (zip-slip / symlink-escape protection; symlinks are
 *     rejected, not followed)
 *   - the target directory is refused if a DIFFERENT plugin id already
 *     occupies it (id takeover)
 ***************************************************************************/
#pragma once

#include <string>
#include <vector>

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_manifest.h"

namespace exprs {

class PluginPackage
{
public:
    /// Validates @p sourceDir as a plugin package and installs it into the
    /// user plugin root. Returns the installed directory on success.
    static bool install( const std::string &sourceDir, std::string &installedDir,
                         PluginDiagnosticLog &log );

    /// Removes <userRoot>/<pluginId>. Returns false when the plugin is not
    /// installed in the user root (builtin/system plugins are refused).
    static bool uninstall( const std::string &pluginId, PluginDiagnosticLog &log );

    /// Lists plugin ids currently installed in the user root.
    static std::vector<std::string> installedIds();
};

} // namespace exprs
