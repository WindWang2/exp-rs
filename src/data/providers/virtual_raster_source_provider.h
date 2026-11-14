#pragma once

#include <functional>
#include <optional>

#include <QString>
#include <QVector>

#include "../internal/source_provider.h"
#include "../virtual_raster_recipe.h"

namespace sicnu::geo
{
struct ConvertProgress;
}

namespace sicnu::data
{
class AssetSnapshot;
}

namespace sicnu::data::providers
{

/// One laid-out input for VRT composition: the file the VRT references and
/// the raster structure its placement is computed from. Normally the input
/// asset's own snapshot values; a reprojection/rotation recipe instead warps
/// the input onto the target grid first (R29-T02 WP06) and passes the WARPED
/// file's values here, so the VRT's axis-aligned SrcRect/DstRect math is
/// always applied to a grid already in the target CRS.
struct VirtualRasterSourceInput
{
  QString sourcePath;
  RasterStructure structure;
};

/// A target grid pinned by the caller (warp path): the builder uses these
/// values verbatim instead of deriving them from the inputs, so the warp
/// staging grid and the composed VRT grid are exactly the same grid.
struct VirtualRasterPinnedGrid
{
  QString crs;
  double resX = 0.0;
  double resY = 0.0;
  SpatialExtent extent;
};

/// Builds the GDAL VRT XML realizing `recipe` against the current input
/// snapshots (aligned with `recipe.inputs`). The output grid is the recipe's
/// target (or the first input's grid when unset) and the target extent is the
/// intersection (or union, per the extent policy) of the input extents.
/// Same-CRS north-up inputs compose directly; CRS-differing and rotated/
/// sheared inputs are WARPED onto the target grid first (R29-T02 WP06) —
/// this function owns that whole layout, so the provider's resolve() and the
/// DataManager's relocate rewrite produce the SAME artifact for one recipe.
///
/// `vrtPath` names the VRT artifact being (re)generated: warp intermediates
/// are staged beside it under deterministic recipe-keyed names
/// (`<vrt>.warp<i>-<digest8>.tif`). R30-T03: this function NEVER sweeps
/// previous generations — deleting the dependencies of the still-published
/// VRT before the new one is committed left the old artifact unreadable on
/// any later publish failure. Instead, when @p liveIntermediatesOut is
/// non-null it receives the file names of THIS generation's intermediates
/// and the caller sweeps stale `<vrt>.warp*` names only after the final
/// publish committed (Ok); on any failure — including a typed post-publish
/// VerifyFailed — nothing is swept, so every VRT on disk keeps referencing
/// existing files. Returns the XML, or an empty string on failure with
/// @p errorCode / @p errorMessage set ("virtual_raster.grid_invalid" style
/// codes).
QString layoutVirtualRasterXml( const VirtualRasterRecipe &recipe,
                                const QVector<AssetSnapshot> &inputSnapshots,
                                const QString &vrtPath,
                                QString *errorCodeOut = nullptr,
                                QString *errorMessageOut = nullptr,
                                QStringList *liveIntermediatesOut = nullptr,
                                sicnu::geo::ConvertProgress *progress = nullptr );

/// Best-effort sweep of stale warp intermediates beside @p vrtPath: removes
/// files named `<vrt>.warp*` that are NOT in @p liveNames (the current
/// generation reported by layoutVirtualRasterXml). Call ONLY after the VRT
/// itself published successfully — a still-served previous VRT must keep its
/// dependencies readable (R30-T03 generation contract).
void sweepStaleWarpIntermediates( const QString &vrtPath,
                                  const QStringList &liveNames );

/// Legacy pure-XML overload: identical composition for recipes whose inputs
/// are all same-CRS north-up (no warp staging). Kept for callers that only
/// need the XML bytes of a known-composable recipe.
QString buildVirtualRasterXml( const VirtualRasterRecipe &recipe,
                               const QVector<AssetSnapshot> &inputSnapshots );

/// Overload with the caller-laid-out inputs and an optional pinned target
/// grid (R29-T02 WP06). When @p pinnedGrid is set, its CRS/resolution/extent
/// are used verbatim — the caller guarantees the inputs' structures already
/// live on that grid (warmed intermediates) or are natively compatible with
/// it; when null, the grid is derived exactly as in the snapshot overload.
QString buildVirtualRasterXml( const VirtualRasterRecipe &recipe,
                               const QVector<VirtualRasterSourceInput> &inputs,
                               const VirtualRasterPinnedGrid *pinnedGrid );

/// Source provider for Virtual Raster Assets.
///
/// The provider's SourceDescriptor carries providerKey `vrt`, the managed
/// scratch path for the generated `.vrt` as canonicalSource, and the recipe
/// JSON in `dataOptions["recipe"]`. resolve() looks up each input's current
/// canonical source through the injected lookup (bound to the owning
/// DataManager), generates the VRT XML at the scratch path, and resolves the
/// structure from it. The `.vrt` file is a disposable build artifact - the
/// recipe is the identity.
class VirtualRasterSourceProvider final : public internal::SourceProvider
{
  public:
    /// Looks up an asset's current snapshot by id. Bound to the owning
    /// DataManager at construction (internal seam; never exposed publicly).
    using AssetLookup =
      std::function<std::optional<AssetSnapshot>( AssetId )>;

    explicit VirtualRasterSourceProvider( AssetLookup assetLookup );

    bool supports( const SourceDescriptor &source ) const override;
    Result<internal::ResolvedSource> resolve( const SourceDescriptor &source ) const override;
    /// Progress/cancel-aware resolution (R30-T03 WP04): the sink is forwarded
    /// into the warp staging (geo::warpRaster's ConvertProgress contract), so
    /// a large recipe reports staged progress and cancels cooperatively with
    /// the typed "virtual_raster.cancelled" failure.
    Result<internal::ResolvedSource> resolve(
      const SourceDescriptor &source,
      sicnu::geo::ConvertProgress *progress ) const override;

  private:
    AssetLookup m_assetLookup;
};

} // namespace sicnu::data::providers
