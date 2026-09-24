// src/agent_ops/diagnostic_bridge.cpp
#include "agent_ops/diagnostic_bridge.h"

namespace sicnu::agent_ops {
namespace {

bool hasTypedCode(const std::string &code)
{
    return !code.empty();
}

} // namespace

std::optional<OpDiagnostic> DiagnosticBridge::diagnose(const DiagnosticInputs &inputs,
                                                       std::string *error) const
{
    OpDiagnostic d;
    d.sources = Json::objectValue;

    // Priority: runtime failure > verifier FAIL > preflight blocked > diagnose_run
    // > debugger. Prose alone never wins.
    if (inputs.runtime && inputs.runtime->finished && !inputs.runtime->succeeded)
    {
        d.rootCauseCode = inputs.runtime->errorCode.empty() ? "RUNTIME_FAILED"
                                                            : inputs.runtime->errorCode;
        d.code = "ops.runtime." + d.rootCauseCode;
        d.retryable = (d.rootCauseCode == "CANCELLED" || d.rootCauseCode == "TIMEOUT" ||
                       d.rootCauseCode == "TRANSIENT");
        d.repairable = !d.retryable;
        d.sources["runtime"] = Json::objectValue;
        d.sources["runtime"]["error_code"] = d.rootCauseCode;
        d.sources["runtime"]["state"] = inputs.runtime->state;
        d.confidence = 0.85;
    }

    if (inputs.missingOutput)
    {
        d.rootCauseCode = "MISSING_OUTPUT";
        d.code = "ops.verify.MISSING_OUTPUT";
        d.retryable = false;
        d.repairable = true;
        d.confidence = 0.9;
        d.sources["verifier"]["missing_output"] = true;
    }
    else if (inputs.indeterminateVerifier)
    {
        d.rootCauseCode = "VERIFIER_INDETERMINATE";
        d.code = "ops.verify.INDETERMINATE";
        d.retryable = false;
        d.repairable = false;
        d.confidence = 0.4;
        d.sources["verifier"]["indeterminate"] = true;
        // Unknown ≠ success — advisory ask/abort, not deliver.
        d.advisoryNext = recovery_action::kAsk;
    }
    else if (inputs.verification)
    {
        d.sources["verification"] = inputs.verification->toJson();
        const std::string &verdict = inputs.verification->verdict();
        if (verdict == "FAIL" && !hasTypedCode(d.rootCauseCode))
        {
            d.rootCauseCode = "VERIFICATION_FAILED";
            d.code = "ops.verify.FAIL";
            d.repairable = true;
            d.retryable = false;
            d.confidence = 0.8;
        }
        else if (verdict == "PASS_WITH_WARNINGS" && !hasTypedCode(d.rootCauseCode))
        {
            d.rootCauseCode = "VERIFICATION_WARNINGS";
            d.code = "ops.verify.PASS_WITH_WARNINGS";
            d.repairable = false;
            d.retryable = false;
            d.confidence = 0.7;
            d.advisoryNext = recovery_action::kAsk;
        }
    }

    if (inputs.preflight)
    {
        d.sources["preflight"] = inputs.preflight->toJson();
        if (inputs.preflight->verdict == "blocked" && !hasTypedCode(d.rootCauseCode))
        {
            d.rootCauseCode = "PREFLIGHT_BLOCKED";
            d.code = "ops.preflight.BLOCKED";
            d.repairable = false;
            d.retryable = false;
            d.confidence = 0.95;
            d.advisoryNext = recovery_action::kAbort;
        }
        else if (inputs.preflight->verdict == "fixable" && !hasTypedCode(d.rootCauseCode))
        {
            d.rootCauseCode = "PREFLIGHT_FIXABLE";
            d.code = "ops.preflight.FIXABLE";
            d.repairable = true;
            d.retryable = false;
            d.confidence = 0.9;
            d.advisoryNext = recovery_action::kRepair;
            for (const auto &p : inputs.preflight->proposals)
            {
                d.proposals.push_back(p.ruleId);
                Json::Value detail(Json::objectValue);
                detail["rule_id"] = p.ruleId;
                detail["risk_class"] = p.riskClass;
                detail["operator_id"] = p.operatorId;
                d.proposalDetails.append(detail);
            }
        }
    }

    if (inputs.diagnoseRun)
    {
        d.sources["diagnose_run"] = inputs.diagnoseRun->toJson();
        if (!hasTypedCode(d.rootCauseCode) && hasTypedCode(inputs.diagnoseRun->rootCauseCode))
        {
            d.rootCauseCode = inputs.diagnoseRun->rootCauseCode;
            d.code = "ops.diagnose." + d.rootCauseCode;
            d.confidence = 0.75;
            d.repairable = !inputs.diagnoseRun->proposals.empty();
            d.summary = inputs.diagnoseRun->summary; // advisory only
        }
        for (const auto &p : inputs.diagnoseRun->proposals)
        {
            d.proposals.push_back(p.ruleId);
            Json::Value detail(Json::objectValue);
            detail["rule_id"] = p.ruleId;
            detail["risk_class"] = p.riskClass;
            detail["operator_id"] = p.operatorId;
            d.proposalDetails.append(detail);
        }
        if (inputs.diagnoseRun->evidence.isObject())
            d.evidence["diagnose_run"] = inputs.diagnoseRun->evidence;
    }

    if (!inputs.debugger.isNull() && inputs.debugger.isObject())
    {
        d.sources["debugger"] = inputs.debugger;
        const std::string dbgCode = inputs.debugger.get("code", "").asString();
        if (inputs.debuggerIncompleteEvidence ||
            dbgCode.find("insufficient_evidence") != std::string::npos)
        {
            if (!hasTypedCode(d.rootCauseCode))
            {
                d.rootCauseCode = "DEBUGGER_INCOMPLETE_EVIDENCE";
                d.code = "ops.debugger.INCOMPLETE_EVIDENCE";
                d.confidence = 0.35;
                d.repairable = false;
                d.retryable = false;
                d.advisoryNext = recovery_action::kAsk;
            }
            d.sources["debugger"]["incomplete_evidence"] = true;
        }
        else if (!hasTypedCode(d.rootCauseCode) && hasTypedCode(dbgCode))
        {
            d.rootCauseCode = dbgCode;
            d.code = "ops.debugger." + dbgCode;
            d.confidence = inputs.debugger.get("details", Json::objectValue)
                               .get("confidence", 0.5)
                               .asDouble();
        }
    }

    if (!inputs.provenance.isNull() && inputs.provenance.isObject())
        d.sources["provenance"] = inputs.provenance;

    if (!hasTypedCode(d.rootCauseCode))
    {
        if (error)
            *error = "no typed root cause from structured sources";
        return std::nullopt;
    }
    if (d.code.empty())
        d.code = "ops." + d.rootCauseCode;
    if (d.advisoryNext.empty())
    {
        if (d.retryable)
            d.advisoryNext = recovery_action::kRetry;
        else if (d.repairable)
            d.advisoryNext = recovery_action::kRepair;
        else
            d.advisoryNext = recovery_action::kReplan;
    }
    if (d.summary.empty())
        d.summary = "structured root cause " + d.rootCauseCode;
    return d;
}

std::optional<OpDiagnostic> DiagnosticBridge::fromLoopDiagnosis(
    const sicnu::agent_loop::Diagnosis &diagnosis, const DiagnosticInputs &extras,
    std::string *error) const
{
    DiagnosticInputs merged = extras;
    merged.diagnoseRun = diagnosis;
    return diagnose(merged, error);
}

sicnu::agent_loop::Diagnosis BridgedDiagnoser::diagnose(
    const sicnu::agent_loop::PlanDraft &plan,
    const sicnu::agent_loop::ExecutionOutcome &outcome,
    const sicnu::agent_loop::VerificationReport &verification)
{
    sicnu::agent_loop::Diagnosis inner;
    if (mInner)
        inner = mInner->diagnose(plan, outcome, verification);

    DiagnosticInputs inputs;
    inputs.verification = verification;
    inputs.runtime = outcome;
    inputs.diagnoseRun = inner;
    inputs.missingOutput = outcome.succeeded && outcome.artifacts.empty();
    mLast = mBridge.diagnose(inputs);

    if (mLast)
    {
        // Enrich loop Diagnosis with structured code; keep proposals.
        if (inner.rootCauseCode.empty())
            inner.rootCauseCode = mLast->rootCauseCode;
        if (inner.summary.empty())
            inner.summary = mLast->summary;
        inner.evidence["ops_diagnostic"] = mLast->toJson();
    }
    return inner;
}

} // namespace sicnu::agent_ops
