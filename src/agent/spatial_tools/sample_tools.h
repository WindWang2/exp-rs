// src/agent/spatial_tools/sample_tools.h
#pragma once

// Phase C — bounded data-grounding SpatialTools:
//   spatial:sample_pixels   point reads from a raster (≤ 64 points)
//   spatial:sample_features attribute reads from a vector (≤ 20 features)
//   spatial:compare_rasters decimated difference verdict between two rasters
//
// Registered via registerSampleTools() from SpatialToolRegistry::registerBuiltinTools().

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/// Registers the sample/compare tools. Idempotent.
void registerSampleTools();

} // namespace sicnu::agent::spatial_tools
