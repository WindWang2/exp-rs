// src/agent/cartography/cartography_tools.h
#pragma once

//
// Phase I/J/L/M — the cartography:* tool family:
//   cartography:list_components / get_component    (component library)
//   cartography:list_templates / instantiate_template (template library)
//   cartography:compose / preflight / repair       (compile → inspect → fix)
//   cartography:chart_create / chart_get / chart_list / chart_delete (charts)
//
// plus free helpers shared with tests:
//   preflightMapSpec()  — spec-level quality report (MapQualityReport)
//   repairMapSpec()     — one deterministic repair pass
//

#include "../spatial_tools/spatial_tool.h"
#include "quality.h"

namespace sicnu::agent::cartography {

/// Registers the cartography:* tools. Idempotent.
void registerCartographyTools();

// preflightMapSpec / repairMapSpec live in quality.h (Milestone E module
// split); the declarations stay reachable through this header for the
// existing consumers (tests, tools, compileAndAssess).

} // namespace sicnu::agent::cartography
