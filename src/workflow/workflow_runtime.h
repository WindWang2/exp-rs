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

namespace sicnu::data {
class DataManager;
}

namespace sicnu::workflow {

/// Process-local workflow definition manager and session orchestrator.
class WorkflowRuntime
{
  public:
    explicit WorkflowRuntime( bool loadBuiltins = true );

    void registerDefinition( WorkflowDefinition def );
    bool hasDefinition( const std::string &id ) const;
    const WorkflowDefinition *findDefinition( const std::string &id ) const;
    std::shared_ptr<const WorkflowDefinition> findDefinitionShared( const std::string &id ) const;
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
    /// Default path is async via ExecutionPlane/TaskCenter with transactional
    /// commit and OutputVerifier; falls back to the original synchronous
    /// RSOperator path when the plane is disabled or unavailable.
    Json::Value runStep( const std::string &sessionId, const std::string &stepId );

    void markStepComplete( const std::string &sessionId, const std::string &stepId );

    /// Record a pure session artifact (path/value) for soft GateDef hasArtifact:*.
    void setArtifact( const std::string &sessionId, const std::string &name, const std::string &value );

    /// Request cooperative cancellation of a session's current operator step
    /// (observed via RSOperatorContext::throwIfCancelled() and via
    /// TaskCenter cancellation for ExecutionPlane runs). Safe from any
    /// thread; cancellation is best-effort for operators that poll the flag.
    void requestCancel( const std::string &sessionId );

    void close( const std::string &sessionId );

    /// Optional DataManager for transactional output commit (OutputCommitter).
    /// When null, outputs are used directly without stable-asset promotion
    /// (verification still runs on the temp path if present).
    void setDataManager( sicnu::data::DataManager *dataManager );
    sicnu::data::DataManager *dataManager() const;

    /// Control the async ExecutionPlane path (default true). When false,
    /// runStep always uses the synchronous RSOperator fallback.
    void setUseExecutionPlane( bool use );
    bool useExecutionPlane() const;

  private:
    std::shared_ptr<WorkflowSession> session( const std::string &sessionId ) const;

    /// Per-session cooperative cancellation flag (created in open()).
    std::shared_ptr<std::atomic<bool>> cancelFlag( const std::string &sessionId );

    Json::Value runStepSync( const std::string &sessionId, const std::string &stepId,
                             const StepDef *step, const Json::Value &params,
                             std::shared_ptr<WorkflowSession> sessionPtr,
                             std::shared_ptr<std::atomic<bool>> cancelFlagPtr );

    Json::Value runStepViaExecutionPlane( const std::string &sessionId, const std::string &stepId,
                                          const StepDef *step, const Json::Value &params,
                                          std::shared_ptr<WorkflowSession> sessionPtr,
                                          std::shared_ptr<std::atomic<bool>> cancelFlagPtr );

    /// Roll back a committed stable asset after failed verification. Takes the
    /// caller's DataManager snapshot (#702): the runStep paths snapshot
    /// m_dataManager under m_mutex exactly once; re-reading the member here
    /// (unlocked) would be a formal data race with setDataManager.
    bool rollbackCommittedAsset( sicnu::data::DataManager *dataManager, const std::string &assetIdStr );

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<WorkflowDefinition>> m_defs;
    std::unordered_map<std::string, std::shared_ptr<WorkflowSession>> m_sessions;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> m_cancelFlags;
    std::unordered_map<std::string, long> m_activeTaskIds;
    sicnu::data::DataManager *m_dataManager = nullptr;
    bool m_useExecutionPlane = true;
    int m_nextId = 1;
};

} // namespace sicnu::workflow
