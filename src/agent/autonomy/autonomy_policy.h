// src/agent/autonomy/autonomy_policy.h
#pragma once

//
// RS14-12: the versioned autonomy policy schema (sicnu.autonomy-policy/1).
//
// A policy says how much assistance a session may receive and which
// capabilities are individually overridden. Policies come from four sources
// with a fixed precedence (course < labspec < teacher < session); resolution
// lives in resolveEffectivePolicy so there is exactly one merge rule.
//
// Parsing is strict and total: every malformed document yields a typed error
// string and ok=false — a policy is never partially applied, and an
// unsupported schema version is refused rather than best-effort read.
//
// Wire shape:
// { "schema": "sicnu.autonomy-policy/1",
//   "level": "L3",                       // optional; undeclared = inherit
//   "mode": "exam"|"practice"|"instructor"|"agent",  // optional
//   "max_level": "L4",                   // optional ceiling; tightest wins
//   "capability_overrides": {            // optional
//     "<capability>": { "decision": "allow"|"deny",
//                       "reason_code": "<typed code>" } },
//   "source": "course"|"labspec"|"teacher"|"session" } // optional

#include <json/json.h>
#include <string>
#include <utility>
#include <vector>

#include "agent/autonomy/autonomy_capability.h"
#include "agent/autonomy/autonomy_level.h"

namespace sicnu::agent::autonomy {

inline constexpr const char *kAutonomyPolicySchema = "sicnu.autonomy-policy/1";

/// Session modes. The mode bounds the ladder: an exam never unlocks
/// next-step-or-beyond by default; agent mode is the explicit L5 opt-in for
/// the research domain (scientific verification stays mandatory there).
namespace autonomy_modes
{
inline constexpr const char *kExam = "exam";
inline constexpr const char *kPractice = "practice";
inline constexpr const char *kInstructor = "instructor";
inline constexpr const char *kAgent = "agent";
} // namespace autonomy_modes

bool isKnownAutonomyMode( const std::string &mode );

/// Policy sources, in ascending precedence order.
namespace policy_sources
{
inline constexpr const char *kCourse = "course";
inline constexpr const char *kLabspec = "labspec";
inline constexpr const char *kTeacher = "teacher";
inline constexpr const char *kSession = "session";
} // namespace policy_sources

bool isKnownPolicySource( const std::string &source );

/// Per-capability override. Downgrades are derived by the engine from the
/// effective level, never authored per capability — one merge rule.
/// `rank` is merge bookkeeping (the precedence rank of the source that last
/// set this override); it is not part of the wire schema.
struct AutonomyCapabilityOverride
{
    std::string decision;   ///< "allow" | "deny"
    std::string reasonCode; ///< typed code reported when the override decides
    int rank = 0;           ///< precedence rank of the deciding source
};

struct AutonomyPolicy
{
    std::string schema = kAutonomyPolicySchema;
    bool hasLevel = false;
    AutonomyLevel level = AutonomyLevel::L0;
    std::string mode; ///< empty = undeclared (inherit)
    bool hasMaxLevel = false;
    AutonomyLevel maxLevel = AutonomyLevel::L5;
    /// Ordered by capability name for deterministic serialization.
    std::vector<std::pair<std::string, AutonomyCapabilityOverride>> overrides;
    std::string source; ///< empty = undeclared

    /// Deterministic wire document. Undeclared fields are omitted.
    Json::Value toJson() const;
};

struct AutonomyPolicyParseResult
{
    bool ok = false;
    AutonomyPolicy policy;
    std::vector<std::string> errors; ///< typed, stable strings
};

/// Strict parse of a policy document. Never throws, never partially applies.
AutonomyPolicyParseResult parseAutonomyPolicy( const Json::Value &doc );

/// Same, from JSON text (parse errors become typed entries).
AutonomyPolicyParseResult parseAutonomyPolicyJson( const std::string &text );

/// Re-validates an in-memory policy (used after programmatic construction).
bool validateAutonomyPolicy( const AutonomyPolicy &policy, std::vector<std::string> &errors );

/// One precedence layer: a policy plus the source it came from.
struct AutonomyPolicyLayer
{
    std::string source; ///< policy_sources::*
    AutonomyPolicy policy;
};

/// Resolves layers into one effective policy: highest-precedence declaring
/// source wins for level/mode/overrides; the tightest max_level wins; unknown
/// sources are ignored (they cannot grant anything).
AutonomyPolicy resolveEffectivePolicy( const std::vector<AutonomyPolicyLayer> &layers );

/// Mode ceiling: the highest level a mode can unlock (agent mode has none —
/// it is the explicit opt-in). Unknown mode ⇒ L0 (fail-closed).
AutonomyLevel modeCeiling( const std::string &mode );

} // namespace sicnu::agent::autonomy
