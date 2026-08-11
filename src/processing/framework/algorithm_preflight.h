// src/processing/framework/algorithm_preflight.h
#pragma once

#include "atomic_algorithm_adapter.h"

#include <json/json.h>
#include <string>

namespace sicnu::processing {

/**
 * Preflight a planned algorithm invocation without executing it — the
 * "PLAN → PREFLIGHT → EXECUTE" contract for agents. Returns a structured
 * Json::Value:
 *
 *   {
 *     "algorithmId", "valid",
 *     "schemaValidation": {valid, errors[], warnings[]},
 *     "parameters": {missing[], unknown[]},
 *     "datasets": { "<port>": {path, exists, width, height, bands, dataType,
 *                              crs, radiometricState} },
 *     "compatibility": {ok, issues[]},
 *     "resources": {tileWidth, tileHeight, estimatedRamBytes,
 *                   temporaryDiskBytes, basis: "dynamic"|"static"|"unknown"},
 *     "metadata": {memoryPolicy, largeRasterSafe, supportsCancellation,
 *                  deterministic, costClass},
 *     "warnings": [], "blockers": []
 *   }
 *
 * Performs no mutation and no long-running work beyond lightweight GDAL
 * dataset probing (open/close) for raster inputs.
 */
Json::Value preflightAlgorithm( const std::string &algorithmId, const Json::Value &params );

/// Preflight against an already-resolved adapter (shared implementation).
Json::Value preflightAdapter( const AtomicAlgorithmAdapter &adapter, const Json::Value &params );

} // namespace sicnu::processing
