#pragma once

#include <functional>
#include <optional>

#include <QString>
#include <QVector>

#include "../internal/source_provider.h"
#include "../virtual_raster_recipe.h"

namespace sicnu::data
{
class AssetSnapshot;
}

namespace sicnu::data::providers
{

/// Builds the GDAL VRT XML realizing `recipe` against the current input
/// snapshots (aligned with `recipe.inputs`). The output grid is the recipe's
/// target (or the first input's grid when unset) and the target extent is the
/// intersection (or union, per the extent policy) of the input extents. Same
/// CRS is assumed - cross-CRS reprojection is a deferred follow-up.
///
/// Shared by the provider's resolve() and the DataManager's regenerate-on-
/// relocate path. Returns an empty string when the grid cannot be derived
/// (e.g. no input has a geotransform).
QString buildVirtualRasterXml( const VirtualRasterRecipe &recipe,
                               const QVector<AssetSnapshot> &inputSnapshots );

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

  private:
    AssetLookup m_assetLookup;
};

} // namespace sicnu::data::providers
