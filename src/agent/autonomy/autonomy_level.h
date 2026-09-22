// src/agent/autonomy/autonomy_level.h
#pragma once

//
// RS14-12 teaching autonomy ladder: the L0..L5 level value object.
//
// The ladder is the single scale every autonomy decision is expressed on:
//   L0 no_assistance             — the assistant stays silent
//   L1 concept_hint              — explain the concept behind the step
//   L2 error_localization        — say what is wrong and how to verify it
//   L3 next_step_recommendation  — say what to do next (still the student acts)
//   L4 plan_generation           — draft the plan (the student runs it)
//   L5 autonomous_execution      — run the work (scientific verification stays)
//
// The strings are the wire contract; never rename. Parsing is strict — an
// unknown spelling is a typed failure, never a silent downgrade.

#include <string>

namespace sicnu::agent::autonomy {

enum class AutonomyLevel
{
    L0 = 0,
    L1 = 1,
    L2 = 2,
    L3 = 3,
    L4 = 4,
    L5 = 5,
};

/// Canonical wire spelling ("L0".."L5").
std::string autonomyLevelToString( AutonomyLevel level );

/// Strict parse: only the exact canonical spellings are accepted.
bool autonomyLevelFromString( const std::string &text, AutonomyLevel &out );

/// True for the exact canonical spellings.
bool isKnownAutonomyLevelString( const std::string &text );

/// 0..5 for the six levels.
int autonomyLevelOrdinal( AutonomyLevel level );

/// Ordinal → level; out-of-range ordinals clamp to the ladder bounds (the
/// caller validates input ranges; clamping keeps arithmetic on the enum
/// total and branch-free at decision sites).
AutonomyLevel autonomyLevelFromOrdinal( int ordinal );

} // namespace sicnu::agent::autonomy
