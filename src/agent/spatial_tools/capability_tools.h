// src/agent/spatial_tools/capability_tools.h
#pragma once

// Phase D/E — capability discovery + automatic model selection:
//   spatial:search_capabilities  ranked algorithm/model candidates (no schemas)
//   spatial:select_model         ranked model candidates from ModelCatalog
//
// Registered via registerCapabilityTools() from SpatialToolRegistry::registerBuiltinTools().

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/// Registers the capability tools. Idempotent.
void registerCapabilityTools();

} // namespace sicnu::agent::spatial_tools
