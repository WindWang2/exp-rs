/***************************************************************************
 * exprs/plugin_diagnostics.cpp
 ***************************************************************************/
#include "exprs/plugin_diagnostics.h"

#include <algorithm>

namespace exprs {

namespace {
struct CodeEntry
{
    PluginDiagnosticCode code;
    const char *text;
};

const CodeEntry kCodeTable[] = {
    { PluginDiagnosticCode::None, "E0000" },
    { PluginDiagnosticCode::ManifestUnreadable, "E1001" },
    { PluginDiagnosticCode::ManifestInvalidJson, "E1002" },
    { PluginDiagnosticCode::ManifestMissingField, "E1003" },
    { PluginDiagnosticCode::ManifestInvalidField, "E1004" },
    { PluginDiagnosticCode::ManifestUnknownVersion, "E1005" },
    { PluginDiagnosticCode::ManifestDuplicateId, "E1006" },
    { PluginDiagnosticCode::ManifestEntrypointMismatch, "E1007" },
    { PluginDiagnosticCode::ApiVersionMismatch, "E2001" },
    { PluginDiagnosticCode::AbiVersionMismatch, "E2002" },
    { PluginDiagnosticCode::ManifestVersionMismatch, "E2003" },
    { PluginDiagnosticCode::PlatformUnsupported, "E2004" },
    { PluginDiagnosticCode::EntrypointMissing, "E3001" },
    { PluginDiagnosticCode::EntrypointNotLibrary, "E3002" },
    { PluginDiagnosticCode::DependencyUnresolved, "E3003" },
    { PluginDiagnosticCode::DependencyCycle, "E3004" },
    { PluginDiagnosticCode::ResourceMissing, "E3005" },
    { PluginDiagnosticCode::ContributionIdConflict, "E3006" },
    { PluginDiagnosticCode::SymbolMissing, "E4001" },
    { PluginDiagnosticCode::LibraryLoadFailed, "E4002" },
    { PluginDiagnosticCode::InitializationFailed, "E4003" },
    { PluginDiagnosticCode::RegistrationFailed, "E4004" },
    { PluginDiagnosticCode::PermissionDenied, "E5001" },
    { PluginDiagnosticCode::TrustRejected, "E5002" },
    { PluginDiagnosticCode::PluginDisabled, "E5003" },
    { PluginDiagnosticCode::PolicyBlocklisted, "E5004" },
};
} // namespace

Json::Value PluginDiagnostic::toJson() const
{
    Json::Value json( Json::objectValue );
    json["code"] = codeString( code );
    json["severity"] = severityString( severity );
    json["message"] = message;
    if ( !pluginId.empty() )
        json["plugin"] = pluginId;
    if ( !field.empty() )
        json["field"] = field;
    if ( !file.empty() )
        json["file"] = file;
    return json;
}

PluginDiagnostic PluginDiagnostic::fromJson( const Json::Value &json )
{
    PluginDiagnostic diagnostic;
    diagnostic.code = codeFromString( json.get( "code", "E0000" ).asString() );
    const std::string severity = json.get( "severity", "error" ).asString();
    if ( severity == "info" )
        diagnostic.severity = PluginDiagnosticSeverity::Info;
    else if ( severity == "warning" )
        diagnostic.severity = PluginDiagnosticSeverity::Warning;
    else
        diagnostic.severity = PluginDiagnosticSeverity::Error;
    diagnostic.message = json.get( "message", "" ).asString();
    diagnostic.pluginId = json.get( "plugin", "" ).asString();
    diagnostic.field = json.get( "field", "" ).asString();
    diagnostic.file = json.get( "file", "" ).asString();
    return diagnostic;
}

std::string PluginDiagnostic::codeString( PluginDiagnosticCode code )
{
    for ( const CodeEntry &entry : kCodeTable )
    {
        if ( entry.code == code )
            return entry.text;
    }
    return "E0000";
}

PluginDiagnosticCode PluginDiagnostic::codeFromString( const std::string &text )
{
    for ( const CodeEntry &entry : kCodeTable )
    {
        if ( text == entry.text )
            return entry.code;
    }
    return PluginDiagnosticCode::None;
}

const char *PluginDiagnostic::severityString( PluginDiagnosticSeverity severity )
{
    switch ( severity )
    {
    case PluginDiagnosticSeverity::Info:
        return "info";
    case PluginDiagnosticSeverity::Warning:
        return "warning";
    case PluginDiagnosticSeverity::Error:
        return "error";
    }
    return "error";
}

void PluginDiagnosticLog::add( PluginDiagnostic diagnostic )
{
    mItems.push_back( std::move( diagnostic ) );
}

void PluginDiagnosticLog::add( PluginDiagnosticCode code, PluginDiagnosticSeverity severity,
                               const std::string &message, const std::string &pluginId,
                               const std::string &field, const std::string &file )
{
    PluginDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.message = message;
    diagnostic.pluginId = pluginId;
    diagnostic.field = field;
    diagnostic.file = file;
    mItems.push_back( std::move( diagnostic ) );
}

void PluginDiagnosticLog::merge( const PluginDiagnosticLog &other )
{
    mItems.insert( mItems.end(), other.mItems.begin(), other.mItems.end() );
}

bool PluginDiagnosticLog::hasErrors() const
{
    return std::any_of( mItems.begin(), mItems.end(), []( const PluginDiagnostic &item ) {
        return item.severity == PluginDiagnosticSeverity::Error;
    } );
}

bool PluginDiagnosticLog::hasErrorsFor( const std::string &pluginId ) const
{
    return std::any_of( mItems.begin(), mItems.end(), [&]( const PluginDiagnostic &item ) {
        return item.pluginId == pluginId && item.severity == PluginDiagnosticSeverity::Error;
    } );
}

std::vector<PluginDiagnostic> PluginDiagnosticLog::forPlugin( const std::string &pluginId ) const
{
    std::vector<PluginDiagnostic> result;
    for ( const PluginDiagnostic &item : mItems )
    {
        if ( item.pluginId == pluginId )
            result.push_back( item );
    }
    return result;
}

Json::Value PluginDiagnosticLog::toJson() const
{
    Json::Value array( Json::arrayValue );
    for ( const PluginDiagnostic &item : mItems )
        array.append( item.toJson() );
    return array;
}

} // namespace exprs
