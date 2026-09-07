// src/agent/harness/entity_resolver.h
#pragma once

//
// Harness 4.0 dataset grounding (mission Phase 4 / Phase 19).
//
// One authoritative resolver for every agent-supplied dataset reference.
// Anti-hallucination contract: the agent may name a dataset by stable entity
// id ("asset-3"), governed asset UUID, concrete path, or display name — the
// resolver decides which dataset that *is* against the registered catalog.
// It never guesses:
//   unknown        -> DATASET_NOT_FOUND (typed failure)
//   several hits   -> ENTITY_AMBIGUOUS with a bounded candidate list
//   path missing   -> DATASET_NOT_FOUND (with the probed location in details)
//

#include <json/json.h>
#include <optional>
#include <QString>

#include "harness_error.h"

namespace sicnu::data {
class AssetId;
}

namespace sicnu::agent::harness {

struct ResolvedDataset {
  QString path;          ///< File path / remote URI a GDAL/OGR call can consume.
  QString canonicalPath; ///< Workspace-entity natural key when cataloged (may be empty).
  QString assetId;       ///< Governed asset UUID string, empty when unregistered.
  QString assetEntityId; ///< Stable "asset-N" id, empty when unregistered.
  QString displayName;
  long long revision = 0;///< Asset revision, 0 when unregistered.
  QString resolution;    ///< "entity_id" | "asset_uuid" | "path" | "display_name" | "canvas_layer"

  Json::Value toJson() const;
};

/// Resolves one dataset reference. On failure `error` is set and the return
/// is empty. `rawRef` that is empty after trimming is INVALID_PARAMETER.
std::optional<ResolvedDataset> resolveDatasetRef( const QString &rawRef, HarnessError *error );

/// Lists bounded candidates for a failed display-name resolution so the agent
/// can re-ask with an exact id instead of guessing again (≤ 8 entries).
Json::Value datasetCandidates( const QString &rawRef );

} // namespace sicnu::agent::harness
