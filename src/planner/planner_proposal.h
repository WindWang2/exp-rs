// src/planner/planner_proposal.h
#pragma once

//
// RS14-09 Scientific Task Planner — proposal validation seam (slice E).
//
// Any external (e.g. LLM) proposal plan is UNTRUSTED INPUT gated by one
// deterministic validator. A rejected proposal never enters the candidate
// set and never falls back to anything: the outcome carries a closed,
// sorted `planner:proposal_*` rejection code plus human-readable reasons.
// An accepted proposal is re-minted: identity (plan id + fingerprint) is
// recomputed from content, so the wire id can never smuggle identity.
//

#include "planner/planner_core.h"
#include "planner/scientific_goal.h"
#include "planner/scientific_plan.h"

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::planner {

struct ProposalRejection
{
    std::string code; ///< kProposalRejectionCodes member; "" when accepted
    std::vector<std::string> reasons;
};

struct ProposalOutcome
{
    bool accepted = false;
    ScientificPlan plan;          ///< valid when accepted (identity re-minted)
    ProposalRejection rejection;  ///< valid when !accepted
};

/// Validates one proposal document against the SAME rules the baseline
/// planner obeys. Deterministic: same inputs ⇒ same outcome.
ProposalOutcome validateProposal( const Json::Value &proposalDoc, const ScientificGoal &goal,
                                  const PlanningContext &context,
                                  const PlannerProviders &providers );

} // namespace sicnu::planner
