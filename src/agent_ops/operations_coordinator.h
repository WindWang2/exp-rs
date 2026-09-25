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
#include "repair_planner/repair_provider.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::agent_ops {

struct OpsRunRequest {
    sicnu::agent_loop::SessionRunRequest session;
    sicnu::agent_loop::SessionPolicy policy = sicnu::agent_loop::SessionPolicy::defaults();
    OpsBudget budgets;
    std::string domain = "research";
    std::string role;
    std::string journalDirectory; ///< empty = in-memory only
    /// Minted repair-approval token (optional). Verified against this
    /// coordinator, the last projected recovery plan and the launch clock;
    /// anything unverifiable fails closed (the gate stays shut and asks).
    Json::Value repairApproval{Json::Value()};
    /// Driver clock used to verify the approval at launch; <= 0 means no
    /// usable clock, which never proves an approval unexpired.
    long long approvalNowMs = 0;
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
    /// Non-empty when a presented repair approval was refused (expired,
    /// tampered, replayed, wrong plan/coordinator). The session outcome is
    /// unaffected: the repair simply runs WITHOUT the approval, and the
    /// human gate asks again.
    std::string approvalError;
    /// Non-empty when the live-trajectory benchmark persist failed; the
    /// session outcome itself stays authoritative.
    std::string benchmarkError;
};

class OperationsCoordinator {
  public:
    struct Dependencies {
        sicnu::agent_loop::ScientificAgentSession::Dependencies seams;
        sicnu::agent::autonomy::AutonomyPolicy autonomyPolicy{
            sicnu::agent::autonomy::AutonomyPolicyHolder::researchDefaultPolicy()};
        IBenchmarkResultSink *benchmarkSink = nullptr;
        /// Live capability knowledge (or a test fake) behind the recovery
        /// bridge's repair planning. Null = the bridge plans nothing (typed
        /// no_safe_repair) instead of fabricating candidates.
        const sicnu::repair::RepairCapabilityProvider *repairCapabilityProvider = nullptr;
    };

    OperationsCoordinator(Dependencies deps, LiveSessionRecorder::Options recorderOptions = {});

    /// This coordinator's process-unique instance id — the value repair
    /// approval tokens bind to.
    long long instanceId() const { return mInstanceId; }

    void requestPause() { mPauseRequested.store(true); }
    void requestCancel() { mCancelRequested.store(true); }
    void clearPause() { mPauseRequested.store(false); }
    /// Relaunch path: after a cancel was consumed or refused, the driver
    /// must be able to arm the coordinator for new work again.
    void clearCancel() { mCancelRequested.store(false); }
    bool isPauseRequested() const { return mPauseRequested.load(); }
    bool isCancelRequested() const { return mCancelRequested.load(); }

    /// Arms a minted repair-approval token for the NEXT launch. The token is
    /// verified here (binding to this coordinator and to the last projected
    /// recovery plan, digest, expiry at `nowMs`) and verified AGAIN at
    /// launch. Returns an empty string on success, else a typed refusal
    /// code (NO_PENDING_REPAIR_PLAN, APPROVAL_MALFORMED, APPROVAL_TAMPERED,
    /// APPROVAL_WRONG_PLAN, APPROVAL_WRONG_COORDINATOR, APPROVAL_EXPIRED,
    /// APPROVAL_REPLAYED). One-shot: the next run() consumes the armed
    /// approval, and a consumed token is refused as a replay.
    std::string armRepairApproval(const Json::Value &tokenDoc, long long nowMs);

    /// True when an approval is armed for the next launch (tests/surface).
    bool hasPendingRepairApproval() const;

    /// The most recent run()/resume() outcome on this coordinator (nullopt
    /// before the first one) — the backing store for the status/timeline/
    /// export surface actions.
    const std::optional<OpsRunResult> &lastResult() const { return mLastResult; }

    /// Run a full session through AgentLoop, recording + projecting + delivering.
    OpsRunResult run(const OpsRunRequest &request);

    /// Resume from journal directory (Feature F).
    OpsRunResult resume(const std::string &journalDirectory, const std::string &sessionId,
                        const OpsRunRequest &request);

    /// The driver's recovery flow: diagnose → recovery decide over THIS
    /// coordinator's provider and armed approval state. When an approval is
    /// armed, it is verified (this coordinator, the projected plan, the
    /// clock in ctx.approvalNowMs) and CONSUMED here: on success
    /// ctx.humanApprovedRepair / ctx.approvedFindingsDigest are derived from
    /// the token — a bare ctx bool is never trusted. A refused token leaves
    /// the gate shut and sets decision.approvalError. The projected repair
    /// plan (when the decision carries one) is remembered as this
    /// coordinator's last projected plan — the binding target for
    /// armRepairApproval.
    RecoveryDecision evaluateRecovery(const OpDiagnostic &diagnostic, RecoveryContext &ctx);

    /// The findings digest of the last projected repair plan: the one
    /// remembered from evaluateRecovery(), else the last run()'s recovery
    /// decision. Empty when this coordinator never projected one. This is
    /// the binding target approvals mint and verify against.
    std::string lastProjectedFindingsDigest() const;

    OpsProjection timeline(const sicnu::agent_loop::SessionJournal &journal,
                           const OpsBudget &budgets = {}) const;

  private:
    OpsRunResult finish(sicnu::agent_loop::SessionResult &&session, const OpsRunRequest &request,
                        const std::optional<OpDiagnostic> &diag,
                        const std::optional<RecoveryDecision> &recovery,
                        const std::string &approvalError);

    /// Verifies `tokenDoc` against this coordinator and the last projected
    /// repair science at clock `nowMs`. Empty string = ok, else typed code.
    std::string verifyApprovalAgainstLastProjection(const Json::Value &tokenDoc,
                                                    long long nowMs) const;

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
    // Control/mutable state below is NOT atomic: approval arming and
    // lastResult (like run() itself) belong to the driver's coordinator
    // thread; pause/cancel are safe to request cross-thread.
    const long long mInstanceId;
    struct ArmedApproval {
        Json::Value token;
        std::string findingsDigest;
    };
    std::optional<ArmedApproval> mPendingRepairApproval;
    /// The findings digest of the last projected repair plan (bookkeeping of
    /// the most recent projection; evaluateRecovery is deliberately
    /// non-const because it consumes approval state and refreshes this).
    std::string mLastProjectedFindingsDigest;
    /// Digests of tokens already consumed by a launch: re-presenting one is
    /// a replay, refused even though the token itself is still intact. The
    /// ring is bounded; once the oldest digest is forgotten, a long-TTL
    /// token can arm again — TTL bounds bound that window (mint with a
    /// sane ttlMs; expiry is enforced at every verify).
    std::vector<std::string> mConsumedApprovalDigests;
    static constexpr std::size_t kMaxConsumedDigests = 16;
    std::optional<OpsRunResult> mLastResult;
};

} // namespace sicnu::agent_ops
