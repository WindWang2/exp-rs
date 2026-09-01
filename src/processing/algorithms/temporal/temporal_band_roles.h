// src/processing/algorithms/temporal/temporal_band_roles.h
// Shared band-role resolution for temporal operators.
//
// Mirrors the single-scene rs:spectral_index semantics exactly (role metadata
// first, conventional positional fallback with a warning) so temporal series
// and single-scene results stay interchangeable. This replaces the per-file
// bandWithRole lambdas with one shared resolver.
#pragma once

#include <QString>
#include <QStringList>

class GdalDatasetWrapper;

namespace sicnu::data
{
enum class BandRole;
}

namespace sicnu::temporal
{

/// 1-based band number carrying @a role (SICNU_BAND_ROLE metadata), or 0.
int findBandWithRole( const GdalDatasetWrapper &ds, sicnu::data::BandRole role );

/// Conventional positional fallback for @a roleId ("nir"→4, "red"→3,
/// "green"→2, "blue"→1, "swir1"→5, "swir2"→min(6, bandCount),
/// "red_edge"→min(5, bandCount)); 0 for unknown roles.
int positionalFallbackBand( const QString &roleId, int bandCount );

/// Resolves @a roleId in @a ds: explicit @a overrideBand (when > 0) wins, then
/// SICNU_BAND_ROLE metadata, then the positional fallback. Sets @a usedFallback
/// when the positional fallback fired (callers log a warning once per run).
int resolveBand( const GdalDatasetWrapper &ds, const QString &roleId,
                 int overrideBand, bool *usedFallback = nullptr );

/// Roles understood by the temporal index series (same set the single-scene
/// operator resolves via kernels). Sorted, stable for schema enums.
QStringList temporalBandRoleIds();

} // namespace sicnu::temporal
