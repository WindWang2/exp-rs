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
    try
    {
        if (!doc.isMember("kind") || !doc["kind"].isString() ||
            doc["kind"].asString() != kOpDiagnosticKind)
        {
            if (error)
                *error = "unknown diagnostic kind";
            return std::nullopt;
        }
        OpDiagnostic d;
        d.schemaVersion = (doc.isMember("schema_version") && doc["schema_version"].isString())
                              ? doc["schema_version"].asString()
                              : kOpsSchemaVersion;
        d.code = (doc.isMember("code") && doc["code"].isString()) ? doc["code"].asString() : "";
        d.rootCauseCode = (doc.isMember("root_cause_code") && doc["root_cause_code"].isString())
                              ? doc["root_cause_code"].asString()
                              : "";
        d.confidence = (doc.isMember("confidence") && doc["confidence"].isNumeric())
                           ? doc["confidence"].asDouble()
                           : 0.0;
        d.repairable = (doc.isMember("repairable") && doc["repairable"].isBool())
                           ? doc["repairable"].asBool()
                           : false;
        d.retryable = (doc.isMember("retryable") && doc["retryable"].isBool())
                          ? doc["retryable"].asBool()
                          : false;
        d.advisoryNext = (doc.isMember("advisory_next") && doc["advisory_next"].isString())
                             ? doc["advisory_next"].asString()
                             : "";
        d.evidence = (doc.isMember("evidence") && doc["evidence"].isObject())
                         ? doc["evidence"]
                         : Json::objectValue;
        d.sources = (doc.isMember("sources") && doc["sources"].isObject())
                        ? doc["sources"]
                        : Json::objectValue;
        d.summary = (doc.isMember("summary") && doc["summary"].isString())
                        ? doc["summary"].asString()
                        : "";
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
    catch (const std::exception &ex)
    {
        if (error)
            *error = std::string("malformed OpDiagnostic: ") + ex.what();
        return std::nullopt;
    }
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
    doc["requires_reverification"] = requiresReverification;
    doc["approval_error"] = approvalError;
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
    try
    {
        if (!doc.isMember("kind") || !doc["kind"].isString() ||
            doc["kind"].asString() != kFinalDeliveryKind)
        {
            if (error)
                *error = "unknown FinalDelivery kind";
            return std::nullopt;
        }
        FinalDelivery d;
        d.schemaVersion = (doc.isMember("schema_version") && doc["schema_version"].isString())
                              ? doc["schema_version"].asString()
                              : kOpsSchemaVersion;
        d.sessionId = (doc.isMember("session_id") && doc["session_id"].isString())
                          ? doc["session_id"].asString()
                          : "";
        d.goal = (doc.isMember("goal") && doc["goal"].isString()) ? doc["goal"].asString() : "";
        d.outcome = (doc.isMember("outcome") && doc["outcome"].isString()) ? doc["outcome"].asString() : "";
        d.stopReason = (doc.isMember("stop_reason") && doc["stop_reason"].isString()) ? doc["stop_reason"].asString() : "";
        d.plan = (doc.isMember("plan") && doc["plan"].isObject()) ? doc["plan"] : Json::objectValue;
        d.outputs = (doc.isMember("outputs") && doc["outputs"].isArray()) ? doc["outputs"] : Json::arrayValue;
        d.verifier = (doc.isMember("verifier") && doc["verifier"].isObject()) ? doc["verifier"] : Json::objectValue;
        d.warnings = (doc.isMember("warnings") && doc["warnings"].isArray()) ? doc["warnings"] : Json::arrayValue;
        d.questions = (doc.isMember("questions") && doc["questions"].isArray()) ? doc["questions"] : Json::arrayValue;
        d.provenance = (doc.isMember("provenance") && doc["provenance"].isObject()) ? doc["provenance"] : Json::objectValue;
        d.runIds = (doc.isMember("run_ids") && doc["run_ids"].isArray()) ? doc["run_ids"] : Json::arrayValue;
        d.capsule = (doc.isMember("capsule") && doc["capsule"].isObject()) ? doc["capsule"] : Json::objectValue;
        d.benchmarkRefs = (doc.isMember("benchmark_refs") && doc["benchmark_refs"].isArray()) ? doc["benchmark_refs"] : Json::arrayValue;
        d.claims = (doc.isMember("claims") && doc["claims"].isArray()) ? doc["claims"] : Json::arrayValue;
        d.budgets = (doc.isMember("budgets") && doc["budgets"].isObject()) ? doc["budgets"] : Json::objectValue;
        d.stages = (doc.isMember("stages") && doc["stages"].isArray()) ? doc["stages"] : Json::arrayValue;
        d.decisions = (doc.isMember("decisions") && doc["decisions"].isArray()) ? doc["decisions"] : Json::arrayValue;
        d.traceRef = (doc.isMember("trace_ref") && doc["trace_ref"].isObject()) ? doc["trace_ref"] : Json::objectValue;
        d.journalReplay = (doc.isMember("journal_replay") && doc["journal_replay"].isObject()) ? doc["journal_replay"] : Json::objectValue;
        if (d.sessionId.empty())
        {
            if (error)
                *error = "FinalDelivery requires session_id";
            return std::nullopt;
        }
        return d;
    }
    catch (const std::exception &ex)
    {
        if (error)
            *error = std::string("malformed FinalDelivery: ") + ex.what();
        return std::nullopt;
    }
}

} // namespace sicnu::agent_ops
