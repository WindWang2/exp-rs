// src/repair_planner/repair_policy.cpp
#include "repair_policy.h"

#include <set>

namespace sicnu::repair {

namespace {

/// The closed preparation whitelist. Byte-identical to the harness preflight
/// kPreparationActions table (scientific_preflight.cpp) — the ONLY issue
/// class the harness ever marks agent-auto-applicable. The leaf planner
/// cannot include the harness; the exact-string pin lives in
/// tests/test_repair_planner_completion.cpp.
const std::set<std::string> &preparationActions()
{
    static const std::set<std::string> kSet = {
        "reproject_to_reference", "align_to_reference", "normalize_radiometry",
        "calibrate_consistently",
    };
    return kSet;
}

/// The lab teaching constraint, mirrored byte-for-byte from the harness
/// single truth (normalizeLabRole): ONLY "teacher" and "admin" escape the
/// student default on the teaching surface. The planner invents no privilege
/// roles: "instructor", "researcher", "agent" and "" are all students there,
/// so a session the rest of the lab system treats as a student cannot obtain
/// an auto-executable surface by declaring a different word.
bool isStudentRole( const std::string &role )
{
    return role != "teacher" && role != "admin";
}

} // namespace

bool isKnownPolicyDecision( const std::string &decision )
{
    return decision == policy_decision::kAutoExecutable ||
           decision == policy_decision::kNeedsConfirmation ||
           decision == policy_decision::kTeachingOnly;
}

bool isPreparationActionKey( const std::string &actionKey )
{
    return preparationActions().count( actionKey ) > 0;
}

RepairPolicyDecision evaluateRepairPolicy( const RepairAction &action,
                                           const RepairPolicyContext &context )
{
    // A documented refusal is never executable — the schema's own invariant,
    // enforced here as the last line of defense (a refusal may still carry
    // sufficient facts; the refusal cause, not the facts, decides).
    if ( !action.refusalCause.empty() )
        return { policy_decision::kNeedsConfirmation,
                 policy_reason::kRefusalNotExecutable };

    // Teaching gate, active on the lab surface only: a student never sees an
    // auto-executable surface there, regardless of every other conjunct.
    if ( context.domain == "lab" && isStudentRole( context.role ) )
        return { policy_decision::kTeachingOnly, policy_reason::kStudentRole };

    // Risk class is the hard ceiling: radiometric and science-changing
    // repairs need a human/Pi confirmation even with approval recorded.
    if ( action.riskClass != repair_risk::kShapePreserving )
        return { policy_decision::kNeedsConfirmation,
                 policy_reason::kRiskClassNotShapePreserving };

    // A decision surfaces a choice; there is nothing to auto-execute.
    if ( action.kind == action_kind::kDecision )
        return { policy_decision::kNeedsConfirmation,
                 policy_reason::kDecisionNotExecutable };

    // Only the preparation whitelist may ever be auto-applied.
    if ( !isPreparationActionKey( action.actionKey ) )
        return { policy_decision::kNeedsConfirmation,
                 policy_reason::kActionKeyNotPreparation };

    // The facts that would back the repair must be sufficient.
    if ( !action.factsSufficient )
        return { policy_decision::kNeedsConfirmation, policy_reason::kFactsInsufficient };

    // Autonomy must allow execution.
    if ( !context.allowAutonomousExec )
        return { policy_decision::kNeedsConfirmation, policy_reason::kAutonomyNotAllowed };

    return { policy_decision::kAutoExecutable, policy_reason::kAutoPolicySatisfied };
}

} // namespace sicnu::repair
