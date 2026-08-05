// src/processing/framework/agent_workflow_executor.h
#pragma once

#include "atomic_algorithm_registry.h"
#include <json/json.h>
#include <functional>
#include <memory>
#include <string>

#include <QMap>
#include <QObject>
#include <QPointer>

namespace sicnu::data {
  class DataManager;
}

namespace sicnu {
  struct AlgorithmTaskInfo;
  struct PipelineExecutionInfo;
}

namespace sicnu::processing {

class AgentWorkflowExecutor : public QObject {
  Q_OBJECT
public:
  /// Completion callback for asynchronous plan execution; receives the same
  /// planResult shape as executeAgentPlan().
  using PlanCompletionCallback = std::function<void( const Json::Value &planResult )>;

  explicit AgentWorkflowExecutor( data::DataManager *dataManager = nullptr, QObject *parent = nullptr );
  ~AgentWorkflowExecutor() override = default;

  void setDataManager( data::DataManager *dataManager );
  data::DataManager* dataManager() const;

  /**
   * Executes a multi-step sequential Agent DAG Plan JSON request:
   * {
   *   "steps": [
   *     { "id": "s1", "name": "rs_spectral_index", "arguments": { ... } },
   *     { "id": "s2", "name": "opencv_gaussian_blur", "arguments": { "input": "$s1.output" } }
   *   ]
   * }
   */
  Json::Value executeAgentPlan( const Json::Value &planJson, ProgressCallback progressCb = nullptr );

  /**
   * Asynchronous variant of executeAgentPlan(): submits the pipeline and
   * returns its pipelineId immediately. Once the pipeline reaches a terminal
   * state (all step tasks terminal, failed, or canceled), the completion
   * callback is invoked exactly once with the same planResult shape the
   * blocking path returns. When @a context is non-null the callback is
   * marshaled onto @a context's thread via QMetaObject::invokeMethod.
   * On parse/submit failure the error-shaped result is delivered the same
   * way and -1 is returned. The connection to TaskCenter is owned by this
   * executor and auto-disconnects on its destruction.
   */
  long executeAgentPlanAsync( const Json::Value &planJson, PlanCompletionCallback callback, QObject *context = nullptr );

private slots:
  /// Watches TaskCenter::taskUpdated (emitted outside the TaskCenter mutex);
  /// fires callbacks for any pending plan whose pipeline reached a terminal
  /// state. Runs on this executor's thread.
  void onTaskCenterTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

private:
  struct PendingPlan {
    PlanCompletionCallback callback;
    QPointer<QObject> context;
    int totalSteps = 0;
  };

  /// Assembles the final planResult (status/completedSteps/stepResults) from a
  /// terminal pipeline snapshot. Single owner of the result shape — shared by
  /// the blocking and asynchronous execution paths.
  Json::Value assemblePlanResult( int totalSteps, long pipelineId, const sicnu::PipelineExecutionInfo &pipeInfo ) const;
  /// Fires a pending plan's callback exactly once if its pipeline is terminal,
  /// and removes the plan from the watch map.
  void checkPendingPlan( long pipelineId );
  /// Delivers @a planResult on @a context's thread (direct call when
  /// @a context is null).
  static void deliverPlanResult( const PlanCompletionCallback &callback, QObject *context, const Json::Value &planResult );

  data::DataManager *mDataManager = nullptr;
  QMap<long, PendingPlan> m_pendingPlans;
};

} // namespace sicnu::processing
