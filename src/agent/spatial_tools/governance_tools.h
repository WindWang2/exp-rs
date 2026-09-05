// governance_tools.h — Workspace Governance 3.0 agent surfaces (Platform 3.0
// Phase T). Bounded, structured, paginated tools over the WorkspaceService:
//
//   project:summary    project:search     project:health
//   asset:inspect      asset:validate     asset:relink
//   collection:query   lineage:upstream   lineage:downstream
//   result:inspect     run:compare
//
// No tool ever materializes the whole workspace: every listing is paged
// (limit clamped 1..100) and every diagnostic list is capped.
#pragma once

namespace sicnu::agent::spatial_tools
{

/// Registers all governance tools in the SpatialToolRegistry (idempotent).
void registerGovernanceTools();

} // namespace sicnu::agent::spatial_tools
