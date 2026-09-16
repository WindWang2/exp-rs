// rs_georef_crs_pick.h — pure CRS pick transform (F13, issue #1005).
//
// The georeferencer stores picked map coordinates in the raster layer's
// CRS. A pick that cannot be transformed must fail closed: returning the
// raw canvas coordinate instead silently stored a wrong GCP. The behavior
// lives in this dependency-light translation unit so it stays unit-testable
// without instantiating the shell window.
#pragma once

#include <optional>

#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransformcontext.h>
#include <qgspointxy.h>

/**
 * Transform a canvas map pick into the layer CRS.
 *
 * Returns the identical point when both CRS are equal (mathematically exact
 * identity), the transformed point for a valid differing pair, and
 * std::nullopt when either CRS is invalid or the transform throws — never
 * the untransformed input (issue #1005).
 */
std::optional<QgsPointXY> rsGeorefTransformPickBetweenCrs(
  const QgsCoordinateReferenceSystem &canvasCrs, const QgsCoordinateReferenceSystem &layerCrs,
  const QgsCoordinateTransformContext &context, const QgsPointXY &canvasMapPt );
