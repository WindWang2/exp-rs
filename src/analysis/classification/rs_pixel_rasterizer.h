// rs_pixel_rasterizer.h — Phase 10A Task 10.4: rasterize a vector geometry
// to a set of raster pixel indices.
//
// Used by:
//   * RsRoiToolBase subclasses (via main window) to compute the pixel set
//     covered by a drawn ROI (so training samples can be extracted from the
//     source raster).
//   * Later tasks (JM separability, training) that need pixel coverage.
//
// Encoding: each pixel index = row * width + col.
//
// Algorithm: builds a 1-band GDAL `MEM` raster of size W*H with the supplied
// GeoTransform, calls GDALRasterizeGeometries on the geometry with burn
// value 1.0, then scans the band collecting all non-zero pixel indices.
#pragma once

#include "qgis_analysis_export.h"
#include "qgsgeometry.h"

#include <QSet>
#include <cstdint>

class QGIS_ANALYSIS_EXPORT RsPixelRasterizer
{
  public:
    /// Rasterize `geom` against a virtual raster of size W*H with affine
    /// GeoTransform `gt` (6 doubles, GDAL convention). Returns the set of
    /// pixel indices (row * W + col) that the geometry burns into.
    static QSet<quint64> rasterize( const QgsGeometry &geom,
                                    const double gt[6],
                                    int width,
                                    int height );
};
