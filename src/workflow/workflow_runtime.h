// src/workflow/workflow_runtime.h
#pragma once

#include "workflow_registry.h"
#include "workflow_session.h"
#include "workflow_types.h"

#include <json/json.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace sicnu::workflow {

/// Process-local workflow session manager.
/// open/canRun/runStep are pure orchestration; operator work goes through WorkflowRunner.
class WorkflowRuntime
{
  public:
    explicit WorkflowRuntime( WorkflowRegistry &registry );

    /// Open a session for a registered definition.
    /// @return session id, or empty string if definition is missing
    std::string open( const std::string &definitionId );

    SessionSnapshot state( const std::string &sessionId ) const;

    bool gotoStep( const std::string &sessionId, const std::string &stepId );
    void setParams( const std::string &sessionId, const std::string &stepId, const Json::Value &params );

    CanRunResult canRun( const std::string &sessionId, const std::string &stepId ) const;

    /// Synchronous run for unit tests / headless use.
    /// @throws std::runtime_error if gates fail, step kind is not Operator, or operator fails
    Json::Value runStep( const std::string &sessionId, const std::string &stepId );

    void markStepComplete( const std::string &sessionId, const std::string &stepId );

    /// Record a pure session artifact (path/value) for soft GateDef hasArtifact:*.
    void setArtifact( const std::string &sessionId, const std::string &name, const std::string &value );

    void close( const std::string &sessionId );

  private:
    WorkflowSession *sessionMut( const std::string &sessionId );
    const WorkflowSession *sessionConst( const std::string &sessionId ) const;

    WorkflowRegistry &m_registry;
    std::unordered_map<std::string, std::unique_ptr<WorkflowSession>> m_sessions;
    int m_nextId = 1;
};

} // namespace sicnu::workflow
