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
/// projection, never a pixel scan; the resolver truncates past this with an
/// explicit note. The JSON reader refuses a document over the cap — it does
/// not truncate a hostile array into a successful parse.
inline constexpr std::size_t kMaxPassportBands = 4096;

/// Hard upper bound on projected temporal collection references. The resolver
/// truncates its own projection to this; the JSON reader refuses documents
/// over it.
inline constexpr std::size_t kMaxPassportTemporalRefs = 256;

/// Section claims that are not per-band (identity, sensor, radiometric,
/// acquisition, geometry, validity, temporal, provenance, model-derived).
/// Headroom above the live set so a max-band resolver document still
/// round-trips through the JSON reader.
inline constexpr std::size_t kMaxPassportSectionClaims = 64;

/// ≤ 3 claims per projected band (role, wavelength, noData) plus the section
/// set above. The JSON reader refuses documents past this ceiling.
inline constexpr std::size_t kMaxPassportClaims =
    3 * kMaxPassportBands + kMaxPassportSectionClaims;

/// Notes follow the same structural budget as claims. The JSON reader
/// refuses documents past this ceiling (it does not truncate).
inline constexpr std::size_t kMaxPassportNotes = kMaxPassportClaims;

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_ASSET_STATE_SCHEMA_H
