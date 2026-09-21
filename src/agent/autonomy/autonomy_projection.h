// src/agent/autonomy/autonomy_projection.h
#pragma once

//
// RS14-12: the autonomy status projection (sicnu.autonomy-status/1).
//
// The projection answers, without prose: what level is this session on, what
// mode, and for every capability whether it is allowed, limited (downgraded),
// or forbidden — with a typed reason and a Chinese explanation. It is the
// surface the UI renders and the machine-readable surface a future agent
// reads; there is no second place that decides what a session may do.
//
// Pure function of (policy, role, domain): the engine is called once per
// capability (six at most), so the projection is bounded and can never
// disagree with a live decision.

#include <json/json.h>
#include <string>

#include "agent/autonomy/autonomy_decision.h"
#include "agent/autonomy/autonomy_level.h"
#include "agent/autonomy/autonomy_policy.h"

namespace sicnu::agent::autonomy {

inline constexpr const char *kAutonomyStatusSchema = "sicnu.autonomy-status/1";

/// Effective level: declared level (undeclared ⇒ L0) clamped by the mode
/// ceiling and the tightest course cap. Same arithmetic the engine uses.
AutonomyLevel effectiveAutonomyLevel( const AutonomyPolicy &policy );

/// Chinese explanation for a closed reason code; empty for unknown codes.
std::string autonomyReasonZh( const std::string &reasonCode );

/// Machine-readable decision block that rides on every answer/result the
/// gates produce: {decision, capability, effective_level, reason_code,
/// reason_zh, downgrade_to?, verification_required?}. Deterministic.
Json::Value autonomyDecisionDoc( const AutonomyDecision &decision );

/// sicnu.autonomy-status/1 for one session. `domain` defaults to the lab
/// (teaching) surface, where the student-execution rule applies.
Json::Value autonomyStatusProjection( const AutonomyPolicy &policy, const std::string &role,
                                      const std::string &domain = "lab" );

} // namespace sicnu::agent::autonomy
