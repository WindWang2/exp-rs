/***************************************************************************
  render_agent.h — the machine surface of a verification report (RS14-14)

  The teaching view answers "can I hand this in"; this one answers "what do I
  call next". It is deliberately flat and closed: a status, the codes, the
  replan classes, the actions, and the nodes that stopped the roll-up. An Agent
  consumer must never have to re-derive any of it from the report, because every
  re-derivation is a fresh chance to read Indeterminate as Pass.

  Two invariants the tests pin:

    1. failureCodes is EXACTLY the set of codes on the report's non-Pass
       results — no invented codes (an Agent would chase a failure that does
       not exist) and no dropped codes (an Agent would retry forever).
    2. `status` comes from the same `derivedStatus` the teaching view uses, so
       the two surfaces can never disagree about what happened.
 ***************************************************************************/
#pragma once

#include "verification/report.h"
#include "verification/status_lattice.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::verification
{

inline constexpr const char *kAgentSchema = "exp.verification.agent.v1";

struct AgentView
{
    std::string schema = kAgentSchema;

    /// "pass" | "fail" | "indeterminate" — the closed lattice spelling.
    std::string status;

    /// Deduplicated, sorted. Exactly the codes on non-Pass results.
    std::vector<std::string> failureCodes;
    /// "none" | "retry" | "replan" | "abort", deduplicated, derived from the
    /// codes by the closed table in failure_codes.h.
    std::vector<std::string> replanClasses;
    /// One action per failure code, in the same order.
    std::vector<std::string> suggestedActions;
    std::vector<std::string> blockingNodes;
    std::string firstBlockingNode;

    Json::Value toJson() const;
};

AgentView renderAgent( const VerificationReport &report );

} // namespace sicnu::verification
