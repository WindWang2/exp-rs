/***************************************************************************
 * exprs/plugin_diagnostics.h — structured plugin diagnostics
 *
 * Every plugin-system failure produces PluginDiagnostic records with a
 * stable code, never a bare "load failed". Codes are grouped:
 *
 *   E1xxx  manifest problems        (parse / required fields / formats)
 *   E2xxx  compatibility problems   (API / ABI / manifest version)
 *   E3xxx  content problems         (entrypoint / dependency / resources)
 *   E4xxx  runtime problems         (symbol / load / init / registration)
 *   E5xxx  policy problems          (permission denied / trust / disabled)
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace exprs {

/// Stable diagnostic codes. Values are part of the public contract; only
/// append.
enum class PluginDiagnosticCode
{
    None = 0,

    // E1xxx — manifest
    ManifestUnreadable = 1001,
    ManifestInvalidJson = 1002,
    ManifestMissingField = 1003,
    ManifestInvalidField = 1004,
    ManifestUnknownVersion = 1005,
    ManifestDuplicateId = 1006,
    ManifestEntrypointMismatch = 1007,

    // E2xxx — compatibility
    ApiVersionMismatch = 2001,
    AbiVersionMismatch = 2002,
    ManifestVersionMismatch = 2003,
    PlatformUnsupported = 2004,

    // E3xxx — content
    EntrypointMissing = 3001,
    EntrypointNotLibrary = 3002,
    DependencyUnresolved = 3003,
    DependencyCycle = 3004,
    ResourceMissing = 3005,
    ContributionIdConflict = 3006,

    // E4xxx — runtime
    SymbolMissing = 4001,
    LibraryLoadFailed = 4002,
    InitializationFailed = 4003,
    RegistrationFailed = 4004,

    // E5xxx — policy
    PermissionDenied = 5001,
    TrustRejected = 5002,
    PluginDisabled = 5003,
    PolicyBlocklisted = 5004,
};

enum class PluginDiagnosticSeverity
{
    Info,
    Warning,
    Error,
};

/// One structured diagnostic record.
struct PluginDiagnostic
{
    PluginDiagnosticCode code = PluginDiagnosticCode::None;
    PluginDiagnosticSeverity severity = PluginDiagnosticSeverity::Error;
    std::string message;
    std::string pluginId;   ///< plugin this record belongs to ("" = global)
    std::string field;      ///< manifest field or entrypoint symbol concerned
    std::string file;       ///< file path concerned (manifest / library)

    Json::Value toJson() const;
    static PluginDiagnostic fromJson( const Json::Value &json );

    /// Stable machine-readable code string, e.g. "E2001".
    static std::string codeString( PluginDiagnosticCode code );
    static PluginDiagnosticCode codeFromString( const std::string &text );

    static const char *severityString( PluginDiagnosticSeverity severity );
};

/// Collects diagnostics for a discovery/load pass.
class PluginDiagnosticLog
{
public:
    void add( PluginDiagnostic diagnostic );
    void add( PluginDiagnosticCode code, PluginDiagnosticSeverity severity,
              const std::string &message, const std::string &pluginId = {},
              const std::string &field = {}, const std::string &file = {} );
    void merge( const PluginDiagnosticLog &other );

    const std::vector<PluginDiagnostic> &items() const { return mItems; }
    bool hasErrors() const;
    bool hasErrorsFor( const std::string &pluginId ) const;
    std::vector<PluginDiagnostic> forPlugin( const std::string &pluginId ) const;
    void clear() { mItems.clear(); }

    /// JSON array form (used by CLI --json output).
    Json::Value toJson() const;

private:
    std::vector<PluginDiagnostic> mItems;
};

} // namespace exprs
