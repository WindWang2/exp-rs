// src/repair_planner/repair_view.h
#pragma once

//
// RS14-03 completion slice F: audience views over a finished plan document.
//
// The teaching constraint is structural, not a prompt: the student-facing
// view of a repair plan carries the why, the what-if, the risk and the cost —
// and NOTHING executable. The parameters, the operator wiring and the harness
// action key are recursively stripped, so no code path can leak a runnable
// recipe through the teaching surface. What remains answers "why is this
// wrong and what kind of repair would change it", never "run this".
//
// The agent/research view carries the full auditable contract plus an
// explicit planning_only marker: everything needed to DECIDE, nothing that
// executes.
//
// Both views are fail-closed envelope readers: a document that is not a
// repair_plan/1.0 envelope is a typed invalid_document, never a best-effort
// projection.

#include <json/json.h>
#include <string>

#include "repair_schema.h"

namespace sicnu::repair {

/// Student view: executable values recursively withheld. Deterministic.
bool teachingRepairView( const Json::Value &planDoc, Json::Value &view, RepairError &error );

/// Agent/research view: full contract + planning_only marker. Deterministic.
bool agentRepairView( const Json::Value &planDoc, Json::Value &view, RepairError &error );

} // namespace sicnu::repair
