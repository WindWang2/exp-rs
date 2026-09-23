// src/repair_planner/repair_view.cpp
#include "repair_view.h"

namespace sicnu::repair {

namespace {

/// The closed executable-key vocabulary: a candidate's runnable surface.
/// Everything else (risk, cost, before/after semantics, assumptions,
/// information loss, refusal causes) stays visible in every view.
const char *const kExecutableKeys[] = { "params", "operator_id", "action_key" };

Json::Value stripExecutableKeys( const Json::Value &node )
{
    if ( node.isObject() )
    {
        Json::Value copy( Json::objectValue );
        for ( const std::string &key : node.getMemberNames() )
        {
            bool executable = false;
            for ( const char *executableKey : kExecutableKeys )
            {
                if ( key == executableKey )
                {
                    executable = true;
                    break;
                }
            }
            if ( !executable )
                copy[key] = stripExecutableKeys( node[key] );
        }
        return copy;
    }
    if ( node.isArray() )
    {
        Json::Value copy( Json::arrayValue );
        for ( const Json::Value &entry : node )
            copy.append( stripExecutableKeys( entry ) );
        return copy;
    }
    return node;
}

bool isRepairPlanEnvelope( const Json::Value &doc, RepairError &error )
{
    if ( !doc.isObject() )
    {
        error = RepairError{ "invalid_document", "repair plan must be a JSON object" };
        return false;
    }
    if ( doc.get( "kind", "" ).asString() != kKind )
    {
        error = RepairError{ "invalid_document",
                             std::string( "envelope kind must be '" ) + kKind + "'" };
        return false;
    }
    if ( doc.get( "schema_version", "" ).asString() != kSchemaVersion )
    {
        error = RepairError{ "unsupported_version",
                             "unsupported repair_plan schema version" };
        return false;
    }
    return true;
}

Json::Value viewEnvelope( const Json::Value &planDoc, const char *viewName,
                          bool stripExecutable )
{
    Json::Value view = stripExecutable ? stripExecutableKeys( planDoc ) : planDoc;
    view["view"] = viewName;
    view["planning_only"] = true;
    return view;
}

} // namespace

bool teachingRepairView( const Json::Value &planDoc, Json::Value &view, RepairError &error )
{
    if ( !isRepairPlanEnvelope( planDoc, error ) )
        return false;
    view = viewEnvelope( planDoc, "teaching", true );
    return true;
}

bool agentRepairView( const Json::Value &planDoc, Json::Value &view, RepairError &error )
{
    if ( !isRepairPlanEnvelope( planDoc, error ) )
        return false;
    view = viewEnvelope( planDoc, "agent", false );
    return true;
}

} // namespace sicnu::repair
