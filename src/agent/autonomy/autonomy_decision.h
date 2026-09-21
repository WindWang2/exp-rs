// src/agent/autonomy/autonomy_decision.h
#pragma once

//
// RS14-12: the autonomy decision engine.
//
// One function turns (effective policy, request) into a typed decision:
//   allow      — the capability may be exercised;
//   deny       — it may not; reasonCode says why (machine-readable);
//   downgrade  — an assistive capability above the effective level resolves
//                to the highest capability the level DOES unlock.
//
// Order of rules (structural safety first, then explicit overrides, then the
// level matrix):
//   1. unknown capability                ⇒ deny AUTONOMY_UNKNOWN_CAPABILITY
//   2. lab + student + execution         ⇒ deny AUTONOMY_LAB_STUDENT_EXECUTION
//   3. non-lab + execution + mode≠agent  ⇒ deny AUTONOMY_AGENT_MODE_REQUIRED
//   4. explicit per-capability override  ⇒ allow / deny (overrides the matrix)
//   5. effective level ≥ minimum level   ⇒ allow (execution keeps verification)
//   6. assistive capability above level  ⇒ downgrade
//   7. execution above level             ⇒ deny (never a silent fallback)
//
// Execution is never downgraded: "run it for me" either is permitted or is
// refused with a typed code — the engine never substitutes a weaker action
// and calls it done.

#include <json/json.h>
#include <string>

#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"

namespace sicnu::agent::autonomy {

enum class AutonomyDecisionKind
{
    Allow,
    Deny,
    Downgrade,
};

/// Closed, stable reason codes. Values are the wire strings; never rename.
namespace autonomy_reason_codes
{
inline constexpr const char *kAllowed = "AUTONOMY_ALLOWED";
inline constexpr const char *kUnknownCapability = "AUTONOMY_UNKNOWN_CAPABILITY";
inline constexpr const char *kLevelTooLow = "AUTONOMY_LEVEL_TOO_LOW";
inline constexpr const char *kDowngraded = "AUTONOMY_DOWNGRADED";
inline constexpr const char *kModeCeiling = "AUTONOMY_MODE_CEILING";
inline constexpr const char *kCourseCap = "AUTONOMY_COURSE_CAP";
inline constexpr const char *kOverrideDenied = "AUTONOMY_OVERRIDE_DENIED";
inline constexpr const char *kLabStudentExecution = "AUTONOMY_LAB_STUDENT_EXECUTION";
inline constexpr const char *kAgentModeRequired = "AUTONOMY_AGENT_MODE_REQUIRED";
} // namespace autonomy_reason_codes

bool isKnownAutonomyReasonCode( const std::string &code );

/// What is being decided. `role` is host-injected session state (same rule
/// as the lab role in ADR 0155): anything that is not teacher/admin is a
/// student — the engine never parses authority out of message content.
struct AutonomyRequest
{
    std::string domain;     ///< "lab" for the teaching surface; other domains are research surfaces
    std::string role;       ///< session role
    std::string capability; ///< closed capability taxonomy
    std::string intent;     ///< optional, audit only
    std::string actionKey;  ///< optional, audit only
    std::string toolId;     ///< optional, audit only
    std::string riskClass;  ///< optional, audit only
};

struct AutonomyDecision
{
    AutonomyDecisionKind kind = AutonomyDecisionKind::Deny;
    std::string capability;
    AutonomyLevel requestedLevel = AutonomyLevel::L0; ///< minimum level of the requested capability
    AutonomyLevel effectiveLevel = AutonomyLevel::L0; ///< level after policy + mode + cap
    std::string reasonCode;
    std::string downgradeTo; ///< capability, when kind == Downgrade
    bool verificationRequired = false;

    std::string kindString() const;
    /// Deterministic wire document (no wall-clock; the audit log adds identity).
    Json::Value toJson() const;
};

/// The engine. `policy` must already be resolved (resolveEffectivePolicy);
/// resolution and decision are separate so precedence has exactly one home.
AutonomyDecision decideAutonomy( const AutonomyPolicy &policy, const AutonomyRequest &request );

/// True for "teacher"/"admin" — mirrors harness_actions::normalizeLabRole's
/// fail-closed rule so the two gates cannot disagree about who a teacher is.
bool isInstructorRole( const std::string &role );

} // namespace sicnu::agent::autonomy
