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

namespace sicnu::agent::cartography {

/// Registers the cartography:* tools. Idempotent.
void registerCartographyTools();

/// Spec-level cartography preflight: returns a MapQualityReport envelope
/// (kind "map_quality_report") with code/severity/item_id/repairable/
/// suggested_action issues and a 0-100 quality score. `compiled` may carry
/// the layout:preflight report of the compiled layout to merge.
Json::Value preflightMapSpec( const Json::Value &spec, const Json::Value &compiledReport = Json::Value() );

/// Applies one deterministic repair pass for every repairable issue of the
/// current report. Returns the number of repairs applied.
int repairMapSpec( Json::Value &spec, const Json::Value &report );

} // namespace sicnu::agent::cartography
