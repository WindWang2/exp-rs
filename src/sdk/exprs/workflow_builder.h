/***************************************************************************
 * exprs/workflow_builder.h — C++ Workflow Builder SDK (schema v1)
 *
 * Produces public workflow documents (exprs/workflow_schema.h). The builder
 * owns no execution state; run the finished document through the CLI
 * (`workflow run`), the WorkflowRunCoordinator, or the Python SDK.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <string>
#include <vector>

#include "exprs/plugin_diagnostics.h"

namespace exprs {

class WorkflowStepBuilder
{
public:
    /// Binds to a step inside the owning document (jsoncpp element
    /// references stay stable across sibling appends).
    explicit WorkflowStepBuilder( Json::Value *step ) : mStep( step ) {}

    WorkflowStepBuilder &withTitle( const std::string &title );
    WorkflowStepBuilder &withKind( const std::string &kind );   ///< operator|interactive|review|composite
    WorkflowStepBuilder &withParam( const std::string &name, const Json::Value &value );
    WorkflowStepBuilder &withParams( const Json::Value &object );
    /// Declares an input port wired to "<step>.<port>" or "${step.port}".
    WorkflowStepBuilder &withInput( const std::string &port, const std::string &source );
    WorkflowStepBuilder &withGate( const std::string &require, const std::string &hint = {} );
    WorkflowStepBuilder &withArtifactOnSuccess( const std::string &port );
    WorkflowStepBuilder &withResourceEstimateMb( int megabytes );

    Json::Value &json() const { return *mStep; }

private:
    Json::Value *mStep = nullptr;
};

class WorkflowBuilder
{
public:
    WorkflowBuilder( std::string id, std::string title );

    /// Adds an operator step. Returns a builder bound into this workflow.
    WorkflowStepBuilder addStep( const std::string &id, const std::string &operatorId );

    /// Adds a non-operator step (interactive/review/composite).
    WorkflowStepBuilder addStepOfKind( const std::string &id, const std::string &kind );

    WorkflowBuilder &withWorkspaceKind( const std::string &kind );
    WorkflowBuilder &withMetadata( const std::string &name, const Json::Value &value );

    /// Renders the public document (with schema_version stamped).
    Json::Value toJson() const;

    /// Renders + validates against the public schema. Returns empty value
    /// on validation failure (diagnostics in @p log).
    Json::Value toValidatedJson( PluginDiagnosticLog &log ) const;

private:
    Json::Value mDocument;
};

} // namespace exprs
