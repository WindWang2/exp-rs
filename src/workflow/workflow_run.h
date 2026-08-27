#pragma once

#include <json/json.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "workflow_types.h"

namespace sicnu::workflow {

enum class WorkflowRunState {
  Created,
  Planning,
  Ready,
  Running,
  WaitingResource,
  Interrupted,
  Cancelling,
  Canceled,
  Failed,
  Completed
};

std::string workflowRunStateToString( WorkflowRunState state );
WorkflowRunState workflowRunStateFromString( const std::string &str );
/// Strict parse: returns false for unknown state strings instead of
/// silently defaulting to Created (corrupt checkpoints must be rejected).
bool tryParseWorkflowRunState( const std::string &str, WorkflowRunState &out );
bool isTerminalRunState( WorkflowRunState state );
bool isValidRunStateTransition( WorkflowRunState from, WorkflowRunState to );

/// A run id is embedded into checkpoint filenames and must stay filename-safe:
/// non-empty, at most 128 chars, limited to [A-Za-z0-9._-] and must not start
/// with '.' (guards against ".." path traversal and absolute-path injection).
bool isValidRunId( const std::string &runId );

/// Serialization schema version stamped into every WorkflowRun JSON payload.
/// Loaders reject payloads that carry a different (or missing) version.
constexpr int kWorkflowRunSerializationVersion = 1;

struct StepPlan {
  std::string stepId;
  std::string operatorId;
  StepKind kind = StepKind::Operator;
  Json::Value rawParams;
  Json::Value resolvedParams;
  std::vector<std::string> dependencies;
  unsigned int resourceEstimateMb = 0;
  std::string fingerprint;
  bool cacheHit = false;
  std::string cachedOutputAssetId;
  std::string status = "Pending"; // Pending, Ready, Running, Completed, Failed, Canceled, Skipped
  long taskId = -1;
  Json::Value resultPayload;
  std::string outputLayerPath;
  std::string errorMessage;
  std::string startTime;
  std::string endTime;

  Json::Value toJson() const;
  /// @param error when non-null, receives a human-readable message when the
  /// payload is rejected (unknown fields are not an error; invalid enum
  /// values, e.g. an out-of-range kind, are).
  static StepPlan fromJson( const Json::Value &json, std::string *error = nullptr );
};

/// Workflow Engine 2.0 run aggregate.
///
/// Concurrency model: getters, setters, state transitions, serialization
/// (toJson) and the targeted step mutators are serialized by an internal
/// mutex, so a checkpoint save can never observe a half-applied transition
/// made through those methods. Two accessors deliberately escape the lock:
/// definition() (const reference) and findStepPlan() (mutable pointer) -
/// they assume the single-writer orchestration model and must not race with
/// structural mutation of the run.
class WorkflowRun {
public:
  WorkflowRun() = default;

  static std::unique_ptr<WorkflowRun> createFromDefinition( const WorkflowDefinition &def,
                                                            const std::string &runId = "" );

  std::string runId() const;
  /// Returns false and leaves the current id unchanged when @a id is invalid
  /// (see isValidRunId). The empty id is rejected here too - use the
  /// generated default from createFromDefinition instead.
  bool setRunId( const std::string &id );

  std::string workflowId() const;
  void setWorkflowId( const std::string &id );

  WorkflowRunState state() const;
  bool transitionTo( WorkflowRunState newState );
  void forceSetState( WorkflowRunState state );

  /// Escapes the internal lock: the reference is stable only while no
  /// setDefinition() call runs (single-writer model).
  const WorkflowDefinition &definition() const;
  void setDefinition( const WorkflowDefinition &def );

  std::vector<StepPlan> stepPlans() const;
  void setStepPlans( const std::vector<StepPlan> &plans );
  /// Copy-based lookup, safe under the internal mutex.
  std::optional<StepPlan> stepPlan( const std::string &stepId ) const;
  /// Replace the plan with a matching stepId. Returns false when no such
  /// step exists. Refreshes updatedAt/progress.
  bool updateStepPlan( const StepPlan &plan );
  /// Targeted status mutation (locks internally, refreshes updatedAt/progress).
  /// Returns false when no such step exists.
  bool setStepStatus( const std::string &stepId, const std::string &status );

  /// Escapes the internal lock: use only when a single thread owns the run
  /// (tests, orchestration setup). Prefer the copy/mutator API above.
  StepPlan *findStepPlan( const std::string &stepId );
  const StepPlan *findStepPlan( const std::string &stepId ) const;

  std::map<std::string, std::string> artifacts() const;
  void setArtifact( const std::string &name, const std::string &value );
  std::string artifact( const std::string &name ) const;

  std::string errorMessage() const;
  void setErrorMessage( const std::string &msg );

  double progress() const;
  void setProgress( double p );
  void recalculateProgress();

  std::string createdAt() const;
  void setCreatedAt( const std::string &ts );

  std::string updatedAt() const;
  void setUpdatedAt( const std::string &ts );

  Json::Value toJson() const;
  static std::unique_ptr<WorkflowRun> fromJson( const Json::Value &json, std::string &error );

private:
  StepPlan *findStepPlanLocked( const std::string &stepId );
  void touchLocked();
  void recalculateProgressLocked();

  mutable std::mutex m_mutex;
  std::string m_runId;
  std::string m_workflowId;
  WorkflowRunState m_state = WorkflowRunState::Created;
  WorkflowDefinition m_definition;
  std::vector<StepPlan> m_stepPlans;
  std::map<std::string, std::string> m_artifacts;
  std::string m_errorMessage;
  double m_progress = 0.0;
  std::string m_createdAt;
  std::string m_updatedAt;
};

} // namespace sicnu::workflow
