// src/agent/tools/agent_session_adapter.h
#pragma once

//
// Production seam adapter for the evidence-first agent loop.
//
// This file is the seam bundle location session_seams.h has always named.
// The bundle delegates to the existing engines — science_context broker +
// grounding probes (data), the scientific planner chain
// planScientificWork → projectPlanToIr → compileWorkflow (planner),
// preflightIntent rule packs (preflight), WorkflowRunCoordinator (executor),
// harness::verifyArtifact (verifier) and the workflow run's own error
// records (diagnoser). No second registry, no second engine, no fake
// doubles: a seam whose authority cannot be loaded is left null and the
// OpsDriver seam gate refuses the run (SEAMS_UNAVAILABLE) before it starts.
//
// Qt-free in the header; the implementation may use Qt internally.
//

#include "agent_loop/session_seams.h"

#include <map>
#include <memory>
#include <string>

namespace sicnu::agent {

/// Loop verifier over the harness verification engine. Expectations are
/// derived from the plan's output documents (kind; optional provenance
/// requirement); artifacts NOT declared by the plan are still verified with
/// structural defaults — an undeclared artifact can never silently pass.
class HarnessVerifier final : public sicnu::agent_loop::IVerifier {
  public:
    sicnu::agent_loop::VerificationReport
    verify( const sicnu::agent_loop::PlanDraft &plan,
            const sicnu::agent_loop::ExecutionOutcome &outcome ) override;
};

/// Loop data state over the ONE grounding chain: every ref resolves through
/// the harness entity resolver and is probed through the same
/// spatial:understand seam the compiler grounds with (cached, scoped facts
/// with per-key fact_status). Unresolvable refs stay typed errors — never
/// invented facts.
class HarnessDataStateProvider final : public sicnu::agent_loop::IDataStateProvider {
  public:
    sicnu::agent_loop::DataStateSnapshot
    snapshot( const std::string &goal, const Json::Value &refs ) override;
};

/// Loop planner over the scientific planner chain: science_context bundle →
/// projectPlannerInputs → planScientificWork (live capability mirror) →
/// projectPlanToIr → compileWorkflow (analysis/repair/lowering) → AgentPlan
/// v2. The draft's steps/outputs are the LOWERED executable steps verbatim;
/// fingerprint/identity come from the harness plan fingerprint (attempt
/// sensitive / attempt independent respectively).
class HarnessPlanner final : public sicnu::agent_loop::IPlanner {
  public:
    /// Fails closed (false + typed @p error) when the live capability
    /// authority (capability mirror) cannot be loaded — planning must never
    /// run over a degraded authority that looks live.
    static bool create( std::unique_ptr< HarnessPlanner > &out, std::string &error );

    sicnu::agent_loop::PlanDraft plan( const sicnu::agent_loop::PlanRequest &request ) override;

    ~HarnessPlanner() override;

  private:
    HarnessPlanner() = default;
    struct Impl;
    std::unique_ptr< Impl > mImpl;
};

/// Loop preflight over the SAME preflightIntent rule packs
/// harness:execute_plan gates on (WP04: what is preflighted is what
/// executes — the draft's steps ARE the lowered executable steps and the
/// snapshot's refs are the resolved inputs).
class HarnessPreflight final : public sicnu::agent_loop::IPreflight {
  public:
    sicnu::agent_loop::PreflightReport
    check( const sicnu::agent_loop::PlanDraft &plan,
           const sicnu::agent_loop::DataStateSnapshot &snapshot ) override;
};

/// Loop executor over the authoritative WorkflowRunCoordinator → TaskCenter
/// stack (the single engine the harness itself submits through). The draft's
/// steps re-compile deterministically (compilePlanToWorkflowJson — the same
/// compiler) into the workflow JSON the engine runs.
class HarnessExecutor final : public sicnu::agent_loop::IExecutor {
  public:
    sicnu::agent_loop::ExecutionStart
    begin( const sicnu::agent_loop::PlanDraft &plan ) override;
    sicnu::agent_loop::ExecutionOutcome
    poll( const sicnu::agent_loop::ExecutionStart &start, long long timeoutMs ) override;
    void cancel( const sicnu::agent_loop::ExecutionStart &start ) override;

  private:
    /// pipelineId per runId for cancel/poll (bounded, single-session use).
    std::map< std::string, long > mPipelineByRun;
};

/// Loop diagnoser over the workflow run's own authoritative records: the
/// engine state, the per-step errors and the normalized typed error code.
/// Repair PROPOSALS stay with the coordinator's recovery bridge (the
/// capability-provider-backed planner) — this seam reports evidence, it
/// does not mint repairs.
class HarnessDiagnoser final : public sicnu::agent_loop::IDiagnoser {
  public:
    sicnu::agent_loop::Diagnosis
    diagnose( const sicnu::agent_loop::PlanDraft &plan,
              const sicnu::agent_loop::ExecutionOutcome &outcome,
              const sicnu::agent_loop::VerificationReport &verification ) override;
};

/// The production bundle hosts inject into OpsDriver. A seam whose
/// authority is unavailable stays null and `error` names it — the driver
/// then refuses SEAMS_UNAVAILABLE instead of running partial science.
struct ProductionSeamBundle
{
    std::unique_ptr< HarnessDataStateProvider > data;
    std::unique_ptr< HarnessPlanner > planner;
    std::unique_ptr< HarnessPreflight > preflight;
    std::unique_ptr< HarnessExecutor > executor;
    std::unique_ptr< HarnessVerifier > verifier;
    std::unique_ptr< HarnessDiagnoser > diagnoser;
    std::string error; ///< typed reason when an authority is unavailable
};

/// Assembles the full production bundle (fail-closed per seam).
ProductionSeamBundle makeProductionSeamBundle();

} // namespace sicnu::agent
