#pragma once

#include "virtual_raster_recipe.h"

namespace sicnu::data
{

class DataManager;

/// Preflights a virtual-raster recipe against the input assets' immutable
/// snapshots. Pure: reads only already-resolved structures (CRS, geotransform,
/// extent, band color interpretation) - it opens no datasets and mutates
/// nothing.
///
/// Classification, first match in priority order:
/// - `UnavailableSource` - an input AssetId is unknown, not a raster, in
///   Missing/Error state, or the referenced band is out of range. Hard.
/// - `MissingCRS` - an input lacks a CRS and the recipe has no target CRS. Hard.
/// - `NoOverlap` - the inputs' extent intersection is empty. Hard.
/// - `UnsupportedDataType` - a categorical (paletted) input with a continuous
///   resampling in the recipe. Hard.
/// - `RequiresReprojection` - input CRSs differ; creatable only when the
///   recipe carries an explicit target CRS.
/// - `RequiresResampling` - grids differ beyond tolerance; creatable only when
///   the recipe carries an explicit target resolution.
/// - `PartialOverlap` - intersection non-empty but smaller than some inputs.
///   Warning; creatable.
/// - `Compatible` - same CRS, same grid, full overlap.
///
/// `canCreate` is false exactly for hard failures and for warning verdicts the
/// recipe does not resolve with an explicit target.
PreflightResult preflightVirtualRaster( const VirtualRasterRecipe &recipe,
                                        const DataManager &manager );

} // namespace sicnu::data
