// src/agent_ops/operations_coordinator.cpp
#include "agent_ops/operations_coordinator.h"

namespace sicnu::agent_ops {

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

    DeliveryExtras extras;
    if (out.trace)
        extras.trace = out.trace;
    out.delivery = mDelivery.assemble(out.session, extras);
    out.projection = mProjector.projectResult(out.session, request.budgets);
    out.reconcile = mReconciler.reconcile(out.session.journal);
    return out;
}

OpsRunResult OperationsCoordinator::run(const OpsRunRequest &request)
{
    OpsRunResult denied;
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

    // Note: pause is cooperative at coordinator level for future multi-stage
    // drivers; AgentLoop cancel remains the hard stop.
    auto result = session.run(request.session);

    std::optional<OpDiagnostic> diag;
    std::optional<RecoveryDecision> recovery;
    if (!result.ok)
    {
        DiagnosticInputs inputs;
        if (!result.summary.verificationVerdict.empty() &&
            result.summary.verificationVerdict == "FAIL")
        {
            // Reconstruct a minimal verification FAIL signal for the bridge.
            sicnu::agent_loop::VerificationReport vr;
            vr.verdictValue = "FAIL";
            inputs.verification = vr;
        }
        // Prefer bridged last diagnostic when available.
        if (mBridgedDiagnoser.lastOpsDiagnostic())
            diag = mBridgedDiagnoser.lastOpsDiagnostic();
        else
        {
            // Fall back: search decisions for diagnosis evidence.
            for (const auto &dec : result.summary.decisions)
            {
                if (dec.stage == "diagnose" && dec.evidence.size() > 0)
                {
                    OpDiagnostic d;
                    d.code = "ops.diagnose.FROM_JOURNAL";
                    d.rootCauseCode = dec.selected.get("root_cause", "UNKNOWN").asString();
                    if (d.rootCauseCode.empty() || d.rootCauseCode == "UNKNOWN")
                        d.rootCauseCode = result.stopReason.empty() ? "SESSION_FAILED" : result.stopReason;
                    d.confidence = 0.6;
                    d.summary = dec.reason;
                    d.repairable = true;
                    d.advisoryNext = recovery_action::kReplan;
                    diag = d;
                }
            }
            if (!diag)
            {
                OpDiagnostic d;
                d.code = "ops.session.FAILED";
                d.rootCauseCode = result.stopReason.empty() ? "SESSION_FAILED" : result.stopReason;
                d.confidence = 0.5;
                d.repairable = true;
                d.advisoryNext = recovery_action::kReplan;
                d.summary = "session did not deliver";
                diag = d;
            }
        }

        RecoveryContext ctx;
        ctx.budgets = request.budgets;
        ctx.domain = request.domain;
        ctx.role = request.role;
        ctx.intent = request.session.intent;
        ctx.cancelRequested = mCancelRequested.load();
        ctx.leadingRiskClass = request.leadingRepairRiskClass;
        ctx.humanApprovedRepair = request.approvePendingRepair;
        ctx.autonomyPolicy = mDeps.autonomyPolicy;
        ctx.replanCount = 0;
        for (const auto &s : result.summary.stages)
            if (s == "replan")
                ++ctx.replanCount;
        recovery = mRecovery.decide(*diag, ctx);
    }

    return finish(std::move(result), request, diag, recovery);
}

OpsRunResult OperationsCoordinator::resume(const std::string &journalDirectory,
                                           const std::string &sessionId,
                                           const OpsRunRequest &request)
{
    OpsRunResult out;
    std::string loadErr;
    out.reconcile = mReconciler.reconcileFile(journalDirectory, sessionId, &loadErr);
    if (!out.reconcile.ok || !out.reconcile.resumable)
    {
        out.ok = false;
        out.error = out.reconcile.reasonCode;
        if (out.reconcile.duplicateSubmitRisk)
            out.error = "DUPLICATE_SUBMIT_REFUSED";
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

    // If reconcile said skip resubmit, we still let the loop advance from
    // its journal stage — AgentLoop resume does not re-execute completed work.
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
