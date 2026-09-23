// src/agent_ops/operations_coordinator.cpp
#include "agent_ops/operations_coordinator.h"

#include <chrono>

namespace sicnu::agent_ops {
namespace {

/// Longest run of the same non-empty diagnose root cause at the tail of the
/// recorded decisions — the journal-evidence projection of "no progress".
int tailIdenticalFailureCount(const sicnu::agent_loop::EvidenceSummary &summary)
{
    std::string last;
    int count = 0;
    for (const auto &dec : summary.decisions)
    {
        if (dec.stage != "diagnose")
            continue;
        const std::string code = dec.inputs.get("root_cause_code", "").asString();
        if (code.empty())
            continue;
        if (code == last)
            ++count;
        else
        {
            last = code;
            count = 1;
        }
    }
    return count;
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
        doc.resultId = "bpr-live-" + out.session.sessionId;
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

    sicnu::agent_loop::SessionPolicy policy = request.policy;
    policy.maxReplans = request.budgets.maxReplans;
    policy.noProgressThreshold = request.budgets.noProgressThreshold;
    if (request.budgets.resourceBudgetMb > 0)
        policy.resourceBudgetMb = request.budgets.resourceBudgetMb;

    sicnu::agent_loop::ScientificAgentSession session(policy, seams);
    if (mCancelRequested.load())
        session.requestCancel();

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
        // Structured evidence first, in decreasing strength: the bridged
        // diagnoser saw the real runtime/verification/diagnosis during the
        // run; next a reconstructed verification FAIL (the only verifier
        // signal SessionResult carries) through the evidence bridge. No
        // structured source -> no typed root cause -> nullopt.
        DiagnosticInputs inputs;
        if (result.summary.verificationVerdict == "FAIL")
        {
            sicnu::agent_loop::VerificationReport vr;
            vr.verdictValue = "FAIL";
            inputs.verification = vr;
        }
        if (mBridgedDiagnoser.lastOpsDiagnostic())
            diag = mBridgedDiagnoser.lastOpsDiagnostic();
        else
            diag = mDiagnostic.diagnose(inputs);

        if (!diag)
        {
            // Journal-decision fallback: the loop's own diagnose records carry
            // a typed root_cause_code — project it, never invent repairability
            // from it (proposals are the only repair evidence, and decisions
            // do not carry proposals).
            for (const auto &dec : result.summary.decisions)
            {
                if (dec.stage != "diagnose")
                    continue;
                OpDiagnostic d;
                d.code = "ops.diagnose.FROM_JOURNAL";
                d.rootCauseCode = dec.inputs.get("root_cause_code", "").asString();
                if (d.rootCauseCode.empty())
                    continue;
                d.confidence = 0.6;
                d.repairable = false;
                d.retryable = false;
                d.advisoryNext = recovery_action::kAsk;
                d.summary = dec.reason;
                d.evidence["decision_id"] = dec.decisionId;
                diag = d;
            }
        }

        if (!diag)
        {
            // No structured diagnostic evidence exists. The loop's typed stop
            // reason is still evidence; repairability is not.
            OpDiagnostic d;
            d.code = "ops.session.FAILED";
            d.rootCauseCode = result.stopReason.empty() ? "SESSION_FAILED" : result.stopReason;
            d.confidence = 0.3;
            d.repairable = false;
            d.retryable = false;
            d.advisoryNext = recovery_action::kAsk;
            d.summary = "session failed without structured diagnostic evidence";
            d.sources["session"]["stop_reason"] = d.rootCauseCode;
            d.evidence["evidence_unavailable"] = true;
            diag = d;
        }

        RecoveryContext ctx;
        ctx.budgets = request.budgets;
        ctx.domain = request.domain;
        ctx.role = request.role;
        ctx.intent = request.session.intent;
        ctx.cancelRequested = mCancelRequested.load();
        ctx.leadingRiskClass = request.leadingRepairRiskClass;
        ctx.humanApprovedRepair = request.approvePendingRepair || mPendingRepairApproval;
        ctx.autonomyPolicy = mDeps.autonomyPolicy;
        ctx.replanCount = 0;
        for (const auto &s : result.summary.stages)
            if (s == "replan")
                ++ctx.replanCount;
        ctx.identicalFailureCount = tailIdenticalFailureCount(result.summary);
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
