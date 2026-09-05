/***************************************************************************
 * exprs/workflow_builder.cpp
 ***************************************************************************/
#include "exprs/workflow_builder.h"

#include "exprs/workflow_schema.h"

namespace exprs {

WorkflowStepBuilder &WorkflowStepBuilder::withTitle( const std::string &title )
{
    ( *mStep )["title"] = title;
    return *this;
}

WorkflowStepBuilder &WorkflowStepBuilder::withKind( const std::string &kind )
{
    ( *mStep )["kind"] = kind;
    return *this;
}

WorkflowStepBuilder &WorkflowStepBuilder::withParam( const std::string &name,
                                                     const Json::Value &value )
{
    Json::Value &step = *mStep;
    if ( !step.isMember( "params" ) )
        step["params"] = Json::Value( Json::objectValue );
    step["params"][name] = value;
    return *this;
}

WorkflowStepBuilder &WorkflowStepBuilder::withParams( const Json::Value &object )
{
    if ( !object.isObject() )
        return *this;
    for ( const std::string &name : object.getMemberNames() )
        withParam( name, object[name] );
    return *this;
}

WorkflowStepBuilder &WorkflowStepBuilder::withInput( const std::string &port,
                                                     const std::string &source )
{
    Json::Value &step = *mStep;
    if ( !step.isMember( "inputs" ) )
        step["inputs"] = Json::Value( Json::arrayValue );
    Json::Value input( Json::objectValue );
    input["port"] = port;
    input["source"] = source;
    step["inputs"].append( input );
    return *this;
}

WorkflowStepBuilder &WorkflowStepBuilder::withGate( const std::string &require,
                                                    const std::string &hint )
{
    Json::Value &step = *mStep;
    if ( !step.isMember( "gates" ) )
        step["gates"] = Json::Value( Json::arrayValue );
    Json::Value gate( Json::objectValue );
    gate["require"] = require;
    if ( !hint.empty() )
        gate["hint"] = hint;
    step["gates"].append( gate );
    return *this;
}

WorkflowStepBuilder &WorkflowStepBuilder::withArtifactOnSuccess( const std::string &port )
{
    ( *mStep )["artifact_on_success"] = port;
    return *this;
}

WorkflowStepBuilder &WorkflowStepBuilder::withResourceEstimateMb( int megabytes )
{
    ( *mStep )["resource_estimate_mb"] = megabytes;
    return *this;
}

WorkflowBuilder::WorkflowBuilder( std::string id, std::string title )
{
    mDocument = Json::Value( Json::objectValue );
    mDocument["schema_version"] = kWorkflowSchemaVersion;
    mDocument["id"] = std::move( id );
    mDocument["title"] = std::move( title );
    mDocument["steps"] = Json::Value( Json::arrayValue );
}

WorkflowStepBuilder WorkflowBuilder::addStep( const std::string &id,
                                              const std::string &operatorId )
{
    WorkflowStepBuilder builder = addStepOfKind( id, "operator" );
    builder.json()["operator"] = operatorId;
    return builder;
}

WorkflowStepBuilder WorkflowBuilder::addStepOfKind( const std::string &id,
                                                    const std::string &kind )
{
    Json::Value step( Json::objectValue );
    step["id"] = id;
    if ( kind != "operator" )
        step["kind"] = kind;
    Json::Value &steps = mDocument["steps"];
    steps.append( step );
    return WorkflowStepBuilder( &steps[steps.size() - 1] );
}

WorkflowBuilder &WorkflowBuilder::withWorkspaceKind( const std::string &kind )
{
    mDocument["workspace_kind"] = kind;
    return *this;
}

WorkflowBuilder &WorkflowBuilder::withMetadata( const std::string &name,
                                                const Json::Value &value )
{
    if ( !mDocument.isMember( "metadata" ) )
        mDocument["metadata"] = Json::Value( Json::objectValue );
    mDocument["metadata"][name] = value;
    return *this;
}

Json::Value WorkflowBuilder::toJson() const
{
    return mDocument;
}

Json::Value WorkflowBuilder::toValidatedJson( PluginDiagnosticLog &log ) const
{
    Json::Value copy = mDocument;
    if ( !validateWorkflowDocument( copy, log ) )
        return Json::Value( Json::nullValue );
    return copy;
}

} // namespace exprs
