// src/agent/symbology/symbology_tools.h
#pragma once

//
// Phase K — structured symbology intelligence:
//   symbology:describe               current renderer state (bounded JSON)
//   symbology:apply_categorical      field-driven categorized renderer
//   symbology:apply_graduated        equal-interval graduated renderer
//   symbology:apply_raster_ramp      single-band pseudo-color ramp
//
// Every apply* runs through the WorkspaceCommandStack (Phase N): the previous
// renderer is captured and restorable via workspace:undo.
//

#include "../spatial_tools/spatial_tool.h"

namespace sicnu::agent::symbology {

/// Registers the symbology:* tools. Idempotent.
void registerSymbologyTools();

} // namespace sicnu::agent::symbology
