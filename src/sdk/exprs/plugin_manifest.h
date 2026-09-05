/***************************************************************************
 * exprs/plugin_manifest.h — ExpRS Plugin Manifest v1
 *
 * A plugin is a directory containing plugin.json (the manifest) plus its
 * payload. The manifest is the authoritative discovery index: the host can
 * enumerate every contribution a plugin offers WITHOUT dlopening any
 * binary, which keeps startup and catalog enumeration cheap. Binary-loaded
 * descriptors are cross-checked against the manifest by the conformance
 * kit.
 *
 * v1 manifest layout (all unknown optional fields are ignored):
 * {
 *   "manifest_version": 1,
 *   "id": "org.example.plugin",              // reverse-DNS, lowercase
 *   "name": "Example Plugin",
 *   "version": "1.0.0",                       // semver
 *   "api_version": "3.0",                     // EXP_RS_PLUGIN_API_VERSION
 *   "abi_version": 1,                         // EXP_RS_PLUGIN_ABI_VERSION
 *   "description": "...", "vendor": "...", "license": "...",
 *   "platforms": ["linux"],                   // optional allow-list
 *   "entrypoint": "libdemo.so",               // native payload (optional)
 *   "entrypoint_kind": "native",              // native | python | manifest
 *   "python": {"module": "...", "package": "..."},   // python plugins
 *   "capabilities": ["operator"],
 *   "permissions": ["filesystem_read"],
 *   "dependencies": ["org.other@^1.0"],
 *   "operators": [ ManifestOperatorV1 ... ],
 *   "data_providers": [ ... ], "model_runtimes": [ ... ],
 *   "agent_tools": [ ... ], "ui": {...}, "cartography": {...}
 * }
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_permissions.h"

namespace exprs {

/// Port descriptor inside a manifest contribution. Mirrors the subset of
/// processing::PortDescriptor that third parties need for discovery-time
/// schema rendering. "type" uses the AlgorithmDescriptor DataType strings
/// (any/raster/vector/table/number/integer/string/boolean/enum/bbox/crs/json).
struct ManifestPort
{
    std::string name;
    std::string type = "any";
    bool required = false;
    std::string description;
    Json::Value defaultValue;
    Json::Value enumOptions;   // optional array
    bool hasMin = false;
    bool hasMax = false;
    double minValue = 0.0;
    double maxValue = 0.0;
    std::string fileFormat;    // optional extension hint ("tif", "gpkg", ...)

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestPort &out, std::string &error );
};

/// Static (manifest-declared) external tool operator. Pure-manifest plugins
/// wrap command-line tools without shipping compiled code; the host binds
/// them to exprs::ExternalProcessOperator instances.
struct ManifestExternalTool
{
    /// argv template. Elements may reference parameters via "${name}"
    /// (string substitution from the params object at run time).
    std::vector<std::string> argv;
    /// Optional explicit environment additions; inherited env is filtered to
    /// a safe baseline unless "inherit_environment" is true.
    Json::Value environment;             // object name -> value
    bool inheritEnvironment = false;
    std::string workingDirectoryParam;   // name of a params key holding the cwd, or absolute path
    int timeoutSeconds = 3600;
    int stdoutLimitBytes = 8 * 1024 * 1024;
    int stderrLimitBytes = 1 * 1024 * 1024;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestExternalTool &out, std::string &error );
};

/// One operator contribution.
struct ManifestOperator
{
    std::string id;                 // "vendor:name"
    std::string displayName;
    std::string group;
    std::string description;
    std::string memoryPolicy = "full_raster";
    std::string determinismGrade = "bit_exact";
    bool supportsCancel = true;
    Json::Value schema;             // inline parameter JSON schema (optional but recommended)
    std::vector<ManifestPort> inputs;
    std::vector<ManifestPort> outputs;
    Json::Value metadata;           // agent metadata (optional)
    /// Present when this is a pure-manifest external tool operator.
    bool hasExternalTool = false;
    ManifestExternalTool external;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestOperator &out, std::string &error );
};

struct ManifestDataProvider
{
    std::string id;              // "vendor:store"
    std::string displayName;
    std::string description;
    std::vector<std::string> schemes;   // URI schemes handled, e.g. "mydb://"
    Json::Value capabilities;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestDataProvider &out, std::string &error );
};

struct ManifestModelRuntime
{
    std::string framework;       // key registered into ModelRuntimeRegistry
    std::string displayName;
    std::string description;
    bool gpu = false;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestModelRuntime &out, std::string &error );
};

struct ManifestAgentTool
{
    std::string id;              // "vendor:tool"
    std::string displayName;
    std::string category;        // Custom unless declared otherwise
    std::string description;
    Json::Value inputSchema;     // JSON Schema object (required)
    Json::Value outputSchema;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestAgentTool &out, std::string &error );
};

struct ManifestUi
{
    bool dock = false;           // contributes createDockWidget()
    bool menuActions = false;    // contributes menu actions
    bool settingsPage = false;   // contributes a settings page
    std::string dockTitle;
    std::string settingsPageTitle;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestUi &out, std::string &error );
};

struct ManifestCartography
{
    bool layoutItems = false;
    Json::Value layoutItemIds;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestCartography &out, std::string &error );
};

struct ManifestPythonSection
{
    std::string module;          // importable module name
    std::string package;         // directory name of the payload

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, ManifestPythonSection &out, std::string &error );
};

enum class PluginEntrypointKind
{
    Native,    // shared library exporting EXPRS_createPluginV1
    Python,    // python module loaded inside the isolated worker
    Manifest,  // pure manifest (external tools only), no code payload
};

std::string entrypointKindName( PluginEntrypointKind kind );
bool entrypointKindFromName( const std::string &name, PluginEntrypointKind &out );

/// Parsed plugin manifest.
struct PluginManifest
{
    int manifestVersion = 0;
    std::string id;
    std::string name;
    std::string version;
    std::string apiVersion;
    int abiVersion = 0;
    std::string description;
    std::string vendor;
    std::string license;
    std::vector<std::string> platforms;
    std::string entrypoint;                 // file name relative to plugin dir
    PluginEntrypointKind entrypointKind = PluginEntrypointKind::Native;
    ManifestPythonSection python;
    std::vector<std::string> capabilities;
    std::vector<PluginPermission> permissions;
    std::vector<std::string> warnings;      // non-fatal parse notes
    std::vector<std::string> dependencies;  // "org.other@^1.0"
    std::vector<ManifestOperator> operators;
    std::vector<ManifestDataProvider> dataProviders;
    std::vector<ManifestModelRuntime> modelRuntimes;
    std::vector<ManifestAgentTool> agentTools;
    ManifestUi ui;
    ManifestCartography cartography;
    bool hasUi = false;
    bool hasCartography = false;

    bool hasCapability( const std::string &capability ) const;
    Json::Value permissionsToJson() const;

    Json::Value toJson() const;
    /// Parses from a JSON object. Returns false and appends an error
    /// diagnostic when the document cannot be structurally parsed.
    static bool fromJson( const Json::Value &json, PluginManifest &out, PluginDiagnostic &error );
};

/// Reads and parses <pluginDir>/plugin.json.
bool loadManifestFromFile( const std::string &manifestPath, PluginManifest &out,
                           PluginDiagnostic &error );

} // namespace exprs
