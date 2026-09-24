// src/agent_ops/ops_projection.cpp
#include "agent_ops/ops_projection.h"

namespace sicnu::agent_ops {

Json::Value OpsProjection::toJson() const
{
    Json::Value doc(Json::objectValue);
    doc["kind"] = kind;
    doc["session_id"] = sessionId;
    doc["current_stage"] = currentStage;
    doc["terminal_state"] = terminalState;
    doc["stop_reason"] = stopReason;
    doc["header"] = header;
    doc["timeline"] = timeline;
    doc["current_decision"] = currentDecision;
    doc["resources"] = resources;
    doc["evidence"] = evidence;
    doc["controls"] = controls;
    return doc;
}

OpsProjection OpsProjector::project(const sicnu::agent_loop::SessionJournal &journal,
                                    const OpsBudget &budgets, bool pauseAvailable) const
{
    OpsProjection p;
    p.sessionId = journal.sessionId();
    const auto replay = journal.replay();
    p.currentStage = replay.finalStage;
    p.terminalState = replay.terminalState;
    p.stopReason = replay.stopReason;
    p.header["session_id"] = journal.sessionId();
    p.header["entries"] = static_cast<Json::UInt64>(journal.size());
    p.header["replan_count"] = replay.replanCount;
    p.resources["max_retries"] = budgets.maxRetries;
    p.resources["max_replans"] = budgets.maxReplans;
    p.resources["max_repairs"] = budgets.maxRepairs;
    p.resources["resource_budget_mb"] = static_cast<Json::Int64>(budgets.resourceBudgetMb);

    for (const auto &entry : journal.entries())
    {
        Json::Value row(Json::objectValue);
        row["seq"] = static_cast<Json::Int64>(entry.seq);
        row["stage"] = entry.stage;
        row["event"] = entry.event;
        row["at"] = static_cast<Json::Int64>(entry.at);
        if (entry.decision)
        {
            row["summary"] = entry.decision->reason;
            row["decision_id"] = entry.decision->decisionId;
            p.currentDecision = entry.decision->toJson();
            p.evidence["last_decision"] = entry.decision->toJson();
        }
        else if (entry.payload.isObject() && entry.payload.isMember("stop_reason") &&
                 entry.payload["stop_reason"].isString())
            row["summary"] = entry.payload["stop_reason"].asString();
        else
            row["summary"] = entry.event;
        p.timeline.append(row);
    }

    const bool terminal = !replay.terminalState.empty();
    p.controls["pause"] = pauseAvailable && !terminal;
    p.controls["cancel"] = !terminal;
    // Honest resume advisory: only the loop's pre-plan stages are
    // resumable; anything past the plan seam restarts as a new session.
    p.controls["resume"] =
        !terminal && (replay.finalStage == "goal_normalization" ||
                      replay.finalStage == "data_state_snapshot");
    p.controls["approve_repair"] = !terminal;
    p.controls["export"] = true;
    // Explicit: UI cannot bypass loop — controls are advisory to coordinator.
    p.controls["bypasses_loop"] = false;
    return p;
}

OpsProjection OpsProjector::projectResult(const sicnu::agent_loop::SessionResult &result,
                                          const OpsBudget &budgets) const
{
    OpsProjection p = project(result.journal, budgets, false);
    p.terminalState = result.terminalState;
    p.stopReason = result.stopReason;
    p.evidence["summary"] = result.summary.toJson();
    p.controls["pause"] = false;
    p.controls["cancel"] = false;
    p.controls["resume"] = false;
    return p;
}

} // namespace sicnu::agent_ops
