// src/agent_ops/ops_types.cpp
#include "agent_ops/ops_types.h"

namespace sicnu::agent_ops {
namespace {

bool requireObject(const Json::Value &doc, std::string *error, const char *what)
{
    if (doc.isObject())
        return true;
    if (error)
        *error = std::string(what) + " must be an object";
    return false;
}

} // namespace

Json::Value OpDiagnostic::toJson() const
{
    Json::Value doc(Json::objectValue);
    doc["schema_version"] = schemaVersion;
    doc["kind"] = kind;
    doc["code"] = code;
    doc["root_cause_code"] = rootCauseCode;
    doc["confidence"] = confidence;
    doc["repairable"] = repairable;
    doc["retryable"] = retryable;
    doc["advisory_next"] = advisoryNext;
    doc["evidence"] = evidence;
    doc["sources"] = sources;
    doc["summary"] = summary;
    Json::Value props(Json::arrayValue);
    for (const auto &p : proposals)
        props.append(p);
    doc["proposals"] = props;
    doc["proposal_details"] = proposalDetails;
    return doc;
}

std::optional<OpDiagnostic> OpDiagnostic::fromJson(const Json::Value &doc, std::string *error)
{
    if (!requireObject(doc, error, "OpDiagnostic"))
        return std::nullopt;
    if (doc.get("kind", "").asString() != kOpDiagnosticKind)
    {
        if (error)
            *error = "unknown diagnostic kind";
        return std::nullopt;
    }
    OpDiagnostic d;
    d.schemaVersion = doc.get("schema_version", kOpsSchemaVersion).asString();
    d.code = doc.get("code", "").asString();
    d.rootCauseCode = doc.get("root_cause_code", "").asString();
    d.confidence = doc.get("confidence", 0.0).asDouble();
    d.repairable = doc.get("repairable", false).asBool();
    d.retryable = doc.get("retryable", false).asBool();
    d.advisoryNext = doc.get("advisory_next", "").asString();
    d.evidence = doc.get("evidence", Json::objectValue);
    d.sources = doc.get("sources", Json::objectValue);
    d.summary = doc.get("summary", "").asString();
    if (doc.isMember("proposals") && doc["proposals"].isArray())
    {
        for (const auto &p : doc["proposals"])
            if (p.isString())
                d.proposals.push_back(p.asString());
    }
    if (doc.isMember("proposal_details") && doc["proposal_details"].isArray())
    {
        for (const auto &p : doc["proposal_details"])
        {
            if (!p.isObject() || !p.isMember("rule_id") || !p["rule_id"].isString())
                continue;
            d.proposalDetails.append(p);
        }
    }
    if (d.code.empty() || d.rootCauseCode.empty())
    {
        if (error)
            *error = "diagnostic requires code and root_cause_code";
        return std::nullopt;
    }
    return d;
}

Json::Value RecoveryDecision::toJson() const
{
    Json::Value doc(Json::objectValue);
    doc["schema_version"] = schemaVersion;
    doc["kind"] = kind;
    doc["action"] = action;
    doc["reason_code"] = reasonCode;
    doc["autonomy_allowed"] = autonomyAllowed;
    doc["autonomy_reason_code"] = autonomyReasonCode;
    doc["needs_approval"] = needsApproval;
    doc["attempt"] = attempt;
    doc["replan_count"] = replanCount;
    doc["repair_count"] = repairCount;
    doc["retry_count"] = retryCount;
    doc["repair_plan"] = repairPlan;
    doc["diagnostic"] = diagnostic;
    doc["budgets"] = budgets;
    return doc;
}

Json::Value FinalDelivery::toJson() const
{
    Json::Value doc(Json::objectValue);
    doc["schema_version"] = schemaVersion;
    doc["kind"] = kind;
    doc["session_id"] = sessionId;
    doc["goal"] = goal;
    doc["outcome"] = outcome;
    doc["stop_reason"] = stopReason;
    doc["plan"] = plan;
    doc["outputs"] = outputs;
    doc["verifier"] = verifier;
    doc["warnings"] = warnings;
    doc["questions"] = questions;
    doc["provenance"] = provenance;
    doc["run_ids"] = runIds;
    doc["capsule"] = capsule;
    doc["benchmark_refs"] = benchmarkRefs;
    doc["claims"] = claims;
    doc["budgets"] = budgets;
    doc["stages"] = stages;
    doc["decisions"] = decisions;
    doc["trace_ref"] = traceRef;
    doc["journal_replay"] = journalReplay;
    return doc;
}

std::optional<FinalDelivery> FinalDelivery::fromJson(const Json::Value &doc, std::string *error)
{
    if (!requireObject(doc, error, "FinalDelivery"))
        return std::nullopt;
    if (doc.get("kind", "").asString() != kFinalDeliveryKind)
    {
        if (error)
            *error = "unknown FinalDelivery kind";
        return std::nullopt;
    }
    FinalDelivery d;
    d.schemaVersion = doc.get("schema_version", kOpsSchemaVersion).asString();
    d.sessionId = doc.get("session_id", "").asString();
    d.goal = doc.get("goal", "").asString();
    d.outcome = doc.get("outcome", "").asString();
    d.stopReason = doc.get("stop_reason", "").asString();
    d.plan = doc.get("plan", Json::objectValue);
    d.outputs = doc.get("outputs", Json::arrayValue);
    d.verifier = doc.get("verifier", Json::objectValue);
    d.warnings = doc.get("warnings", Json::arrayValue);
    d.questions = doc.get("questions", Json::arrayValue);
    d.provenance = doc.get("provenance", Json::objectValue);
    d.runIds = doc.get("run_ids", Json::arrayValue);
    d.capsule = doc.get("capsule", Json::objectValue);
    d.benchmarkRefs = doc.get("benchmark_refs", Json::arrayValue);
    d.claims = doc.get("claims", Json::arrayValue);
    d.budgets = doc.get("budgets", Json::objectValue);
    d.stages = doc.get("stages", Json::arrayValue);
    d.decisions = doc.get("decisions", Json::arrayValue);
    d.traceRef = doc.get("trace_ref", Json::objectValue);
    d.journalReplay = doc.get("journal_replay", Json::objectValue);
    if (d.sessionId.empty())
    {
        if (error)
            *error = "FinalDelivery requires session_id";
        return std::nullopt;
    }
    return d;
}

} // namespace sicnu::agent_ops
