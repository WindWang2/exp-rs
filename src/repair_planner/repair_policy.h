// src/repair_planner/repair_policy.h
#pragma once

//
// RS14-03 completion slice D: the auto-execution policy.
//
// A candidate may be auto-executable ONLY when EVERY conjunct holds:
//
//   1. its action key is on the preparation whitelist — the same closed
//      four-key table the harness preflight emits as the ONLY class the
//      agent may auto-apply (scientific_preflight.cpp kPreparationActions);
//   2. its risk class is shape_preserving (radiometric and science_changing
//      repairs are NEVER auto — the workflow_repair risk-class contract);
//   3. its facts are sufficient (factsSufficient);
//   4. autonomy allows execution (the science-context bundle's
//      allowAutonomousExec — L5+);
//   5. the requester is not a student (the lab teaching constraint: the
//      planner is a tutor, artifacts and executable wiring are withheld —
//      harness_actions normalizeLabRole discipline, "" degrades to student).
//
// Science-changing candidates stay needs_confirmation even with a recorded
// human approval: the planner plans, a caller executes, and the approval is
// carried as auditable context — never folded into an auto flag.
//
// PLANNING-ONLY: the decision vocabulary describes what a CALLER may do; this
// module executes nothing.

#include <string>

#include "repair_schema.h"

namespace sicnu::repair {

namespace policy_decision {
inline constexpr const char *kAutoExecutable = "auto_executable";
inline constexpr const char *kNeedsConfirmation = "needs_confirmation";
inline constexpr const char *kTeachingOnly = "teaching_review_only";
} // namespace policy_decision

/// Closed decision reason codes (machine-readable; one per decision).
namespace policy_reason {
inline constexpr const char *kAutoPolicySatisfied = "auto_policy_satisfied";
inline constexpr const char *kStudentRole = "student_role";
inline constexpr const char *kRiskClassNotShapePreserving = "risk_class_not_shape_preserving";
inline constexpr const char *kDecisionNotExecutable = "decision_not_executable";
inline constexpr const char *kActionKeyNotPreparation = "action_key_not_preparation";
inline constexpr const char *kFactsInsufficient = "facts_insufficient";
inline constexpr const char *kAutonomyNotAllowed = "autonomy_not_allowed";
inline constexpr const char *kRefusalNotExecutable = "refusal_not_executable";
} // namespace policy_reason

struct RepairPolicyContext
{
    /// Which surface the plan is being resolved for. "lab" activates the
    /// teaching gate (mirror of the harness TeachingContext intentDomain
    /// discipline); anything else — including "" — leaves it inert, exactly
    /// as research flows resolve in the harness. EMBEDDER OBLIGATION: this
    /// must be the RESOLVED intentDomain (as lab_copilot derives it), never
    /// a client-claimed value — an embedder that fails to thread the
    /// resolved domain silently loses the student gate there.
    std::string domain;
    /// Session role. ON THE TEACHING SURFACE ("lab") only "teacher" and
    /// "admin" escape the student gate — byte-identical to the harness
    /// normalizeLabRole table; every other word, including "" and words the
    /// harness does not know, is a student there. Outside the lab the gate
    /// is inert; autonomy remains the governing conjunct.
    std::string role;
    /// From the science-context autonomy constraints (L5+).
    bool allowAutonomousExec = false;
    /// A recorded human approval for a science-changing repair. Auditable
    /// context only — it never turns a science-changing candidate auto.
    bool scienceChangeApproved = false;
};

struct RepairPolicyDecision
{
    std::string decision;  ///< policy_decision::*
    std::string reasonCode; ///< policy_reason::*
};

bool isKnownPolicyDecision( const std::string &decision );

/// True when `actionKey` is on the closed preparation whitelist
/// {reproject_to_reference, align_to_reference, normalize_radiometry,
/// calibrate_consistently} — byte-identical to the harness
/// kPreparationActions table (drift-pinned by tests).
bool isPreparationActionKey( const std::string &actionKey );

/// Evaluates the auto-execution policy for one candidate under one context.
/// Deterministic and pure.
RepairPolicyDecision evaluateRepairPolicy( const RepairAction &action,
                                           const RepairPolicyContext &context );

} // namespace sicnu::repair
