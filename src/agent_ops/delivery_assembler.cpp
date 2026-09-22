// src/agent_ops/delivery_assembler.cpp
#include "agent_ops/delivery_assembler.h"
#include "agentbench/trace.h"

namespace sicnu::agent_ops {

FinalDelivery DeliveryAssembler::assemble(const sicnu::agent_loop::SessionResult &result,
                                          const DeliveryExtras &extras) const
{
    FinalDelivery d;
    d.sessionId = result.sessionId;
    d.goal = result.summary.goal;
    d.outcome = result.summary.outcome.empty() ? result.terminalState : result.summary.outcome;
    d.stopReason = result.stopReason.empty() ? result.summary.stopReason : result.stopReason;
    d.budgets = result.summary.budgets;
    d.verifier["verdict"] = result.summary.verificationVerdict;
    d.capsule = extras.capsule;
    d.benchmarkRefs = extras.benchmarkRefs;
    d.questions = extras.questions;
    d.claims = extras.claims;

    for (const auto &stage : result.summary.stages)
        d.stages.append(stage);
    for (const auto &dec : result.summary.decisions)
        d.decisions.append(dec.toJson());
    for (const auto &art : result.summary.artifacts)
    {
        Json::Value o(Json::objectValue);
        o["path"] = art;
        // Portability: flag absolute paths but keep basename-ish ref.
        o["portable_ref"] = art;
        d.outputs.append(o);
        d.provenance["artifacts"].append(art);
    }

    // Pull plan / run ids from decisions when present.
    for (const auto &dec : result.summary.decisions)
    {
        if (dec.selected.isMember("plan_id"))
            d.plan["plan_id"] = dec.selected["plan_id"];
        if (dec.selected.isMember("run_id"))
            d.runIds.append(dec.selected["run_id"]);
        if (dec.selected.isMember("action"))
        {
            const std::string action = dec.selected["action"].asString();
            if (action.find("warn") != std::string::npos)
                d.warnings.append(dec.toJson());
        }
    }
    d.plan["mode"] = result.summary.mode;
    d.journalReplay = result.summary.replay;

    if (extras.trace)
    {
        d.traceRef["trace_id"] = extras.trace->traceId;
        d.traceRef["schema"] = sicnu::agentbench::kAgentTraceSchemaTag;
        d.traceRef["stop_reason"] = sicnu::agentbench::stopReasonToString(extras.trace->stopReason);
        d.traceRef["outcome_claim_success"] = extras.trace->outcomeClaimSuccess;
    }

    // Claims with confidence: delivered => high; refused/aborted => low; unknown ≠ success.
    if (d.claims.empty())
    {
        Json::Value claim(Json::objectValue);
        claim["claim"] = "session_outcome";
        claim["value"] = d.outcome;
        if (d.outcome == "delivered")
            claim["confidence"] = 0.9;
        else if (d.outcome == "refused" || d.outcome == "aborted")
            claim["confidence"] = 0.85;
        else
            claim["confidence"] = 0.0; // indeterminate
        claim["evidence_ref"] = "journal:" + d.sessionId;
        d.claims.append(claim);
    }
    return d;
}

Json::Value DeliveryAssembler::capsuleExportDocument(const FinalDelivery &delivery) const
{
    Json::Value doc(Json::objectValue);
    doc["schema"] = "sicnu.agent_ops.capsule_export/v1";
    doc["session_id"] = delivery.sessionId;
    doc["goal"] = delivery.goal;
    doc["outcome"] = delivery.outcome;
    doc["stop_reason"] = delivery.stopReason;
    doc["plan"] = delivery.plan;
    doc["verifier"] = delivery.verifier;
    doc["claims"] = delivery.claims;
    doc["run_ids"] = delivery.runIds;
    doc["stages"] = delivery.stages;
    doc["trace_ref"] = delivery.traceRef;
    // Strip absolute-looking path strings from outputs → portable refs only.
    Json::Value outs(Json::arrayValue);
    for (const auto &o : delivery.outputs)
    {
        Json::Value p(Json::objectValue);
        p["portable_ref"] = o.get("portable_ref", o.get("path", ""));
        outs.append(p);
    }
    doc["outputs"] = outs;
    doc["benchmark_refs"] = delivery.benchmarkRefs;
    if (!delivery.capsule.isNull())
        doc["capsule"] = delivery.capsule;
    return doc;
}

} // namespace sicnu::agent_ops
