// src/agent_ops/live_session_recorder.cpp
#include "agent_ops/live_session_recorder.h"
#include "agent_ops/secret_redactor.h"
#include "agentbench/json_writer.h"

#include <json/json.h>

namespace sicnu::agent_ops {

std::optional<sicnu::agentbench::AgentTrace> LiveSessionRecorder::projectTrace(
    const sicnu::agent_loop::SessionJournal &journal, const std::string &caseId,
    std::string *error) const
{
    if (journal.sessionId().empty())
    {
        if (error)
            *error = "empty session id";
        return std::nullopt;
    }

    sicnu::agentbench::AgentTrace trace;
    trace.traceId = "trace-" + journal.sessionId();
    trace.caseId = caseId.empty() ? "live" : caseId;
    trace.agentName = mOptions.agentName;
    trace.agentKind = sicnu::agentbench::AgentKind::Live;
    trace.agentVersion = mOptions.agentVersion;
    trace.seed = 0;

    int index = 0;
    bool anyTruncated = false;
    for (const auto &entry : journal.entries())
    {
        sicnu::agentbench::TraceStep step;
        step.index = index++;
        step.tool = entry.event.empty() ? "journal" : entry.event;
        Json::Value input(Json::objectValue);
        input["stage"] = entry.stage;
        input["seq"] = static_cast<Json::Int64>(entry.seq);
        input["at"] = static_cast<Json::Int64>(entry.at);
        step.input = input;

        bool truncated = false;
        Json::Value payload = redactAndBound(entry.payload, mOptions.maxPayloadChars, &truncated);
        if (truncated)
            anyTruncated = true;
        if (entry.decision)
        {
            payload["decision"] =
                redactAndBound(entry.decision->toJson(), mOptions.maxPayloadChars, &truncated);
            if (truncated)
                anyTruncated = true;
        }
        step.payload = payload;
        const bool payloadObj = entry.payload.isObject();
        const std::string terminalState =
            payloadObj ? entry.payload.get("terminal_state", "").asString() : "";
        step.success = entry.event != "terminal" || terminalState == "delivered";
        if (!step.success && payloadObj && entry.payload.isMember("stop_reason"))
            step.errorCode = entry.payload["stop_reason"].asString();
        else if (!step.success && entry.event == "terminal")
            step.errorCode = "SESSION_TERMINAL_NON_DELIVERED";
        step.tokens = 0;
        trace.steps.push_back(std::move(step));
    }

    const auto replay = journal.replay();
    if (replay.terminalState == "delivered")
    {
        trace.outcomeClaimSuccess = true;
        trace.stopReason = sicnu::agentbench::StopReason::Completed;
        trace.outcomeClaimNote = "delivered";
    }
    else if (replay.terminalState == "refused" || replay.terminalState == "aborted")
    {
        trace.outcomeClaimSuccess = false;
        trace.stopReason = sicnu::agentbench::StopReason::Blocked;
        trace.outcomeClaimNote = replay.stopReason.empty() ? replay.terminalState : replay.stopReason;
    }
    else
    {
        // Unknown / in-progress ≠ success.
        trace.outcomeClaimSuccess = false;
        trace.stopReason = sicnu::agentbench::StopReason::GaveUp;
        trace.outcomeClaimNote = "indeterminate_or_in_progress";
    }

    if (anyTruncated)
    {
        sicnu::agentbench::TraceEvidence ev;
        ev.id = "truncation";
        ev.kind = "report";
        ev.fields["truncated_payloads"] = true;
        trace.evidence.push_back(ev);
    }

    Json::Value raw = sicnu::agentbench::traceToJson(trace);
    const std::string bytes = sicnu::agentbench::deterministicSerialize(raw);
    if (bytes.size() > mOptions.maxTraceBytes)
    {
        // Compact: drop payload bodies but keep envelopes + fault markers.
        for (auto &step : trace.steps)
        {
            Json::Value compact(Json::objectValue);
            compact["compacted"] = true;
            if (step.payload.isObject() && step.payload.isMember("fault"))
                compact["fault"] = step.payload["fault"];
            if (step.payload.isObject() && step.payload.isMember("decision"))
            {
                Json::Value d = step.payload["decision"];
                Json::Value thin(Json::objectValue);
                if (d.isObject())
                {
                    thin["decision_id"] = d.get("decision_id", "");
                    thin["stage"] = d.get("stage", "");
                    thin["selected"] = d.get("selected", Json::objectValue);
                }
                compact["decision"] = thin;
            }
            step.payload = compact;
        }
        raw = sicnu::agentbench::traceToJson(trace);
        const std::string compacted = sicnu::agentbench::deterministicSerialize(raw);
        if (compacted.size() > mOptions.maxTraceBytes)
        {
            if (error)
                *error = "trace exceeds budget after compaction";
            return std::nullopt;
        }
    }
    trace.raw = raw;
    return trace;
}

bool LiveSessionRecorder::persistJournal(const sicnu::agent_loop::SessionJournal &journal,
                                         const std::string &directory, std::string *error) const
{
    return journal.save(directory, error);
}

std::optional<sicnu::agent_loop::SessionJournal> LiveSessionRecorder::loadJournal(
    const std::string &directory, const std::string &sessionId, std::string *error) const
{
    return sicnu::agent_loop::SessionJournal::load(directory, sessionId, error);
}

} // namespace sicnu::agent_ops
