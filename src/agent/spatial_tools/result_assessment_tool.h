// src/agent/spatial_tools/result_assessment_tool.h
#pragma once

// Phase G — scientific result assessment SpatialTool:
//   spatial:assess_result  post-execution sanity verdict (ResultAssessment)
//
// Registered via registerResultAssessmentTool() from SpatialToolRegistry::registerBuiltinTools().

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/// Registers spatial:assess_result. Idempotent.
void registerResultAssessmentTool();

} // namespace sicnu::agent::spatial_tools
