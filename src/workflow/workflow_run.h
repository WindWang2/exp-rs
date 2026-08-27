#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>

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
bool isTerminalRunState( WorkflowRunState state );
bool isValidRunStateTransition( WorkflowRunState from, WorkflowRunState to );

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
  static StepPlan fromJson( const Json::Value &json );
};

class WorkflowRun {
public:
  WorkflowRun() = default;

  static std::unique_ptr<WorkflowRun> createFromDefinition( const WorkflowDefinition &def,
                                                            const std::string &runId = "" );

  const std::string &getRunId() const { return m_runId; }
  void setRunId( const std::string &id ) { m_runId = id; }

  const std::string &getWorkflowId() const { return m_workflowId; }
  void setWorkflowId( const std::string &id ) { m_workflowId = id; }

  WorkflowRunState getState() const { return m_state; }
  bool transitionTo( WorkflowRunState newState );
  void forceSetState( WorkflowRunState state ) { m_state = state; }

  const WorkflowDefinition &getDefinition() const { return m_definition; }
  void setDefinition( const WorkflowDefinition &def ) { m_definition = def; }

  const std::vector<StepPlan> &getStepPlans() const { return m_stepPlans; }
  std::vector<StepPlan> &getStepPlans() { return m_stepPlans; }
  void setStepPlans( const std::vector<StepPlan> &plans ) { m_stepPlans = plans; }

  StepPlan *findStepPlan( const std::string &stepId );
  const StepPlan *findStepPlan( const std::string &stepId) const;

  const std::map<std::string, std::string> &getArtifacts() const { return m_artifacts; }
  std::map<std::string, std::string> &getArtifacts() { return m_artifacts; }
  void setArtifact( const std::string &name, const std::string &value ) { m_artifacts[name] = value; }
  std::string getArtifact( const std::string &name ) const;

  const std::string &getErrorMessage() const { return m_errorMessage; }
  void setErrorMessage( const std::string &msg ) { m_errorMessage = msg; }

  double getProgress() const { return m_progress; }
  void setProgress( double p ) { m_progress = p; }
  void recalculateProgress();

  const std::string &getCreatedAt() const { return m_createdAt; }
  void setCreatedAt( const std::string &ts ) { m_createdAt = ts; }

  const std::string &getUpdatedAt() const { return m_updatedAt; }
  void setUpdatedAt( const std::string &ts ) { m_updatedAt = ts; }

  Json::Value toJson() const;
  static std::unique_ptr<WorkflowRun> fromJson( const Json::Value &json, std::string &error );

private:
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
