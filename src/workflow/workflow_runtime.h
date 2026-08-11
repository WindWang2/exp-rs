// src/workflow/workflow_runtime.h
#pragma once

#include "workflow_definition.h"
#include "workflow_session.h"
#include "workflow_types.h"

#include <json/json.h>

#include <atomic>
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

    /// Request cooperative cancellation of a session's current operator step
    /// (observed via RSOperatorContext::throwIfCancelled()). Safe from any
    /// thread; cancellation is best-effort for operators that poll the flag.
    void requestCancel( const std::string &sessionId );

    void close( const std::string &sessionId );

  private:
    WorkflowSession *sessionMut( const std::string &sessionId );
    const WorkflowSession *sessionConst( const std::string &sessionId ) const;

    /// Per-session cooperative cancellation flag (created in open()).
    std::shared_ptr<std::atomic<bool>> cancelFlag( const std::string &sessionId );

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, WorkflowDefinition> m_defs;
    std::unordered_map<std::string, std::unique_ptr<WorkflowSession>> m_sessions;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> m_cancelFlags;
    int m_nextId = 1;
};

} // namespace sicnu::workflow
