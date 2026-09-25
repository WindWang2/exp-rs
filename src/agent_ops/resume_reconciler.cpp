// src/agent_ops/resume_reconciler.cpp
#include "agent_ops/resume_reconciler.h"

namespace sicnu::agent_ops {

Json::Value ReconcileResult::toJson() const
{
    Json::Value doc(Json::objectValue);
    doc["ok"] = ok;
    doc["resumable"] = resumable;
    doc["duplicate_submit_risk"] = duplicateSubmitRisk;
    doc["reason_code"] = reasonCode;
    doc["stage"] = stage;
    doc["terminal_state"] = terminalState;
    Json::Value runs(Json::arrayValue);
    for (const auto &id : submittedRunIds)
        runs.append(id);
    doc["submitted_run_ids"] = runs;
    doc["details"] = details;
    return doc;
}

ReconcileResult ResumeReconciler::reconcile(const sicnu::agent_loop::SessionJournal &journal) const
{
    ReconcileResult r;
    if (journal.sessionId().empty())
    {
        r.reasonCode = "EMPTY_SESSION";
        return r;
    }
    const auto replay = journal.replay();
    r.stage = replay.finalStage;
    r.terminalState = replay.terminalState;
    r.details["replan_count"] = replay.replanCount;
    r.details["entries"] = static_cast<Json::UInt64>(journal.size());

    // Collect run ids from the loop's recorded wire shape: the execute
    // stage records the SUBMISSION as decision.inputs["run_id"] with
    // selected["action"] == "run" (scientific_agent_session stageExecute),
    // before any outcome exists — so these are submitted runs, not
    // successful ones.
    for (const auto &entry : journal.entries())
    {
        if (!entry.decision || entry.stage != "execute")
            continue;
        const auto &decision = *entry.decision;
        if (decision.selected.get("action", "").asString() == "run" &&
            decision.inputs.isMember("run_id"))
        {
            const std::string runId = decision.inputs["run_id"].asString();
            if (!runId.empty())
                r.submittedRunIds.insert(runId);
        }
    }

    if (!replay.terminalState.empty())
    {
        r.ok = true;
        r.resumable = false;
        r.reasonCode = "ALREADY_TERMINAL";
        // Delivered sessions must not re-submit.
        r.duplicateSubmitRisk = (replay.terminalState == "delivered");
        r.details["submitted_run_ids"] = static_cast<Json::UInt64>(r.submittedRunIds.size());
        return r;
    }

    // Loop authority (ScientificAgentSession::resume): only PRE-PLAN stages
    // carry enough journalled state to resume; everything from plan_request
    // onward would run real seams over default-constructed state and
    // fabricate a delivery, so the loop refuses it. The reconciler mirrors
    // that contract for non-terminal journals (it stays deliberately
    // conservative on terminal ones: a cancelled pre-plan abort could
    // resume, but restarting as a new session is always safe).
    if (replay.finalStage == "goal_normalization" ||
        replay.finalStage == "data_state_snapshot")
    {
        r.ok = true;
        r.resumable = true;
        r.reasonCode = "RESUMABLE";
        return r;
    }

    if (replay.finalStage.empty())
    {
        // Unknown stage / empty — not success.
        r.ok = false;
        r.resumable = false;
        r.reasonCode = "INDETERMINATE_STATE";
        return r;
    }

    r.ok = true;
    r.resumable = false;
    r.reasonCode = "RESUME_PAST_PLAN_SEAM";
    // The restart hazard is distinct from the resume contract: this journal
    // already SUBMITTED engine runs (recorded at submission, before any
    // outcome), so restarting it as a new session would re-do that science.
    // The loop still refuses to resume past the plan seam — the flag and the
    // run-id list only make the hazard visible to the driver instead of
    // leaving a blind restart as the only next step.
    r.duplicateSubmitRisk = !r.submittedRunIds.empty();
    r.details["resume_constraint"] =
        "agent_loop resumes only goal_normalization/data_state_snapshot; "
        "restart as a new session";
    r.details["submitted_run_ids"] = static_cast<Json::UInt64>(r.submittedRunIds.size());
    return r;
}

ReconcileResult ResumeReconciler::reconcileFile(const std::string &directory,
                                                const std::string &sessionId,
                                                std::string *loadError) const
{
    std::string err;
    auto journal = sicnu::agent_loop::SessionJournal::load(directory, sessionId, &err);
    if (!journal)
    {
        ReconcileResult r;
        r.ok = false;
        r.resumable = false;
        r.reasonCode = "CORRUPTED_OR_MISSING_JOURNAL";
        r.details["load_error"] = err;
        if (loadError)
            *loadError = err;
        return r;
    }
    return reconcile(*journal);
}

} // namespace sicnu::agent_ops
