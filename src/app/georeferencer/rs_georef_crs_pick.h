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

/// Outcome of normalizing a picked target point into the panel's destination
/// CRS before it becomes a GCP (issue #1030, F-1030-P1-gcp).
struct RsGeorefGcpDestination
{
  /// The transform failed: the caller MUST NOT store the GCP. Persisting the
  /// raster-CRS point under the destination CRS tag silently corrupts
  /// rectification, and reporting success makes it invisible.
  bool refuse = false;
  QgsPointXY point;
};

/**
 * Normalize a picked target point into the destination CRS.
 *
 * An unreferenced raster (no CRS) or an unspecified destination CRS is not a
 * transform failure — the pick is stored as-is, the pre-existing workflow.
 * Every other case requires the transform to succeed; when it does not the
 * result refuses the GCP instead of storing a mismatched coordinate pair.
 */
RsGeorefGcpDestination rsGeorefNormalizeGcpDestination(
  const QgsPointXY &destPick, const QgsCoordinateReferenceSystem &rasterCrs,
  const QgsCoordinateReferenceSystem &destCrs, const QgsCoordinateTransformContext &context );
