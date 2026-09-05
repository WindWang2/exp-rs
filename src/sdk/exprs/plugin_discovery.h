/***************************************************************************
 * exprs/plugin_discovery.h — filesystem discovery of plugin directories
 *
 * A plugin directory is any directory containing a plugin.json directly
 * inside it. Discovery only reads manifests — it never dlopen binaries —
 * and caches parsed manifests per root in a manifest index
 * (<root>/.exprs-manifest-index.json) so large plugin directories do not
 * slow down startup (re-parse only when the manifest file changes).
 ***************************************************************************/
#pragma once

#include <string>
#include <vector>

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_record.h"

namespace exprs {

struct PluginDiscoveryOptions
{
    /// Absolute directories to scan, in priority order.
    std::vector<std::string> roots;
    /// Use the per-root manifest index cache (default true).
    bool useCache = true;
};

class PluginDiscovery
{
public:
    /// Default scan roots in load-priority order:
    ///   1. <appDir>/../plugins                 (builtin/application payload)
    ///   2. <installDataDir>/plugins            (system install root)
    ///   3. $HOME/.local/share/sicnu_geo_rs/plugins (user root)
    ///   4. entries of $SICNU_PLUGIN_PATH       (':'-separated, external)
    static std::vector<std::string> defaultRoots( const std::string &appDir = {},
                                                  const std::string &installDataDir = {} );

    /// The user plugin root (install/uninstall target).
    static std::string userPluginRoot();

    /// Scans @p options.roots and returns one record per plugin directory.
    /// Duplicate ids: the first root in priority order wins; the loser is
    /// diagnosed with ManifestDuplicateId. Manifests that fail to parse
    /// produce Broken records instead of being dropped.
    static std::vector<PluginRecord> scan( const PluginDiscoveryOptions &options,
                                           PluginDiagnosticLog &log );

    /// Validates one ad-hoc plugin directory (CLI `plugin validate <path>`).
    /// Never consults caches or roots.
    static PluginRecord inspectDirectory( const std::string &directory,
                                          PluginDiagnosticLog &log );
};

} // namespace exprs
