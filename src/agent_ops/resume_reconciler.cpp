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
    for (const auto &id : successfulRunIds)
        runs.append(id);
    doc["successful_run_ids"] = runs;
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

    // Collect successful execute decisions / run ids.
    for (const auto &entry : journal.entries())
    {
        if (!entry.decision)
            continue;
        const auto &sel = entry.decision->selected;
        if (sel.isMember("run_id") && sel.get("succeeded", false).asBool())
            r.successfulRunIds.insert(sel["run_id"].asString());
        if (entry.stage == "execute" && sel.get("action", "").asString() == "execute_succeeded")
        {
            if (sel.isMember("run_id"))
                r.successfulRunIds.insert(sel["run_id"].asString());
        }
    }

    if (!replay.terminalState.empty())
    {
        r.ok = true;
        r.resumable = false;
        r.reasonCode = "ALREADY_TERMINAL";
        // Delivered sessions must not re-submit.
        r.duplicateSubmitRisk = (replay.terminalState == "delivered");
        return r;
    }

    // After execute before verify: resumable at verify; do not re-begin execute
    // if a successful run id is already recorded.
    if (replay.finalStage == "execute" && !r.successfulRunIds.empty())
    {
        r.ok = true;
        r.resumable = true;
        r.duplicateSubmitRisk = true; // coordinator must skip re-submit
        r.reasonCode = "RESUME_AFTER_EXECUTE_SKIP_RESUBMIT";
        return r;
    }

    if (replay.finalStage == "verify" || replay.finalStage == "diagnose" ||
        replay.finalStage == "replan" || replay.finalStage == "delivery" ||
        replay.finalStage == "preflight" || replay.finalStage == "plan_request" ||
        replay.finalStage == "repair_approval" || replay.finalStage == "data_state_snapshot" ||
        replay.finalStage == "goal_normalization")
    {
        r.ok = true;
        r.resumable = true;
        r.reasonCode = "RESUMABLE";
        return r;
    }

    // Unknown stage / empty — not success.
    r.ok = false;
    r.resumable = false;
    r.reasonCode = "INDETERMINATE_STATE";
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
