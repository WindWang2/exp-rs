// src/agent_ops/diagnostic_bridge.h
#pragma once

//
// Feature C: Production Diagnoser bridge.
// Merges verifier / preflight / runtime / debugger / provenance / diagnose_run
// inputs into a structured OpDiagnostic. LLM/prose summary is never the sole
// root cause — a typed code is required.
//

#include "agent_ops/ops_types.h"
#include "agent_loop/session_seams.h"

#include <optional>
#include <string>

namespace sicnu::agent_ops {

struct DiagnosticInputs {
    std::optional<sicnu::agent_loop::VerificationReport> verification;
    std::optional<sicnu::agent_loop::PreflightReport> preflight;
    std::optional<sicnu::agent_loop::ExecutionOutcome> runtime;
    std::optional<sicnu::agent_loop::Diagnosis> diagnoseRun;
    Json::Value debugger{Json::Value()};   ///< exp.diag.v1-shaped optional
    Json::Value provenance{Json::Value()}; ///< optional sidecar facts
    bool missingOutput = false;
    bool indeterminateVerifier = false;
    bool debuggerIncompleteEvidence = false;
};

class DiagnosticBridge {
  public:
    /// Fail-closed: returns nullopt when no typed root cause can be derived.
    std::optional<OpDiagnostic> diagnose(const DiagnosticInputs &inputs,
                                         std::string *error = nullptr) const;

    /// Convenience: wrap an agent_loop Diagnosis (+ optional extras).
    std::optional<OpDiagnostic> fromLoopDiagnosis(
        const sicnu::agent_loop::Diagnosis &diagnosis,
        const DiagnosticInputs &extras = {},
        std::string *error = nullptr) const;
};

/// IDiagnoser decorator: enriches Fake/production diagnoser output through
/// DiagnosticBridge while preserving the seam contract for AgentLoop.
class BridgedDiagnoser final : public sicnu::agent_loop::IDiagnoser {
  public:
    explicit BridgedDiagnoser(sicnu::agent_loop::IDiagnoser *inner) : mInner(inner) {}

    sicnu::agent_loop::Diagnosis diagnose(
        const sicnu::agent_loop::PlanDraft &plan,
        const sicnu::agent_loop::ExecutionOutcome &outcome,
        const sicnu::agent_loop::VerificationReport &verification) override;

    const std::optional<OpDiagnostic> &lastOpsDiagnostic() const { return mLast; }

    /// Drop the cached diagnostic. Drivers MUST call this between runs: the
    /// cache is evidence about ONE diagnose invocation, and a stale entry
    /// from a previous session would otherwise be attributed to a later,
    /// unrelated failure.
    void resetLastDiagnostic() { mLast.reset(); }

  private:
    sicnu::agent_loop::IDiagnoser *mInner = nullptr;
    DiagnosticBridge mBridge;
    std::optional<OpDiagnostic> mLast;
};

} // namespace sicnu::agent_ops
