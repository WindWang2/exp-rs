// src/verify/verify_levels.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — the two verification levels.
//
// Level 1 (node): a node-scope spec judges ONE plan node's postcondition
// against the execution facts of that node.
//
// Level 2 (task): a task-scope spec judges the WHOLE task outcome, folding
// the already-produced node reports into the rollup. The no-swap rule: any
// node-level Indeterminate keeps the task at Indeterminate — a task whose
// every own check passed has NOT succeeded while one node's verdict is
// merely unknown.
//
// Distinct from (and never replacing) the harness Verdict vocabulary and the
// lab grading verdicts; projections.h owns the shape mapping.
//

#include <string>
#include <vector>

#include "verify_context.h"
#include "verify_types.h"

namespace sicnu::verify
{

/// Wire-stable marker for the whole-task outcome document.
inline constexpr const char *kTaskOutcomeSchema = "sicnu.verification.task_outcome/1";

/// Level 1: evaluate a NODE-scope spec against one node's execution facts.
/// A spec whose scope is not "node" is refused with the synthetic
/// "spec.valid" Fail — a task-scope contract must not silently judge a node.
VerificationReport verifyPlanNodePostcondition( const VerificationSpec &spec,
                                                const VerificationContext &context );

/// One node report folded into the task outcome.
struct NodeOutcome
{
    std::string specId;
    VerificationStatus overall = VerificationStatus::Indeterminate;
    std::string reportDigest;
};

/// Level 2 result: the task spec's own checks plus the folded node reports,
/// sealed under the same digest discipline as VerificationReport (canonical
/// JSON body, no wall clock, empty digest = refusal sentinel).
struct TaskOutcome
{
    std::string taskSpecId;
    std::string taskSpecDigest;
    std::vector<NodeOutcome> nodes;                  ///< caller order preserved
    std::vector<VerificationCheckResult> taskChecks; ///< task spec declaration order
    VerificationStatus overall = VerificationStatus::Indeterminate;

    Json::Value toCanonicalJson() const;
    std::string digest() const;
};

/// Level 2: evaluate a TASK-scope spec and fold @p nodeReports into the
/// outcome. A spec whose scope is not "task" is refused with a synthetic
/// "spec.valid" Fail outcome. The overall status is the fail-closed lattice
/// of every node overall and every task check — node Indeterminate is never
/// swamped by a passing task body.
TaskOutcome verifyWholeTask( const VerificationSpec &taskSpec, const VerificationContext &context,
                             const std::vector<VerificationReport> &nodeReports );

} // namespace sicnu::verify
