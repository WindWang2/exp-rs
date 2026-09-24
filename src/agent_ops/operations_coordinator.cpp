// src/agent_ops/operations_coordinator.cpp
#include "agent_ops/operations_coordinator.h"

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

} // namespace

OperationsCoordinator::OperationsCoordinator(Dependencies deps,
                                             LiveSessionRecorder::Options recorderOptions)
    : mDeps(std::move(deps)), mRecorder(std::move(recorderOptions)),
      mBridgedDiagnoser(mDeps.seams.diagnoser)
{
}

OpsRunResult OperationsCoordinator::finish(sicnu::agent_loop::SessionResult &&session,
                                           const OpsRunRequest &request,
                                           const std::optional<OpDiagnostic> &diag,
                                           const std::optional<RecoveryDecision> &recovery)
{
    OpsRunResult out;
    out.session = std::move(session);
    out.lastDiagnostic = diag;
    out.lastRecovery = recovery;
    out.ok = out.session.ok;

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
    out.delivery = mDelivery.assemble(out.session, extras);
    out.projection = mProjector.projectResult(out.session, request.budgets);
    out.reconcile = mReconciler.reconcile(out.session.journal);

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
        OpsAutonomyRequest ar;
        ar.mutateKind = ops_mutate::kExecute;
        ar.domain = request.domain;
        ar.role = request.role;
        ar.intent = request.session.intent;
        ar.actionKey = "ops:run";
        const auto gate = gateMutatingOp(mDeps.autonomyPolicy, ar);
        if (!gate.allowed)
        {
            denied.ok = false;
            denied.error = gate.reasonCode;
            denied.lastRecovery = RecoveryDecision{};
            denied.lastRecovery->action = recovery_action::kAbort;
            denied.lastRecovery->reasonCode = gate.reasonCode;
            denied.lastRecovery->autonomyAllowed = false;
            denied.lastRecovery->autonomyReasonCode = gate.reasonCode;
            return denied;
        }
    }

    // Wire bridged diagnoser when an inner diagnoser is present.
    sicnu::agent_loop::ScientificAgentSession::Dependencies seams = mDeps.seams;
    if (seams.diagnoser)
        seams.diagnoser = &mBridgedDiagnoser;

    // One-shot: the surface-recorded repair approval is consumed by this
    // launch whatever the outcome — the human gate must re-arm explicitly,
    // otherwise a single approval would silence the science-changing repair
    // gate for every later session.
    const bool pendingApproval = mPendingRepairApproval;
    mPendingRepairApproval = false;

    sicnu::agent_loop::SessionPolicy policy = request.policy;
    policy.maxReplans = request.budgets.maxReplans;
    policy.noProgressThreshold = request.budgets.noProgressThreshold;
    if (request.budgets.resourceBudgetMb > 0)
        policy.resourceBudgetMb = request.budgets.resourceBudgetMb;

    sicnu::agent_loop::ScientificAgentSession session(policy, seams);
    if (mCancelRequested.load())
        session.requestCancel();
    // The bridge cache is evidence about one diagnose invocation only; a
    // stale entry from a previous session must never be attributed here.
    mBridgedDiagnoser.resetLastDiagnostic();

    // Note: pause is checked at launch (above); cancel remains the hard stop
    // inside the loop. AgentLoop owns the state machine while it runs.
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
                    dec.inputs.get("root_cause_code", "").asString().empty())
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
        ctx.humanApprovedRepair = request.approvePendingRepair || pendingApproval;
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

    return finish(std::move(result), request, diag, recovery);
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

    std::string loadErr;
    out.reconcile = mReconciler.reconcileFile(journalDirectory, sessionId, &loadErr);
    if (!out.reconcile.ok || !out.reconcile.resumable)
    {
        out.ok = false;
        // Surface the reconciler's typed reason: the reconciler projects the
        // loop's resume contract, so its reason is the answer (a past-plan
        // journal reports RESUME_PAST_PLAN_SEAM, a delivered one
        // ALREADY_TERMINAL / DUPLICATE_SUBMIT_REFUSED).
        out.error = out.reconcile.duplicateSubmitRisk ? "DUPLICATE_SUBMIT_REFUSED"
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

    sicnu::agent_loop::SessionPolicy policy = request.policy;
    policy.maxReplans = request.budgets.maxReplans;
    auto resumed = sicnu::agent_loop::ScientificAgentSession::resume(*journal, policy, seams);
    if (!resumed)
    {
        out.ok = false;
        out.error = "RESUME_REJECTED";
        return out;
    }
    if (mCancelRequested.load())
        resumed->requestCancel();
    mBridgedDiagnoser.resetLastDiagnostic();

    // The loop restarts at the journal's final stage (pre-plan only) and
    // re-executes no journalled work; run() must restate the journalled goal.
    auto result = resumed->run(request.session);
    return finish(std::move(result), request, std::nullopt, std::nullopt);
}

RecoveryDecision OperationsCoordinator::evaluateRecovery(const OpDiagnostic &diagnostic,
                                                         const RecoveryContext &ctx) const
{
    return mRecovery.decide(diagnostic, ctx);
}

OpsProjection OperationsCoordinator::timeline(const sicnu::agent_loop::SessionJournal &journal,
                                              const OpsBudget &budgets) const
{
    return mProjector.project(journal, budgets);
}

} // namespace sicnu::agent_ops
