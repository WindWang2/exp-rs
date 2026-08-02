// src/workflow/workflow_runtime.h
#pragma once

#include "workflow_definition.h"
#include "workflow_session.h"
#include "workflow_types.h"

#include <json/json.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::workflow {

/// Process-local workflow definition manager and session orchestrator.
class WorkflowRuntime
{
  public:
    explicit WorkflowRuntime( bool loadBuiltins = true );

    void registerDefinition( WorkflowDefinition def );
    bool hasDefinition( const std::string &id ) const;
    const WorkflowDefinition *findDefinition( const std::string &id ) const;
    std::vector<std::string> registeredDefinitionIds() const;

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

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, WorkflowDefinition> m_defs;
    std::unordered_map<std::string, std::unique_ptr<WorkflowSession>> m_sessions;
    int m_nextId = 1;
};

} // namespace sicnu::workflow
