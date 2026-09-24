// src/agent_ops/delivery_assembler.cpp
#include "agent_ops/delivery_assembler.h"
#include "agentbench/trace.h"

#include <cstdint>
#include <cstdio>

namespace sicnu::agent_ops {
namespace {

/// Wire marker of the unified Scientific Verifier report document. Consumed
/// as a JSON contract here: the delivery layer projects it, it never
/// re-runs verification and never links the verifier.
bool isUnifiedVerificationReport(const Json::Value &doc)
{
    return doc.isObject() && doc.isMember("schema") && doc["schema"].isString()
           && doc["schema"].asString() == "sicnu.verification.report/1";
}

/// Stable 8-hex FNV-1a fingerprint over the full path (same scheme the
/// loop uses for plan identity).
std::string pathFingerprint(const std::string &canonical)
{
    std::uint64_t hash = 1469598103934665603ULL; // FNV-1a offset basis
    for (const unsigned char c : canonical)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buffer[9];
    std::snprintf(buffer, sizeof(buffer), "%08llx", static_cast<unsigned long long>(hash));
    return std::string(buffer, 8);
}

/// Portable ref: strips the longest leading directory component (POSIX and
/// Windows separators) so an export never leaks absolute machine paths, and
/// disambiguates basename collisions with a stable path fingerprint —
/// /r1/out.tif and /r2/out.tif stay distinguishable. A path that is already
/// a bare relative name passes through untouched.
std::string basenameRef(const std::string &path)
{
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return path;
    return path.substr(slash + 1) + "@" + pathFingerprint(path);
}

} // namespace

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

    // Project the unified verifier report when the caller supplies one: its
    // overall is the engine's fail-closed lattice, so anything that is not
    // "pass" — including "indeterminate" — records as a non-passing
    // verdict here. It never upgrades the loop's own verdict.
    bool unifiedPresent = false;
    bool unifiedPassed = false;
    if (isUnifiedVerificationReport(extras.verificationReport))
    {
        unifiedPresent = true;
        // The overall word is the fail-closed lattice; a non-string overall
        // (hostile/buggy document) is a non-pass, never an exception.
        const Json::Value &overall = extras.verificationReport["overall"];
        unifiedPassed = overall.isString() && overall.asString() == "pass";
        Json::Value unified(Json::objectValue);
        unified["spec_id"] = extras.verificationReport["specId"];
        unified["overall"] = overall;
        unified["verdict"] = unifiedPassed ? "PASS" : "FAIL";
        unified["counts"] = extras.verificationReport["counts"];
        d.verifier["unified"] = unified;
    }
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
        // Portability: the capsule carries a basename ref only; the raw
        // (absolute) path stays in the local delivery document.
        o["portable_ref"] = basenameRef(art);
        d.outputs.append(o);
        d.provenance["artifacts"].append(art);
    }

    // Pull plan / run ids from decisions when present. The loop records
    // submitted runs as decision.inputs["run_id"].
    for (const auto &dec : result.summary.decisions)
    {
        if (dec.selected.isMember("plan_id"))
            d.plan["plan_id"] = dec.selected["plan_id"];
        if (dec.inputs.isMember("run_id"))
            d.runIds.append(dec.inputs["run_id"]);
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

    // Claims with confidence, gated on the evidence that actually exists:
    // a delivered claim is high-confidence only when the loop's verdict
    // passed AND the unified report — when one was supplied — did not
    // record a non-pass overall (dry-run/plan-only deliveries never verify;
    // refused/aborted claims carry the loop's typed stop reason; unknown ≠
    // success).
    if (d.claims.empty())
    {
        Json::Value claim(Json::objectValue);
        claim["claim"] = "session_outcome";
        claim["value"] = d.outcome;
        const std::string verdict = d.verifier["verdict"].asString();
        if (d.outcome == "delivered")
        {
            const bool loopPassed = verdict == "PASS" || verdict == "PASS_WITH_WARNINGS";
            if (loopPassed && (!unifiedPresent || unifiedPassed))
                claim["confidence"] = 0.9;
            else if (unifiedPresent)
            {
                claim["confidence"] = 0.0; // verification present but not passed
                claim["evidence_missing"].append("passing_verifier_verdict");
                claim["unified_overall"] = d.verifier["unified"]["overall"];
            }
            else
            {
                claim["confidence"] = 0.0; // delivered without verification: indeterminate
                claim["evidence_missing"].append("verifier_verdict");
            }
        }
        else if (d.outcome == "refused" || d.outcome == "aborted")
            claim["confidence"] = 0.85;
        else
            claim["confidence"] = 0.0; // indeterminate
        claim["evidence_ref"] = "journal:" + d.sessionId;
        if (!d.stopReason.empty())
            claim["stop_reason"] = d.stopReason;
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
        p["portable_ref"] =
            basenameRef(o.get("portable_ref", o.get("path", "")).asString());
        outs.append(p);
    }
    doc["outputs"] = outs;
    doc["benchmark_refs"] = delivery.benchmarkRefs;
    if (!delivery.capsule.isNull())
        doc["capsule"] = delivery.capsule;
    return doc;
}

} // namespace sicnu::agent_ops
