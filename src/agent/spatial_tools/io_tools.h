// src/agent/spatial_tools/io_tools.h
#pragma once

// Foundation 5.0 — thin read-only I/O tools over the Qt-free geospatial
// foundation (src/geospatial). No format parsing lives in the wrappers:
//   io:probe            what is this resource (format/product/COG)
//   io:capabilities     what can this dataset do (window/multidim/remote...)
//   io:product          normalized sensor product metadata + constituents
//
// Registered via registerIoTools() from SpatialToolRegistry::registerBuiltinTools().

#include "spatial_tool.h"

namespace sicnu::agent::spatial_tools {

/// Registers the I/O tools. Idempotent.
void registerIoTools();

} // namespace sicnu::agent::spatial_tools
