// src/planner/plan_teaching.h
#pragma once

//
// RS14-09 Scientific Task Planner — teaching projections (slice D, plan.md D7).
//
// Pure functions over a ScientificPlan. The hidden-answer view masks the
// parameters of student-decision steps ONLY in teaching mode with guided or
// minimal autonomy (masking_applied is honest — false means no masking).
// The explanation view carries per-step rationale, transition whys and
// thinking questions. ADR 0155-aligned: the plan never does the experiment
// for the student, and a masked view never leaks the answer it masks.
//

#include "planner/planning_context.h"
#include "planner/scientific_plan.h"

#include <json/json.h>

namespace sicnu::planner {

struct TeachingViews
{
    Json::Value hiddenAnswer; ///< masked plan document (envelope preserved)
    Json::Value explanation;  ///< plan document + rationale + thinking questions
};

/// Both views of @p plan under @p mode. Deterministic pure function.
TeachingViews teachingViews( const ScientificPlan &plan, const ModePolicy &mode );

} // namespace sicnu::planner
