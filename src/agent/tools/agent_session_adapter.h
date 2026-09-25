// src/agent/tools/agent_session_adapter.h
#pragma once

//
// Production seam adapter for the evidence-first agent loop.
//
// This is the FIRST real production seam bundle (the location the loop's
// session_seams.h header has always named). It currently wires:
//
//   HarnessVerifier : IVerifier — delegates to the harness automatic output
//   verification (harness::verifyArtifact): existence, openability, CRS,
//   dimensions/band count, finite/NoData fractions, class domain, and
//   provenance sidecar checks, with the closed tri-state verdict contract.
//
// The remaining seams (planner, preflight, executor, diagnoser, data
// provider) stay caller-injected until the production wiring lands; hosts
// that cannot supply them fail closed through the OpsDriver seam gate
// (SEAMS_UNAVAILABLE) instead of running fake science.
//
// Qt-free: compiled into a small static target so both the headless CLI and
// the GUI app can link it.
//

#include "agent_loop/session_seams.h"

#include <string>
#include <vector>

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

} // namespace sicnu::agent
