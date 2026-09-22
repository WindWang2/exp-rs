#pragma once

// suitability_agent_adapter.h — machine-readable entry points for agent /
// MCP consumption, living INSIDE sicnu_suitability so the adapter is testable
// without linking the agent library (which drags qgis_gui).
//
// The agent-side wiring (tool defs table, prefix check, dispatch) is a thin
// shell in src/agent/data_platform_tools.cpp that forwards here. Contract,
// mirroring the other data-platform tools:
//   - a structurally broken REQUEST (missing goal, unparsable JSON document,
//     unknown profile key, dataset_version_id without dataset_db) throws
//     std::runtime_error — the MCP layer converts it to an isError result;
//   - a well-formed request whose CONTENT is invalid (goal validation, an
//     assessment that cannot run) is a COMPLETED call returning
//     { "valid": false, "diagnostics": [...] } — the typed answer is on
//     record instead of an error.
// A successful assess returns the versioned report JSON verbatim next to the
// derived overall level, gap list and teaching lines, so a caller can
// re-verify via SuitabilityReport::fromJson + contentDigest ("callable and
// verifiable", never "callable but uncheckable").

#include <QVariantMap>

namespace sicnu::suitability::agent_adapter
{

/// suitability:assess — args: goal (JSON document string, required),
/// dataset_db (store path, optional), dataset_version_id (requires
/// dataset_db), scenes (SceneCandidate JSON array document, optional).
QVariantMap suitabilityAssess( const QVariantMap &args );

/// suitability:profiles — args: profile (one key, optional; omit for all).
QVariantMap suitabilityProfiles( const QVariantMap &args );

} // namespace sicnu::suitability::agent_adapter
