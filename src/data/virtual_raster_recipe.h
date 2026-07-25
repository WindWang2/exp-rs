#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "asset_types.h"
#include "data_result.h"

namespace sicnu::data
{

/// One band of one Data Asset as an input to a Virtual Raster composition.
/// References the asset by identity (never by path), so relocation of the
/// underlying source keeps the recipe valid across re-resolution.
struct BandRef
{
  AssetId asset;
  /// 1-based band number within the asset.
  int bandNumber = 1;

  friend bool operator==( const BandRef &, const BandRef & ) = default;
};

/// How the output extent is derived from the input extents. `Intersection`
/// (the default) uses the spatial intersection of all inputs; an empty
/// intersection rejects creation. `Union` (an explicit advanced choice) uses
/// the union, filling uncovered areas with NoData.
enum class ExtentPolicy
{
  Intersection,
  Union,
};

/// Resampling applied when an input grid does not match the target grid.
/// Categorical inputs must use `NearestNeighbour`; a categorical input with a
/// continuous method is `UnsupportedDataType` at preflight.
enum class ResamplingMethod
{
  NearestNeighbour,
  Bilinear,
  Cubic,
};

/// How NoData is carried into the composition. `Preserve` keeps each input's
/// own NoData value; `FillValue` writes `noDataFillValue` where a union extent
/// leaves pixels uncovered.
enum class NoDataPolicy
{
  Preserve,
  FillValue,
};

/// The identity-bearing recipe of a Virtual Raster Asset: ordered BandRef
/// inputs, target CRS/grid, extent policy, resampling, and NoData policy.
///
/// The recipe is the persisted, dedup-keyed form of the composition — the
/// `.vrt` it produces is a disposable build artifact, never the source of
/// truth. Band `i` of the output raster is `inputs[i]`. It is a plain value
/// with no live handles and no paths — inputs are AssetIds, so the recipe
/// survives source relocation and project round-trips.
///
/// Value-domain transformation (spec line 336: TOA/BOA/temperature semantics)
/// is deliberately NOT part of this type — it belongs to the normalized
/// spectral metadata wave (#7).
struct VirtualRasterRecipe
{
  /// Ordered inputs; band i of the output is inputs[i]. Must be non-empty.
  QVector<BandRef> inputs;
  /// Target CRS (authid or WKT). Empty means "the common input CRS"; when
  /// inputs differ, the recipe must carry an explicit target.
  QString targetCrs;
  /// Target pixel size in target-CRS units. Zero means "the first input's
  /// resolution"; when grids differ, the recipe must carry an explicit target.
  double targetResolutionX = 0.0;
  double targetResolutionY = 0.0;
  ExtentPolicy extentPolicy = ExtentPolicy::Intersection;
  ResamplingMethod resampling = ResamplingMethod::Bilinear;
  NoDataPolicy noDataPolicy = NoDataPolicy::Preserve;
  double noDataFillValue = 0.0;

  QJsonObject toJson() const;

  /// Parses a recipe from JSON. Strict: rejects empty inputs, band numbers
  /// below 1, invalid AssetId strings, and unknown enum spellings with a
  /// `recipe.invalid` diagnostic.
  static Result<VirtualRasterRecipe> fromJson( const QJsonObject &json );

  friend bool operator==( const VirtualRasterRecipe &,
                          const VirtualRasterRecipe & ) = default;
};

/// Structured outcome of a virtual-raster preflight, classified so creation
/// can fail (or proceed with an explicit target) before anything is
/// registered. Hard failures are `NoOverlap`, `MissingCRS` (without an
/// explicit target CRS), `UnavailableSource`, and `UnsupportedDataType`.
enum class PreflightVerdict
{
  Compatible,
  RequiresReprojection,
  RequiresResampling,
  PartialOverlap,
  NoOverlap,
  MissingCRS,
  UnavailableSource,
  UnsupportedDataType,
};

/// The result of preflighting a recipe against the input assets' snapshots.
/// `canCreate` is false for hard-failure verdicts, and also for warning
/// verdicts (`RequiresReprojection` / `RequiresResampling`) that the recipe
/// does not resolve with an explicit target CRS / target resolution;
/// `diagnostics` carries human-readable detail the UI can present verbatim.
struct PreflightResult
{
  PreflightVerdict verdict = PreflightVerdict::Compatible;
  bool canCreate = true;
  QVector<Diagnostic> diagnostics;
};

} // namespace sicnu::data
