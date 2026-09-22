// src/agent_ops/operations_coordinator.h
#pragma once

//
// OperationsCoordinator — production integration surface over AgentLoop.
// Pause/cancel/resume, budgets, autonomy, evidence, live trace, diagnose,
// recovery, FinalDelivery. Does NOT reimplement the loop state machine.
//

#include "agent_ops/autonomy_ops_gate.h"
#include "agent_ops/benchmark_adapter.h"
#include "agent_ops/delivery_assembler.h"
#include "agent_ops/diagnostic_bridge.h"
#include "agent_ops/live_session_recorder.h"
#include "agent_ops/ops_projection.h"
#include "agent_ops/ops_types.h"
#include "agent_ops/recovery_bridge.h"
#include "agent_ops/resume_reconciler.h"
#include "agent_loop/scientific_agent_session.h"
#include "agent/autonomy/autonomy_holder.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace sicnu::agent_ops {

struct OpsRunRequest {
    sicnu::agent_loop::SessionRunRequest session;
    sicnu::agent_loop::SessionPolicy policy = sicnu::agent_loop::SessionPolicy::defaults();
    OpsBudget budgets;
    std::string domain = "research";
    std::string role;
    std::string journalDirectory; ///< empty = in-memory only
    bool approvePendingRepair = false;
    std::string leadingRepairRiskClass = "shape_preserving";
};

struct OpsRunResult {
    bool ok = false;
    sicnu::agent_loop::SessionResult session;
    FinalDelivery delivery;
    OpsProjection projection;
    std::optional<sicnu::agentbench::AgentTrace> trace;
    std::optional<OpDiagnostic> lastDiagnostic;
    std::optional<RecoveryDecision> lastRecovery;
    ReconcileResult reconcile;
    std::string error;
};

class OperationsCoordinator {
  public:
    struct Dependencies {
        sicnu::agent_loop::ScientificAgentSession::Dependencies seams;
        sicnu::agent::autonomy::AutonomyPolicy autonomyPolicy{
            sicnu::agent::autonomy::AutonomyPolicyHolder::researchDefaultPolicy()};
        IBenchmarkResultSink *benchmarkSink = nullptr;
    };

    OperationsCoordinator(Dependencies deps, LiveSessionRecorder::Options recorderOptions = {});

    void requestPause() { mPauseRequested.store(true); }
    void requestCancel() { mCancelRequested.store(true); }
    void clearPause() { mPauseRequested.store(false); }
    bool isPauseRequested() const { return mPauseRequested.load(); }
    bool isCancelRequested() const { return mCancelRequested.load(); }

    /// Run a full session through AgentLoop, recording + projecting + delivering.
    OpsRunResult run(const OpsRunRequest &request);

    /// Resume from journal directory (Feature F).
    OpsRunResult resume(const std::string &journalDirectory, const std::string &sessionId,
                        const OpsRunRequest &request);

    /// Standalone closed-loop helper for tests: after a failed SessionResult,
    /// diagnose → recovery decide (does not mutate loop; used for assertions).
    RecoveryDecision evaluateRecovery(const OpDiagnostic &diagnostic,
                                      const RecoveryContext &ctx) const;

    OpsProjection timeline(const sicnu::agent_loop::SessionJournal &journal,
                           const OpsBudget &budgets = {}) const;

  private:
    OpsRunResult finish(sicnu::agent_loop::SessionResult &&session, const OpsRunRequest &request,
                        const std::optional<OpDiagnostic> &diag,
                        const std::optional<RecoveryDecision> &recovery);

    Dependencies mDeps;
    LiveSessionRecorder mRecorder;
    DiagnosticBridge mDiagnostic;
    RecoveryBridge mRecovery;
    DeliveryAssembler mDelivery;
    OpsProjector mProjector;
    ResumeReconciler mReconciler;
    BenchmarkAdapter mBenchmark;
    BridgedDiagnoser mBridgedDiagnoser;
    std::atomic<bool> mPauseRequested{false};
    std::atomic<bool> mCancelRequested{false};
};

} // namespace sicnu::agent_ops
