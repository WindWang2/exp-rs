// src/agent/harness/grounding_tools.h
#pragma once

#include "../spatial_tools/spatial_tool.h"

namespace sicnu::agent::harness {

/// Registers `spatial:understand` (typed dataset grounding document) and
/// `harness:context` (revision-stamped compact workspace context). Idempotent.
void registerGroundingTools();

/// Deterministic single-scene modality inference over a spatial:raster_inspect
/// output: "sar" | "optical" | "dem" | "unknown". Uses product metadata, band
/// roles, wavelengths, and the GDAL driver name — never filenames alone.
std::string inferModality( const Json::Value &rasterInspect );

} // namespace sicnu::agent::harness
