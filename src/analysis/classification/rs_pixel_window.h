// rs_pixel_window.h — map extent → half-open pixel window on a raster.
//
// Used by classification preview to restrict tile prediction to the canvas
// viewport. Apply stays full-raster (cropToWindow=false).
#pragma once

#include "qgis_analysis_export.h"
#include "qgsrectangle.h"

/// Half-open pixel window: [x0, x1) × [y0, y1) in raster column/row space.
struct QGIS_ANALYSIS_EXPORT RsPixelWindow
{
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  bool valid = false;

  int width() const { return x1 - x0; }
  int height() const { return y1 - y0; }
};

/// Map a world-space extent to a clamped half-open pixel window on a raster
/// of size W×H with GDAL geotransform `gt`. Returns valid=false when the
/// extent is disjoint from the raster or the transform cannot be inverted.
QGIS_ANALYSIS_EXPORT RsPixelWindow rsMapExtentToPixelWindow(
  const QgsRectangle &extent,
  const double gt[6],
  int W,
  int H );
