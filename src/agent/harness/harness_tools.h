// src/agent/harness/harness_tools.h
#pragma once

//
// Harness 4.0 agent-facing harness:* SpatialTools.
//
// These tools expose the harness's own contracts to Pi: the tool manifest
// catalog (Phase 1/2) and the stable error taxonomy (Phase 12). They are
// read-only, bounded, and deterministic like every SpatialTool (ADR 0122).
//

#include "spatial_tools/spatial_tool.h"

namespace sicnu::agent::harness {

/// Registers harness:tool_manifest and harness:error_codes. Idempotent.
void registerHarnessTools();

} // namespace sicnu::agent::harness
