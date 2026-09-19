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

/// Transform a picked destination point from the reference raster CRS into the
/// panel's target CRS before it is stored on a GCP (#1037 F-1030-P1-gcp).
/// Fail-closed: a REQUIRED transform that is unbuildable (isValid()==false) or
/// throws returns std::nullopt — never the untransformed input.
/// Semantics mirroring the legacy guard: equal CRS, or either CRS invalid,
/// return destMap unchanged (unreferenced-raster workflow).
std::optional<QgsPointXY> rsGeorefTransformDestinationForStore(
  const QgsCoordinateReferenceSystem &rasterCrs, const QgsCoordinateReferenceSystem &targetCrs,
  const QgsCoordinateTransformContext &context, const QgsPointXY &destMap );
