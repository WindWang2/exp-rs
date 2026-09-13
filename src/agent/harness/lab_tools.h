// src/agent/harness/lab_tools.h
#pragma once

//
// D9: the lab copilot's agent-facing SpatialTools.
//
//   harness:lab_ask       — the teaching chat surface (student-safe by
//                           construction: refusals are typed, actions gated)
//   harness:lab_reference — the teacher surface (reference solutions, grade
//                           citation); students receive TEACHING_REFUSAL
//
// Read-only, bounded, deterministic like every SpatialTool (ADR 0122). The
// teaching constraint itself lives in harness_actions — these tools only
// route to it.
//

#include "../spatial_tools/spatial_tool.h"

namespace sicnu::agent::harness {

/// Registers harness:lab_ask and harness:lab_reference. Idempotent.
void registerLabTools();

} // namespace sicnu::agent::harness
