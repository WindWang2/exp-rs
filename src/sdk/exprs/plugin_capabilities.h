/***************************************************************************
 * exprs/plugin_capabilities.h — structured capability declarations
 *
 * A manifest may declare (optional, manifest v1 additive field "access" —
 * the name "capabilities" was already taken by the contribution-kind list):
 *
 *   "access": {
 *     "filesystem": { "read": ["${workspace}/inputs"], "write": ["${temp}"] },
 *     "network": true,
 *     "externalProcess": true,
 *     "gpu": { "hint": "cuda" },
 *     "workspace": { "mutate": false },
 *     "project": { "mutate": false },
 *     "modelProvider": { "frameworks": ["onnx"] },
 *     "ui": true,
 *     "destructive": false
 *   }
 *
 * HONEST ENFORCEMENT BOUNDARY (docs/plugins/capabilities.md):
 *   - host-process runtime: filesystem roots, temp scoping and the
 *     external-process helper are enforced at the worker boundary; quotas
 *     (exprs/plugin_quotas.h) add hard OS-level caps (job objects).
 *   - in-process runtime: declaration + audit + diagnostics ONLY. Native
 *     code cannot be confined from itself; this is NOT an OS sandbox on
 *     either runtime. Network access of arbitrary in-process code is never
 *     claimed to be intercepted.
 *
 * Path placeholders (expanded by the host, never by the plugin):
 *   ${plugin}    the plugin install directory
 *   ${workspace} the configured workspace root ("" when unset: paths using
 *                it fail validation with WorkspaceEscape)
 *   ${temp}      the plugin-scoped temp directory
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace exprs {

struct PluginCapabilities
{
    std::vector<std::string> fsReadRoots;    ///< canonical read roots
    std::vector<std::string> fsWriteRoots;   ///< canonical write roots
    bool network = false;
    bool externalProcess = false;
    std::string gpuHint;                     ///< "" | "cuda" | "cpu" | vendor string
    bool workspaceMutation = false;
    bool projectMutation = false;
    std::vector<std::string> modelFrameworks; ///< model runtime frameworks
    bool uiContribution = false;
    bool destructive = false;

    Json::Value toJson() const;
};

struct PluginCapabilityParseResult
{
    PluginCapabilities capabilities;
    std::vector<std::string> warnings;  ///< non-fatal (unknown keys, defaults)
    std::vector<std::string> errors;    ///< fatal (path escapes, bad types)
    bool ok() const { return errors.empty(); }
};

/// Parses and validates the manifest "access" object (pass manifest.access;
/// a null object parses to deny-all defaults). @p pluginDir / @p
/// workspaceRoot / @p tempDir are the expansion targets; roots are
/// canonicalized (symlink-free generic form). A declared path that cannot
/// resolve is a validation ERROR (fail closed), not a warning.
PluginCapabilityParseResult parsePluginAccess(
    const Json::Value &access, const std::string &pluginDir,
    const std::string &workspaceRoot, const std::string &tempDir );

/// Expands ${plugin}/${workspace}/${temp} placeholders in one declared path
/// and canonicalizes it. Returns false when the declaration cannot resolve
/// (unknown placeholder, workspace needed but unset, empty result).
bool expandCapabilityPath( const std::string &declared, const std::string &pluginDir,
                           const std::string &workspaceRoot, const std::string &tempDir,
                           std::string &resolved, std::string &error );

/// True when @p candidate resolves (canonicalized; a not-yet-existing tail
/// is anchored at its deepest existing ancestor, exactly like declared
/// roots) INSIDE @p root. Empty roots contain nothing. Used by the worker
/// to gate host-provided paths (operator workDir) against the plugin's
/// declared write roots; a false result is a typed policy refusal at the
/// call site — never a silent pass.
bool pathIsWithinRoot( const std::string &candidate, const std::string &root,
                       std::string &resolvedCandidate );

} // namespace exprs
