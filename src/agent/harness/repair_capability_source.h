// src/agent/harness/repair_capability_source.h
#pragma once

//
// Live adapter: CapabilityKnowledge -> the repair planner's provider seam.
//
// The repair planner (src/repair_planner) is a portable leaf and consumes
// capability entries only through RepairCapabilityProvider. This adapter is
// the READ-ONLY production source for that seam: it snapshots the live
// knowledge documents (data/agent/capabilities/*.json, family defaults
// merged) and hands them to the provider's closed family router. It ships
// no operator ids of its own — an entry the knowledge layer no longer has
// simply cannot be offered, and a renamed or re-costed entry changes the
// candidates with it. One authority, no second operator table.
//
// Planning-only: nothing here executes a repair or mutates the knowledge.

#include <json/json.h>
#include <string>
#include <vector>

#include "repair_planner/repair_provider.h"
#include "repair_planner/repair_schema.h"

namespace sicnu::agent::harness {

/// Read-only snapshot of the live capability knowledge entries: the raw,
/// as-shipped document per operator id (family-default documents excluded —
/// they are knowledge, not operators, and never route). Reloads the
/// knowledge when it has not been loaded yet. Empty only when the knowledge
/// layer itself has no entries — the adapter never invents one.
std::vector<Json::Value> liveRepairCapabilityEntries();

/// Builds the repair planner provider from the live knowledge. Fails typed
/// when an entry cannot identify itself (the provider's own fail-closed
/// build path) — never a best-effort subset.
bool liveRepairCapabilityProvider(sicnu::repair::JsonRepairCapabilityProvider &out,
                                  sicnu::repair::RepairError &error);

} // namespace sicnu::agent::harness
