// rs_pixel_window.h — map-extent → raster pixel half-open window
#pragma once

#include "qgis_analysis_export.h"
#include "qgsrectangle.h"

/**
 * Half-open pixel rectangle [x0, x1) × [y0, y1) in raster column/row space.
 * Used by classification preview to crop Task output to the current viewport.
 */
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

/**
 * Convert a map-space \a extent to a clamped pixel window for a raster with
 * geotransform \a gt and size \a W × \a H.
 *
 * Returns valid=false when the extent does not intersect the raster, when
 * dimensions are non-positive, or when the geotransform cannot be inverted.
 * Coordinates use half-open intervals: columns [x0,x1), rows [y0,y1).
 */
QGIS_ANALYSIS_EXPORT RsPixelWindow rsMapExtentToPixelWindow(
  const QgsRectangle &extent,
  const double gt[6],
  int W,
  int H );
