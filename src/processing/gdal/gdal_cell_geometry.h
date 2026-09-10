// gdal_cell_geometry.h — single owner of geographic-CRS pixel-geometry
// decisions for kernels and operators (Foundation 7.0 primitive
// consolidation; the degrees→metres arc conversion previously existed in
// four operator/streaming copies and the geographic test in two).
//
// The pure math (WGS84 arc lengths, scene-centre latitude) lives in
// MathUtils (dependency-light, unit-tested); this module composes it with
// the GDAL dataset view (geotransform + CRS test) so every consumer gets
// identical metre semantics.
#pragma once

#include <QString>

class GdalDatasetWrapper;

namespace sicnu::processing::gdal_util
{

/// True when @p wkt declares a geographic CRS (angular units — degrees,
/// not metres). Empty/unparsable WKT is not geographic.
bool isGeographicCrs( const QString &wkt );

/// Per-axis DEM pixel size in METRES for gradient/area math. Projected
/// CRS: |gt[1]| / |gt[5]| directly (with a 30 m default for degenerate
/// axes, and y falling back to x). Geographic CRS: scene-centre WGS84
/// arc conversion via MathUtils::wgs84ArcAtLatitudeDeg, so Horn
/// gradients and metre-denominated products agree across every family
/// (terrain, topographic correction, SAR masks — the #612 rule).
void cellSizesMetres( const GdalDatasetWrapper &ds, double *csx, double *csy );

} // namespace sicnu::processing::gdal_util
