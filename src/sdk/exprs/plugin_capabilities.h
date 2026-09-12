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

/// Machine-readable honesty levels for the declaration-vs-enforcement
/// matrix below. The names are part of the public contract: doctor output,
/// `plugin inspect --json` and the debug bundle quote them verbatim, and
/// docs/plugins/capabilities.md is generated FROM this table (never the
/// other way round).
enum class CapabilityEnforcementLevel
{
    EnforcedHost,        ///< refused/limited in the launcher process
    EnforcedWorker,      ///< refused in the worker at the host-provided seam
    EnforcedOs,          ///< kernel-enforced bound (platform nuances in note)
    Advisory,            ///< surfaced + logged, never enforced
    AuditOnly,           ///< recorded in diagnostics, no gate
    RefusedByContract,   ///< documented non-goal; we never claim it
};

std::string capabilityEnforcementLevelName( CapabilityEnforcementLevel level );

struct CapabilityEnforcementEntry
{
    const char *capability;    ///< stable id ("filesystem.workDir", "quotas.workerMemoryBytes", ...)
    const char *runtimeScope;  ///< "in-process" | "host-process" | "all"
    CapabilityEnforcementLevel level;
    const char *note;          ///< boundaries and platform nuances, honest wording
};

/// The capability enforcement matrix — one source of truth shared by the
/// docs, doctor, inspect and the debug bundle. Order is stable (manifest
/// grouping) so consumers can diff output.
std::vector<CapabilityEnforcementEntry> pluginCapabilityEnforcementMatrix();

/// JSON projection of the matrix: [{"capability","runtimeScope","level","note"}].
Json::Value pluginCapabilityEnforcementMatrixJson();

/// True when the manifest declares an "access" object at all. The 9.0
/// capability gates (model frameworks, explicit ui:false, explicit
/// externalProcess:false, provider schemes) enforce EXPLICIT declarations
/// only — manifests without an access object keep every pre-9.0 behavior
/// (deny-by-default is never invented for undeclared policy).
bool manifestDeclaresAccess( const Json::Value &access );

/// True when @p framework may be served under the declared access model:
/// a manifest without an access object allows everything (compatibility);
/// a declared modelProvider.frameworks array bounds the plugin to it (an
/// EMPTY declared array means the plugin deliberately serves nothing).
bool modelFrameworkAllowed( const Json::Value &access, const std::string &framework );

/// Tri-state for explicit boolean access declarations:
///   1 = declared true, 0 = declared false, -1 = not declared.
/// Gates act only on DECLARED values (0); undeclared never gates.
int accessBool( const Json::Value &access, const char *key );

} // namespace exprs
