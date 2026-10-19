// src/agent_ops/operations_coordinator.cpp
#include "agent_ops/operations_coordinator.h"

#include "agent_ops/repair_approval.h"

#include <algorithm>
#include <chrono>

namespace sicnu::agent_ops {

namespace {

/// The goal the journal was started with: the loop rebuilds it from the
/// goal_normalization decision's inputs["goal"] (last write wins); empty
/// when the journal has no recorded goal yet.
std::string journalledGoal(const sicnu::agent_loop::SessionJournal &journal)
{
    std::string goal;
    for (const auto &entry : journal.entries())
    {
        if (!entry.decision || entry.decision->stage != "goal_normalization")
            continue;
        const Json::Value &recorded = entry.decision->inputs["goal"];
        if (recorded.isString())
            goal = recorded.asString();
    }
    return goal;
}

std::string approvalCheckToError(const char *check)
{
    if (check == nullptr)
        return "APPROVAL_MALFORMED";
    if (std::string(check) == approval_check::kExpired)
        return "APPROVAL_EXPIRED";
    if (std::string(check) == approval_check::kTampered)
        return "APPROVAL_TAMPERED";
    if (std::string(check) == approval_check::kWrongPlan)
        return "APPROVAL_WRONG_PLAN";
    if (std::string(check) == approval_check::kWrongCoordinator)
        return "APPROVAL_WRONG_COORDINATOR";
    return "APPROVAL_MALFORMED";
}

/// Budget parity for the two launch paths: run() and resume() must start the
/// session under the SAME bounds the caller asked for, never silently under
/// policy defaults. One definition so the paths cannot drift apart. (#1629)
sicnu::agent_loop::SessionPolicy launchPolicy(const OpsRunRequest &request)
{
    sicnu::agent_loop::SessionPolicy policy = request.policy;
    policy.maxReplans = request.budgets.maxReplans;
    policy.noProgressThreshold = request.budgets.noProgressThreshold;
    if (request.budgets.resourceBudgetMb > 0)
        policy.resourceBudgetMb = request.budgets.resourceBudgetMb;
    return policy;
}

/// The autonomy gate both launch paths share: a fresh launch ("ops:run") and
/// a continued one ("ops:resume") pass the SAME gate — a resumed session can
/// execute exactly like a fresh launch, so refusing one and not the other
/// would let an execute-capable resume bypass the policy. Returns false with
/// `reasonCode` set when the policy refuses.
bool gateLaunchAutonomy(const OperationsCoordinator::Dependencies &deps, const char *actionKey,
                        const OpsRunRequest &request, std::string &reasonCode)
{
    OpsAutonomyRequest ar;
    ar.mutateKind = ops_mutate::kExecute;
    ar.domain = request.domain;
    ar.role = request.role;
    ar.intent = request.session.intent;
    ar.actionKey = actionKey;
    const auto gate = gateMutatingOp(deps.autonomyPolicy, ar);
    if (gate.allowed)
        return true;
    reasonCode = gate.reasonCode;
    return false;
}

/// The typed refusal a gated launch returns: the session never starts and the
/// recovery carrier reports the abort with its reason, so every driver (CLI /
/// MCP / panel) routes it the same way.
OpsRunResult autonomyDenial(const std::string &reasonCode)
{
    OpsRunResult denied;
    denied.ok = false;
    denied.error = reasonCode;
    denied.lastRecovery = RecoveryDecision{};
    denied.lastRecovery->action = recovery_action::kAbort;
    denied.lastRecovery->reasonCode = reasonCode;
    denied.lastRecovery->autonomyAllowed = false;
    denied.lastRecovery->autonomyReasonCode = reasonCode;
    return denied;
}

/// A cancel that arrived before launch must reach the session before its
/// first stage: the loop's cooperative flag is the only stop an in-flight run
/// honors, and this pre-launch arming is the last chance to set it. A cancel
/// arriving mid-run is covered separately by the active-session registration
/// (RAII guard at the call site). (#1451)
void applyPendingCancel(sicnu::agent_loop::ScientificAgentSession &session,
                        const std::atomic<bool> &cancelRequested)
{
    if (cancelRequested.load())
        session.requestCancel();
}

/// The bridge cache is evidence about ONE diagnose invocation; a stale entry
/// from a previous session must never be attributed to this launch. Both
/// launch paths reset it for the same reason. (#1629)
void resetLaunchDiagnostic(BridgedDiagnoser &diagnoser)
{
    diagnoser.resetLastDiagnostic();
}

/// Installs the crash-evidence checkpoint sink on a launching session: the
/// journal is flushed after EVERY append, so a hard kill at any stage (not
/// only at terminal) leaves a loadable prefix on disk. The reconciler reads
/// that prefix to refuse unsafe resumes (e.g. submitted runs that a blind
/// restart would duplicate).
///
/// Why the by-reference `checkpointErr` capture is safe: ScientificAgentSession
/// invokes the sink SYNCHRONOUSLY from note()/recordDecision()
/// (scientific_agent_session.cpp), which only run inside session.run(), and
/// the session that owns the sink is a LOCAL of run()/resume(), destroyed
/// when that scope ends. Every invocation therefore happens while the
/// caller's checkpointErr is still alive, and the sink never outlives it —
/// the safety rests on that synchronous invocation, not on the session
/// outliving the scope (it does not). The directory is copied into the
/// closure so the sink never depends on a caller-owned buffer. (#1629)
void installCheckpointSink(sicnu::agent_loop::ScientificAgentSession &session,
                           const std::string &journalDirectory,
                           const LiveSessionRecorder &recorder, std::string &checkpointErr)
{
    session.setCheckpointSink(
        [&recorder, dir = journalDirectory,
         &checkpointErr](const sicnu::agent_loop::SessionJournal &journal)
        {
            std::string err;
            if (!recorder.persistJournal(journal, dir, &err))
                checkpointErr = err.empty() ? "JOURNAL_CHECKPOINT_FAILED" : err;
        });
}

} // namespace

OperationsCoordinator::OperationsCoordinator(Dependencies deps,
                                             LiveSessionRecorder::Options recorderOptions)
    : mDeps(std::move(deps)), mRecorder(std::move(recorderOptions)),
      mRecovery(mDeps.repairCapabilityProvider), mBridgedDiagnoser(mDeps.seams.diagnoser),
      mInstanceId(nextRepairApprovalInstanceId())
{
}

void OperationsCoordinator::requestCancel()
{
    mCancelRequested.store(true);
    // Also reach sessions already inside run()/resume(): the pre-launch
    // check there cannot see a cancel that arrives afterwards, and a stop
    // that never reaches the live loop would let it run to completion.
    // ScientificAgentSession::requestCancel() is a cross-thread-safe atomic
    // store (cooperative stop at the next stage boundary), so calling it
    // under the registry lock cannot deadlock or re-enter. (#1451)
    std::lock_guard<std::mutex> lock(mActiveSessionsMutex);
    for (sicnu::agent_loop::ScientificAgentSession *session : mActiveSessions)
        session->requestCancel();
}

OperationsCoordinator::ActiveSessionGuard::ActiveSessionGuard(
    OperationsCoordinator &coordinator, sicnu::agent_loop::ScientificAgentSession *session)
    : mCoordinator(coordinator), mSession(session)
{
    std::lock_guard<std::mutex> lock(mCoordinator.mActiveSessionsMutex);
    mCoordinator.mActiveSessions.push_back(mSession);
}

OperationsCoordinator::ActiveSessionGuard::~ActiveSessionGuard()
{
    std::lock_guard<std::mutex> lock(mCoordinator.mActiveSessionsMutex);
    mCoordinator.mActiveSessions.erase(
        std::remove(mCoordinator.mActiveSessions.begin(), mCoordinator.mActiveSessions.end(),
                    mSession),
        mCoordinator.mActiveSessions.end());
}

std::string OperationsCoordinator::lastProjectedFindingsDigest() const
{
    if (!mLastProjectedFindingsDigest.empty())
        return mLastProjectedFindingsDigest;
    const auto &last = mLastResult;
    if (last && last->lastRecovery && last->lastRecovery->repairPlan.isObject() &&
        last->lastRecovery->repairPlan["provenance"].isObject() &&
        last->lastRecovery->repairPlan["provenance"]["findings_digest"].isString())
        return last->lastRecovery->repairPlan["provenance"]["findings_digest"].asString();
    return {};
}

std::string OperationsCoordinator::verifyApprovalAgainstLastProjection(
    const Json::Value &tokenDoc, long long nowMs) const
{
    const std::string targetDigest = lastProjectedFindingsDigest();
    if (targetDigest.empty())
        return "NO_PENDING_REPAIR_PLAN";
    const char *check =
        verifyRepairApprovalToken(tokenDoc, targetDigest, mInstanceId, nowMs);
    if (std::string(check) == approval_check::kOk)
        return {};
    return approvalCheckToError(check);
}

std::string OperationsCoordinator::armRepairApproval(const Json::Value &tokenDoc,
                                                     long long nowMs)
{
    // Fail closed on a malformed token BEFORE touching the digest: jsoncpp's
    // asString() throws on a missing/non-string field, and a typed refusal is
    // the contract, not an exception escaping the approval path. (#1451)
    if (!tokenDoc.isObject() || !tokenDoc["digest"].isString() ||
        !tokenDoc["findings_digest"].isString())
        return "APPROVAL_MALFORMED";

    try
    {
        // Replay first: a token this coordinator already consumed by a launch
        // cannot arm again, whatever the current projection is — one approval
        // authorizes exactly one repair launch.
        const std::string tokenDigest = tokenDoc["digest"].asString();
        for (const std::string &consumed : mConsumedApprovalDigests)
        {
            if (consumed == tokenDigest)
                return "APPROVAL_REPLAYED";
        }

        const std::string error = verifyApprovalAgainstLastProjection(tokenDoc, nowMs);
        if (!error.empty())
            return error;

        mPendingRepairApproval =
            ArmedApproval{tokenDoc, tokenDoc["findings_digest"].asString()};
        return {};
    }
    catch (const std::exception &)
    {
        return "APPROVAL_MALFORMED";
    }
}

bool OperationsCoordinator::hasPendingRepairApproval() const
{
    return mPendingRepairApproval.has_value();
}

OpsRunResult OperationsCoordinator::finish(sicnu::agent_loop::SessionResult &&session,
                                           const OpsRunRequest &request,
                                           const std::optional<OpDiagnostic> &diag,
                                           const std::optional<RecoveryDecision> &recovery,
                                           const std::string &checkpointError,
                                           const std::string &approvalError)
{
    OpsRunResult out;
    out.session = std::move(session);
    out.lastDiagnostic = diag;
    out.lastRecovery = recovery;
    out.ok = out.session.ok;
    out.approvalError = approvalError;
    out.checkpointError = checkpointError;

    if (!request.journalDirectory.empty())
    {
        std::string err;
        if (!mRecorder.persistJournal(out.session.journal, request.journalDirectory, &err))
        {
            // Persistence failure is noted; session outcome stays authoritative.
            out.error = err.empty() ? "JOURNAL_PERSIST_FAILED" : err;
        }
    }

    std::string traceErr;
    out.trace = mRecorder.projectTrace(out.session.journal, "live", &traceErr);
    if (!out.trace && out.error.empty())
        out.error = traceErr;

    // Live-trajectory benchmark refs: record what actually happened (session
    // id, trace id, decision/evidence refs) — never recomputed metrics. The
    // sink is the declared persistence seam for this trajectory evidence.
    Json::Value benchmarkRef(Json::objectValue);
    bool benchmarkPersisted = false;
    if (mDeps.benchmarkSink)
    {
        BenchmarkPersistDocument doc;
        doc.suiteId = "live-session";
        doc.suiteVersion = "1";
        doc.resultId = "bpr-live-" + out.session.sessionId + "-" +
                       std::to_string(out.session.summary.journalEntries);
        doc.status = out.ok ? "completed" : "failed";
        doc.summary["session_id"] = out.session.sessionId;
        doc.summary["outcome"] = out.session.summary.outcome;
        doc.summary["stop_reason"] = out.session.stopReason;
        doc.summary["mode"] = out.session.summary.mode;
        if (out.trace)
            doc.summary["trace_id"] = out.trace->traceId;
        doc.rawReport["kind"] = "live_trajectory";
        doc.rawReport["journal_entries"] =
            static_cast<Json::UInt64>(out.session.summary.journalEntries);
        Json::Value decisionRefs(Json::arrayValue);
        for (const auto &dec : out.session.summary.decisions)
        {
            Json::Value ref(Json::objectValue);
            ref["decision_id"] = dec.decisionId;
            ref["stage"] = dec.stage;
            Json::Value evidenceRefs(Json::arrayValue);
            for (const auto &ev : dec.evidence)
                if (!ev.ref.empty())
                    evidenceRefs.append(ev.ref);
            ref["evidence_refs"] = evidenceRefs;
            decisionRefs.append(ref);
        }
        doc.rawReport["decision_refs"] = decisionRefs;

        std::string sinkErr;
        if (mBenchmark.persist(doc, *mDeps.benchmarkSink, &sinkErr))
        {
            benchmarkPersisted = true;
            benchmarkRef["kind"] = kBenchmarkPersistKind;
            benchmarkRef["result_id"] = doc.resultId;
            benchmarkRef["status"] = doc.status;
        }
        else
        {
            // Persist failure is noted; the session outcome stays authoritative.
            out.benchmarkError = sinkErr.empty() ? "BENCHMARK_PERSIST_FAILED" : sinkErr;
        }
    }

    DeliveryExtras extras;
    if (out.trace)
        extras.trace = out.trace;
    if (benchmarkPersisted)
        extras.benchmarkRefs.append(benchmarkRef);

    // Approval/ask carrier: the loop's withhold_repair decisions are the
    // evidence that a non-auto-approvable repair (radiometric /
    // science-changing) was proposed and NOT applied. They surface here as
    // typed questions on the delivery wire so every driver (CLI / MCP /
    // panel) can route the human approval round-trip; a repair is never
    // auto-executed, and the ask must not die inside the decision log.
    for (const auto &dec : out.session.summary.decisions)
    {
        if (dec.selected.get("action", "").asString() != "withhold_repair")
            continue;
        Json::Value question(Json::objectValue);
        question["kind"] = "repair_approval";
        question["rule_id"] = dec.inputs.get("rule_id", "").asString();
        question["risk_class"] = dec.inputs.get("risk_class", "").asString();
        question["decision_id"] = dec.decisionId;
        question["stage"] = dec.stage;
        question["reason"] = dec.reason;
        question["ask"] = "apply repair '" + question["rule_id"].asString() + "' (risk class '" +
                          question["risk_class"].asString() +
                          "')? It was withheld because the policy does not auto-approve it.";
        extras.questions.append(question);
    }
    // The recovery bridge's ask (e.g. REPAIR_NEEDS_APPROVAL) rides along the
    // same way — the human gate is on the wire, not only in the trace.
    if (recovery && recovery->needsApproval)
    {
        Json::Value question(Json::objectValue);
        question["kind"] = "recovery_ask";
        question["action"] = recovery->action;
        question["reason_code"] = recovery->reasonCode;
        question["ask"] = "recovery requires a human decision (" + recovery->reasonCode + ")";
        extras.questions.append(question);
    }

    out.delivery = mDelivery.assemble(out.session, extras);
    out.projection = mProjector.projectResult(out.session, request.budgets);
    out.reconcile = mReconciler.reconcile(out.session.journal);

    // The binding target for approvals is always THIS coordinator's most
    // recent projection: a fresh run replaces (or clears) what an earlier
    // evaluateRecovery remembered, so a stale plan can never silently
    // outlive its run.
    if (recovery && recovery->repairPlan.isObject() &&
        recovery->repairPlan["provenance"].isObject() &&
        recovery->repairPlan["provenance"]["findings_digest"].isString())
        mLastProjectedFindingsDigest =
            recovery->repairPlan["provenance"]["findings_digest"].asString();
    else
        mLastProjectedFindingsDigest.clear();

    mLastResult = out;
    return out;
}

OpsRunResult OperationsCoordinator::run(const OpsRunRequest &request)
{
    OpsRunResult denied;
    // Pause gates the launch of new work: a paused coordinator refuses to
    // start a session instead of silently ignoring the request. The loop
    // state machine keeps sole authority over a session once started.
    if (mPauseRequested.load())
    {
        denied.ok = false;
        denied.error = "PAUSED";
        return denied;
    }

    // Autonomy gate before execute-capable modes.
    if (request.policy.mode == sicnu::agent_loop::RunMode::ExecuteWithVerify)
    {
        std::string reasonCode;
        if (!gateLaunchAutonomy(mDeps, "ops:run", request, reasonCode))
            return autonomyDenial(reasonCode);
    }

    // Wire bridged diagnoser when an inner diagnoser is present.
    sicnu::agent_loop::ScientificAgentSession::Dependencies seams = mDeps.seams;
    if (seams.diagnoser)
        seams.diagnoser = &mBridgedDiagnoser;

    // Repair approval consumption — one-shot, verified at launch. A token
    // may arrive with the request (driver-held) or already be armed via
    // armRepairApproval (surface flow); both go through the same gate: this
    // coordinator, the last projected recovery plan, digest, expiry at the
    // launch clock. Anything unverifiable fails closed: the run proceeds
    // WITHOUT the approval, the refusal is surfaced, and the gate asks
    // again. The armed entry is consumed either way, and the consumed
    // digest refuses later replays.
    std::string approvalError;
    if (request.repairApproval.isObject() && !request.repairApproval.isNull())
    {
        // Fail closed on a malformed token BEFORE touching the digest — the
        // same #1451 guard armRepairApproval has. jsoncpp's asString() throws
        // Json::LogicError on a missing/non-string field; through OpsDriver
        // that degrades to INVALID_ARGS, but a direct coordinator caller
        // (the GUI dock worker thread) would terminate the process.
        if (!request.repairApproval["digest"].isString() ||
            !request.repairApproval["findings_digest"].isString())
        {
            if (approvalError.empty())
                approvalError = "APPROVAL_MALFORMED";
        }
        else
        {
            // Single-use is symmetric: a driver-held token goes through the
            // SAME consumed ring as an armed one — a consumed token presented
            // with the request is a replay, never a second authorization.
            const std::string tokenDigest = request.repairApproval["digest"].asString();
            bool replayed = false;
            for (const std::string &consumed : mConsumedApprovalDigests)
            {
                if (consumed == tokenDigest)
                {
                    replayed = true;
                    break;
                }
            }
            if (replayed)
            {
                if (approvalError.empty())
                    approvalError = "APPROVAL_REPLAYED";
            }
            else
            {
                const std::string directError = verifyApprovalAgainstLastProjection(
                    request.repairApproval, request.approvalNowMs);
                if (directError.empty())
                    mPendingRepairApproval =
                        ArmedApproval{request.repairApproval,
                                      request.repairApproval["findings_digest"].asString()};
                else if (approvalError.empty())
                    approvalError = directError;
            }
        }
    }
    bool pendingApproval = false;
    std::string approvedPlanId;
    if (mPendingRepairApproval)
    {
        const char *check = verifyRepairApprovalToken(
            mPendingRepairApproval->token, mPendingRepairApproval->findingsDigest,
            mInstanceId, request.approvalNowMs);
        if (std::string(check) == approval_check::kOk)
        {
            pendingApproval = true;
            approvedPlanId = mPendingRepairApproval->findingsDigest;
        }
        else if (approvalError.empty())
            approvalError = approvalCheckToError(check);
        // Record the consumed token BEFORE clearing: replay of the same
        // approval must not arm a later launch.
        if (mConsumedApprovalDigests.size() >= kMaxConsumedDigests)
            mConsumedApprovalDigests.erase(mConsumedApprovalDigests.begin());
        if (mPendingRepairApproval->token.isObject() &&
            mPendingRepairApproval->token["digest"].isString())
        {
            mConsumedApprovalDigests.push_back(mPendingRepairApproval->token["digest"].asString());
        }
        mPendingRepairApproval.reset();
    }

    sicnu::agent_loop::SessionPolicy policy = launchPolicy(request);

    sicnu::agent_loop::ScientificAgentSession session(policy, seams);
    applyPendingCancel(session, mCancelRequested);
    // Propagate a cancel that arrives while the loop runs (RAII: the
    // registration ends with session.run()'s scope even if a seam throws,
    // so propagation never dereferences a dead session). (#1451)
    ActiveSessionGuard activeSessionGuard(*this, &session);
    std::string checkpointErr;
    if (!request.journalDirectory.empty())
        installCheckpointSink(session, request.journalDirectory, mRecorder, checkpointErr);
    resetLaunchDiagnostic(mBridgedDiagnoser);

    // Note: pause is checked at launch (above); cancel is the hard stop
    // inside the loop and now also reaches a mid-run session through the
    // active-session registration above. AgentLoop owns the state machine
    // while it runs.
    const auto startedAt = std::chrono::steady_clock::now();
    auto result = session.run(request.session);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - startedAt)
                               .count();

    std::optional<OpDiagnostic> diag;
    std::optional<RecoveryDecision> recovery;
    if (!result.ok)
    {
        // The loop's typed stop reason is what actually terminated the
        // session: it is always the headline root cause. Structured
        // evidence — the live bridge diagnostic, the journal's own diagnose
        // records, the bare verification verdict — attaches as sources and
        // raises confidence; it never overrides the terminal fact and never
        // invents repairability. Post-hoc recovery stays advisory (ask).
        OpDiagnostic d;
        d.code = "ops.session.FAILED";
        d.rootCauseCode = result.stopReason.empty() ? "SESSION_FAILED" : result.stopReason;
        d.repairable = false;
        d.retryable = false;
        d.advisoryNext = recovery_action::kAsk;
        d.sources["session"]["stop_reason"] = d.rootCauseCode;

        bool structured = false;
        if (mBridgedDiagnoser.lastOpsDiagnostic())
        {
            // Bridge evidence from THIS run (the cache was reset at launch).
            d.sources["bridge"] = mBridgedDiagnoser.lastOpsDiagnostic()->toJson();
            structured = true;
        }
        else
        {
            for (const auto &dec : result.summary.decisions)
            {
                if (dec.stage != "diagnose" ||
                    !dec.inputs.isObject() ||
                    !dec.inputs.isMember("root_cause_code") ||
                    !dec.inputs["root_cause_code"].isString() ||
                    dec.inputs["root_cause_code"].asString().empty())
                    continue;
                d.sources["journal_diagnose"] = dec.toJson();
                d.evidence["decision_id"] = dec.decisionId;
                structured = true;
            }
        }
        if (!structured && result.summary.verificationVerdict == "FAIL")
        {
            sicnu::agent_loop::VerificationReport vr;
            vr.verdictValue = "FAIL";
            DiagnosticInputs inputs;
            inputs.verification = vr;
            auto verification = mDiagnostic.diagnose(inputs);
            if (verification)
            {
                d.sources["verification"] = verification->toJson();
                structured = true;
            }
        }

        d.confidence = structured ? 0.6 : 0.3;
        if (!structured)
        {
            d.summary = "session failed without structured diagnostic evidence";
            d.evidence["evidence_unavailable"] = true;
        }
        else
        {
            d.summary = "session failed: stop reason with structured evidence attached";
        }
        diag = d;

        RecoveryContext ctx;
        ctx.budgets = request.budgets;
        ctx.domain = request.domain;
        ctx.role = request.role;
        ctx.intent = request.session.intent;
        ctx.cancelRequested = mCancelRequested.load();
        ctx.leadingRiskClass = request.leadingRepairRiskClass;
        ctx.humanApprovedRepair = pendingApproval;
        ctx.approvedFindingsDigest = approvedPlanId;
        ctx.approvalNowMs = request.approvalNowMs;
        ctx.autonomyPolicy = mDeps.autonomyPolicy;
        ctx.replanCount = 0;
        for (const auto &s : result.summary.stages)
            if (s == "replan")
                ++ctx.replanCount;
        // No-progress: the loop's own plan-identity detector is authoritative
        // (it already ran inside the session); the post-hoc recovery bridge
        // does not re-derive a second, weaker definition. Drivers that have
        // their own cross-session evidence may set identicalFailureCount via
        // evaluateRecovery().
        if (request.budgets.wallClockMs > 0)
            ctx.elapsedMs = static_cast<long long>(elapsedMs);
        recovery = mRecovery.decide(*diag, ctx);
    }

    std::string finishApprovalError = approvalError;
    if (recovery && !recovery->approvalError.empty() && finishApprovalError.empty())
        finishApprovalError = recovery->approvalError;
    return finish(std::move(result), request, diag, recovery, checkpointErr,
                  finishApprovalError);
}

OpsRunResult OperationsCoordinator::resume(const std::string &journalDirectory,
                                           const std::string &sessionId,
                                           const OpsRunRequest &request)
{
    OpsRunResult out;
    if (mPauseRequested.load())
    {
        out.ok = false;
        out.error = "PAUSED";
        return out;
    }

    // Autonomy gate BEFORE any journal is loaded or adopted: a resumed
    // session can execute exactly like a fresh launch, so an execute-capable
    // resume must pass the same gate as run(). Without it an L0/exam policy
    // could be bypassed by parking a journal and resuming it.
    if (request.policy.mode == sicnu::agent_loop::RunMode::ExecuteWithVerify)
    {
        std::string reasonCode;
        if (!gateLaunchAutonomy(mDeps, "ops:resume", request, reasonCode))
            return autonomyDenial(reasonCode);
    }

    std::string loadErr;
    out.reconcile = mReconciler.reconcileFile(journalDirectory, sessionId, &loadErr);
    if (!out.reconcile.ok || !out.reconcile.resumable)
    {
        out.ok = false;
        // Surface the reconciler's typed reason: the reconciler projects the
        // loop's resume contract, so its reason is the answer (a past-plan
        // journal reports RESUME_PAST_PLAN_SEAM, a delivered one
        // ALREADY_TERMINAL). DUPLICATE_SUBMIT_REFUSED stays reserved for the
        // delivered case: a crashed mid-run journal is refused for the SAME
        // past-plan reason, while reconcile.duplicateSubmitRisk +
        // reconcile.submittedRunIds carry the restart hazard on the wire.
        out.error = (out.reconcile.terminalState == "delivered") ? "DUPLICATE_SUBMIT_REFUSED"
                                                                   : out.reconcile.reasonCode;
        return out;
    }

    auto journal = mRecorder.loadJournal(journalDirectory, sessionId, &loadErr);
    if (!journal)
    {
        out.ok = false;
        out.error = "CORRUPTED_OR_MISSING_JOURNAL";
        return out;
    }

    // Goal-equality guard BEFORE adopting the journal. The loop checks the
    // restated goal only inside run(), after the journal has been adopted —
    // a mismatch there appends a refusal, and finish() would persist the
    // refused terminal journal over this parked (resumable) one. Refuse
    // here instead; the parked journal stays untouched.
    const std::string recorded = journalledGoal(*journal);
    if (!recorded.empty() && request.session.goal != recorded)
    {
        out.ok = false;
        out.error = "SESSION_GOAL_MISMATCH";
        return out;
    }

    sicnu::agent_loop::ScientificAgentSession::Dependencies seams = mDeps.seams;
    if (seams.diagnoser)
        seams.diagnoser = &mBridgedDiagnoser;

    sicnu::agent_loop::SessionPolicy policy = launchPolicy(request);
    auto resumed = sicnu::agent_loop::ScientificAgentSession::resume(*journal, policy, seams);
    if (!resumed)
    {
        out.ok = false;
        out.error = "RESUME_REJECTED";
        return out;
    }
    applyPendingCancel(*resumed, mCancelRequested);
    // Same in-flight cancel propagation as run(): a cancel arriving during
    // the resumed leg must reach the live loop. (#1451)
    ActiveSessionGuard activeSessionGuard(*this, &*resumed);
    resetLaunchDiagnostic(mBridgedDiagnoser);
    std::string checkpointErr;
    if (!request.journalDirectory.empty())
        installCheckpointSink(*resumed, journalDirectory, mRecorder, checkpointErr);

    // The loop restarts at the journal's final stage (pre-plan only) and
    // re-executes no journalled work; run() must restate the journalled goal.
    auto result = resumed->run(request.session);
    return finish(std::move(result), request, std::nullopt, std::nullopt, checkpointErr);
}

RecoveryDecision OperationsCoordinator::evaluateRecovery(const OpDiagnostic &diagnostic,
                                                         RecoveryContext &ctx)
{
    // The human gate is the armed token, never the caller's bool: verify
    // and consume it here, then derive what the bridge may trust. With
    // nothing armed the caller's assertion is cleared — a forged ctx
    // (the findings digest is public wire content) must not arm the gate.
    std::string tokenErrorOut;
    if (!mPendingRepairApproval)
    {
        if (ctx.humanApprovedRepair || !ctx.approvedFindingsDigest.empty())
        {
            ctx.humanApprovedRepair = false;
            ctx.approvedFindingsDigest.clear();
            tokenErrorOut = "APPROVAL_REQUIRED";
        }
    }
    else
    {
        const std::string targetDigest = lastProjectedFindingsDigest();
        const char *check = verifyRepairApprovalToken(
            mPendingRepairApproval->token, targetDigest, mInstanceId, ctx.approvalNowMs);
        std::string tokenError;
        if (std::string(check) == approval_check::kOk)
        {
            ctx.humanApprovedRepair = true;
            ctx.approvedFindingsDigest = targetDigest;
        }
        else
        {
            ctx.humanApprovedRepair = false;
            ctx.approvedFindingsDigest.clear();
            tokenError = approvalCheckToError(check);
        }
        if (mConsumedApprovalDigests.size() >= kMaxConsumedDigests)
            mConsumedApprovalDigests.erase(mConsumedApprovalDigests.begin());
        if (mPendingRepairApproval->token.isObject() &&
            mPendingRepairApproval->token["digest"].isString())
        {
            mConsumedApprovalDigests.push_back(mPendingRepairApproval->token["digest"].asString());
        }
        mPendingRepairApproval.reset();
        tokenErrorOut = tokenError;
    }
    RecoveryDecision decision = mRecovery.decide(diagnostic, ctx);
    // The refusal must be visible even when the decision itself is safe —
    // a driver cannot otherwise tell an applied approval from a refused one.
    if (!tokenErrorOut.empty() && decision.approvalError.empty())
        decision.approvalError = tokenErrorOut;
    // Remember what this coordinator projected last: the surface's
    // approve_repair binds its token to exactly this repair science.
    if (decision.repairPlan.isObject() && decision.repairPlan["provenance"].isObject() &&
        decision.repairPlan["provenance"]["findings_digest"].isString())
        mLastProjectedFindingsDigest =
            decision.repairPlan["provenance"]["findings_digest"].asString();
    return decision;
}

OpsProjection OperationsCoordinator::timeline(const sicnu::agent_loop::SessionJournal &journal,
                                              const OpsBudget &budgets) const
{
    return mProjector.project(journal, budgets);
}

} // namespace sicnu::agent_ops
