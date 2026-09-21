/***************************************************************************
  scientific_state/asset_state_schema.h
  RS14-01 Scientific Data Passport — schema identity constants.

  The asset state layer is a READ-ONLY, versioned projection of recorded
  truth (catalog snapshots, GDAL metadata, sidecars, provenance). It never
  replaces an existing source of truth and never persists anything itself.
  Schema ids follow the repo convention "sicnu.<name>.v<N>"
  (see sicnu.labreport.v1, exp.scientific_contract.v1).
 ***************************************************************************/

#ifndef SICNU_SCIENTIFIC_STATE_ASSET_STATE_SCHEMA_H
#define SICNU_SCIENTIFIC_STATE_ASSET_STATE_SCHEMA_H

#include <cstddef>

namespace sicnu::state
{

/// Canonical schema id of a serialized RemoteSensingAssetState document.
inline constexpr const char *kAssetStateSchemaId = "sicnu.asset_state.v1";

/// Canonical schema id of a serialized state diff document.
inline constexpr const char *kAssetStateDiffSchemaId = "sicnu.asset_state_diff.v1";

/// Overall resolvability confidence (state.confidence, [0,1]) — deterministic
/// lattice over 8 key claim paths. Each applicable path scores
///   known=1.0, inferred=0.75, assumed=0.25, conflicted/unknown=0
/// and confidence = total / applicable-path count, rounded to 3 decimals.
/// A path is applicable when its source exists; absent sources never dilute:
///   identity.asset_id     — only when a catalog is present
///   sensor.modality       — always
///   bands[*].role         — when bands exist; scores the worst band (full
///                           credit only when every band role resolves)
///   radiometric.unit      — always
///   acquisition.time      — always
///   geometry.crs          — only when a dataset is present
///   provenance.algorithm  — only when a derivation record is present
///   validity.noDataPolicy — only when bands exist
/// Computed by the resolver (asset_state_resolver.cpp); see
/// tests/test_scientific_state_diff.cpp for the pinned scenarios.

/// Hard upper bound on projected bands. Passport resolution is a metadata
/// projection, never a pixel scan; pathological inputs (10k-band rasters)
/// are truncated with an explicit note, never silently.
inline constexpr std::size_t kMaxPassportBands = 4096;

/// Hard upper bound on projected temporal collection references.
inline constexpr std::size_t kMaxPassportTemporalRefs = 256;

/// Claim and note counts are not independently capped: they are structurally
/// bounded by the input bounds above (≤ 2 claims per projected band plus a
/// fixed set of section claims; ≤ 1 note per observation anomaly), so no
/// pathological input can inflate them beyond O(kMaxPassportBands).

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_ASSET_STATE_SCHEMA_H
