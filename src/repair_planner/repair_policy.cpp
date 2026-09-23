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

/// The planner's closed role vocabulary. "teacher"/"admin"/"instructor" are
/// the lab teaching roles that escape the student gate (harness
/// normalizeLabRole discipline); "researcher" and "agent" are the
/// research/automation audiences this planner serves. EVERYTHING else —
/// including "" — degrades to "student", the safe default: an unrecognized
/// role never sees an auto-executable surface.
bool isStudentRole( const std::string &role )
{
    static const std::set<std::string> kNonStudent = {
        "teacher", "admin", "instructor", "researcher", "agent",
    };
    return kNonStudent.count( role ) == 0;
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
    // Teaching gate first: a student never sees an auto-executable surface,
    // regardless of every other conjunct.
    if ( isStudentRole( context.role ) )
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
