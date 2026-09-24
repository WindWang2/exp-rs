// src/planner/scientific_goal.h
#pragma once

//
// RS14-09 Scientific Task Planner — ScientificGoal document (slice A).
//
// Versioned, fail-closed, canonical-JSON document ("scientific_goal/1.0"):
// WHAT the user wants to know or produce, with optional temporal scope and
// acceptance criteria. No execution semantics; the goal is planner input.
//

#include <json/json.h>
#include <optional>
#include <string>
#include <vector>

#include "planner/planner_vocab.h"

namespace sicnu::planner {

struct GoalTemporalScope
{
    std::string start;   ///< ISO date/time or "" when unset
    std::string end;     ///< ISO date/time or "" when unset
    int minScenes = 0;   ///< 0 = no minimum declared
    int maxGapDays = 0;  ///< 0 = no gap constraint declared
};

struct GoalAcceptanceCriterion
{
    std::string criterionId;  ///< stable id, ≤ 64 chars
    std::string check;        ///< what to verify, ≤ 512 chars
    std::string target;       ///< target value/threshold, "" when none
};

struct ScientificGoal
{
    std::string goalId;  ///< ≤ 64 chars
    std::string kind;    ///< kGoalKinds member
    std::string subject; ///< what the science is about, ≤ 512 chars
    std::string quantity;///< optional measured quantity, ≤ 512 chars

    std::optional<GoalTemporalScope> temporalScope;
    std::vector<GoalAcceptanceCriterion> acceptanceCriteria;
};

/// Canonical JSON projection: key-sorted compact object, envelope
/// {kind, schema_version, goal_id, kind-of-goal, ...}. Byte-deterministic.
Json::Value scientificGoalToJson( const ScientificGoal &goal );

/// Fail-closed reader: accepts only "scientific_goal"/"1.0"; unknown goal
/// kinds and out-of-bounds text are typed errors, never coercion.
/// @returns false with @p error (closed code prefix + detail) on any problem.
bool scientificGoalFromJson( const Json::Value &doc, ScientificGoal &out, std::string &error );

/// Structural validation independent of JSON (used by the core planner).
std::vector<std::string> validateScientificGoal( const ScientificGoal &goal );

} // namespace sicnu::planner
