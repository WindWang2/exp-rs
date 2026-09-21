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

/// Hard upper bound on projected bands. Passport resolution is a metadata
/// projection, never a pixel scan; pathological inputs (10k-band rasters)
/// are truncated with an explicit note, never silently.
inline constexpr std::size_t kMaxPassportBands = 4096;

/// Hard upper bound on explicit claim records kept in one passport.
inline constexpr std::size_t kMaxPassportClaims = 1024;

/// Hard upper bound on projected temporal collection references.
inline constexpr std::size_t kMaxPassportTemporalRefs = 256;

/// Hard upper bound on resolution notes attached to one passport.
inline constexpr std::size_t kMaxPassportNotes = 1024;

} // namespace sicnu::state

#endif // SICNU_SCIENTIFIC_STATE_ASSET_STATE_SCHEMA_H
