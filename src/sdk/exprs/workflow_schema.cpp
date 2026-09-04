/***************************************************************************
 * exprs/workflow_schema.cpp
 ***************************************************************************/
#include "exprs/workflow_schema.h"

#include <set>

namespace exprs {

namespace {

void addError( PluginDiagnosticLog &log, PluginDiagnosticCode code, const std::string &message,
               const std::string &field )
{
    PluginDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = PluginDiagnosticSeverity::Error;
    diagnostic.message = message;
    diagnostic.field = field;
    log.add( diagnostic );
}

bool isValidStepKind( const std::string &kind )
{
    return kind.empty() || kind == "operator" || kind == "interactive" || kind == "review"
           || kind == "composite";
}

} // namespace

int workflowSchemaVersion( const Json::Value &document, PluginDiagnosticLog &diagnostics )
{
    if ( !document.isMember( "schema_version" ) )
        return 1;
    const Json::Value &version = document["schema_version"];
    if ( !version.isIntegral() || version.asInt() < 1 )
    {
        addError( diagnostics, PluginDiagnosticCode::ManifestUnknownVersion, "schema_version",
                  "'schema_version' must be a positive integer" );
        return 0;
    }
    return version.asInt();
}

bool validateWorkflowDocument( const Json::Value &document, PluginDiagnosticLog &diagnostics )
{
    if ( !document.isObject() )
    {
        addError( diagnostics, PluginDiagnosticCode::ManifestInvalidJson,
                  "workflow document root must be an object", "" );
        return false;
    }

    const int version = workflowSchemaVersion( document, diagnostics );
    if ( version == 0 )
        return false;
    if ( version > kWorkflowSchemaVersion )
    {
        addError( diagnostics, PluginDiagnosticCode::ManifestUnknownVersion,
                  "workflow schema_version " + std::to_string( version )
                      + " is newer than the supported version "
                      + std::to_string( kWorkflowSchemaVersion )
                      + "; migrate the document before use",
                  "schema_version" );
        return false;
    }

    bool ok = true;
    if ( !document.isMember( "steps" ) || !document["steps"].isArray() || document["steps"].empty() )
    {
        addError( diagnostics, PluginDiagnosticCode::ManifestMissingField,
                  "workflow document requires a non-empty 'steps' array", "steps" );
        return false;
    }

    std::set<std::string> stepIds;
    const Json::Value &steps = document["steps"];
    for ( Json::ArrayIndex index = 0; index < steps.size(); ++index )
    {
        const Json::Value &step = steps[index];
        if ( !step.isObject() )
        {
            addError( diagnostics, PluginDiagnosticCode::ManifestInvalidField,
                      "step at index " + std::to_string( index ) + " must be an object",
                      "steps" );
            ok = false;
            continue;
        }
        const std::string stepId = step.get( "id", "" ).asString();
        if ( stepId.empty() )
        {
            addError( diagnostics, PluginDiagnosticCode::ManifestMissingField,
                      "step at index " + std::to_string( index ) + " is missing 'id'",
                      "steps[].id" );
            ok = false;
        }
        else if ( !stepIds.insert( stepId ).second )
        {
            addError( diagnostics, PluginDiagnosticCode::ManifestInvalidField,
                      "duplicate step id '" + stepId + "'", "steps[].id" );
            ok = false;
        }

        const std::string kind = step.get( "kind", "operator" ).asString();
        if ( !isValidStepKind( kind ) )
        {
            addError( diagnostics, PluginDiagnosticCode::ManifestInvalidField,
                      "step '" + stepId + "' has unknown kind '" + kind + "'", "steps[].kind" );
            ok = false;
        }
        if ( kind.empty() || kind == "operator" )
        {
            const std::string operatorId = step.isMember( "operator" )
                                               ? step["operator"].asString()
                                               : ( step.isMember( "operatorId" )
                                                       ? step["operatorId"].asString()
                                                       : step.get( "name", "" ).asString() );
            if ( operatorId.empty() )
            {
                addError( diagnostics, PluginDiagnosticCode::ManifestMissingField,
                          "operator step '" + stepId + "' requires 'operator'",
                          "steps[].operator" );
                ok = false;
            }
        }
        if ( step.isMember( "params" ) && !step["params"].isObject() )
        {
            addError( diagnostics, PluginDiagnosticCode::ManifestInvalidField,
                      "step '" + stepId + "' 'params' must be an object", "steps[].params" );
            ok = false;
        }
    }
    return ok;
}

Json::Value migrateWorkflowDocument( const Json::Value &document, int targetVersion,
                                     PluginDiagnosticLog &diagnostics )
{
    const int currentVersion = workflowSchemaVersion( document, diagnostics );
    if ( currentVersion == 0 )
        return Json::Value( Json::nullValue );
    if ( currentVersion > targetVersion )
    {
        addError( diagnostics, PluginDiagnosticCode::ManifestUnknownVersion,
                  "cannot migrate workflow from schema_version "
                      + std::to_string( currentVersion ) + " down to "
                      + std::to_string( targetVersion ),
                  "schema_version" );
        return Json::Value( Json::nullValue );
    }
    // v1 is the baseline; upgrades append per-version steps here.
    Json::Value migrated = document;
    migrated["schema_version"] = targetVersion;
    return migrated;
}

void stampWorkflowSchemaVersion( Json::Value &document )
{
    if ( document.isObject() && !document.isMember( "schema_version" ) )
        document["schema_version"] = kWorkflowSchemaVersion;
}

} // namespace exprs
