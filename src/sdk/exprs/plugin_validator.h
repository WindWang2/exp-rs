/***************************************************************************
 * exprs/plugin_validator.h — semantic validation of plugin manifests
 *
 * Structural parsing lives in plugin_manifest.cpp; this validator layer
 * produces the human/machine-readable verdict: is this plugin loadable on
 * this host, and if not, exactly why (see PluginDiagnosticCode groups).
 ***************************************************************************/
#pragma once

#include <string>
#include <vector>

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_manifest.h"
#include "exprs/version.h"

namespace exprs {

struct PluginValidationRequest
{
    /// Directory containing plugin.json (used to check the entrypoint file).
    std::string pluginDir;
    /// Host versions for the compatibility gate. Defaults to the SDK the
    /// host was built against.
    PluginApiVersion hostApi{ pluginApiVersion() };
    int hostAbi = pluginAbiVersion();
    /// Treat the plugin as coming from this origin (affects trust checks).
    std::string trust = "third-party";
};

class PluginManifestValidator
{
public:
    /// Validates @p manifest. Returns true when the plugin is loadable;
    /// diagnostics always carry the full findings (errors + warnings).
    static bool validate( const PluginManifest &manifest, const PluginValidationRequest &request,
                          PluginDiagnosticLog &diagnostics );

    /// Reverse-DNS plugin id check ("org.example.plugin").
    static bool isValidPluginId( const std::string &id );
    /// "vendor:name" contribution id check.
    static bool isValidContributionId( const std::string &id );
    /// Loose semver check (major[.minor[.patch]] with numeric parts).
    static bool isValidSemver( const std::string &version );
    /// Dependency spec check: "<plugin-id>" or "<plugin-id>@<semver-range>".
    static bool isValidDependencySpec( const std::string &spec, std::string &outId,
                                       std::string &outRange );
};

} // namespace exprs
