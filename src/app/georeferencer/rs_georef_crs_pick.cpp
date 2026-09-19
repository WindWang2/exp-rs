// rs_georef_crs_pick.cpp — pure CRS pick transform (F13, issue #1005).
#include "rs_georef_crs_pick.h"

#include <qgscoordinatetransform.h>
#include <qgsproject.h>

#include <cmath>

std::optional<QgsPointXY> rsGeorefTransformPickBetweenCrs(
  const QgsCoordinateReferenceSystem &canvasCrs, const QgsCoordinateReferenceSystem &layerCrs,
  const QgsCoordinateTransformContext &context, const QgsPointXY &canvasMapPt )
{
  // An unreferenced raster has no layer CRS: canvas picks ARE raw image
  // coordinates, and the pre-F13 georeferencing workflow depends on that
  // pass-through — preserve it (a missing layer CRS is not a transform
  // failure, which is what #1005 is about).
  if ( !layerCrs.isValid() )
    return canvasMapPt;
  if ( !canvasCrs.isValid() )
    return std::nullopt;
  // Same CRS: the identity is mathematically exact, not a failure path.
  if ( canvasCrs == layerCrs )
    return canvasMapPt;

  try
  {
    const QgsCoordinateTransform ct( canvasCrs, layerCrs, context );
    return ct.transform( canvasMapPt );
  }
  catch ( ... )
  {
    // A failed transform must never surface the raw canvas coordinate as a
    // layer-CRS point �� that silently stored a wrong GCP (#1005).
    return std::nullopt;
  }
}

RsGeorefGcpDestination rsGeorefNormalizeGcpDestination(
  const QgsPointXY &destPick, const QgsCoordinateReferenceSystem &rasterCrs,
  const QgsCoordinateReferenceSystem &destCrs, const QgsCoordinateTransformContext &context )
{
  RsGeorefGcpDestination out;
  // Unreferenced raster, no destination CRS chosen yet, or identical CRS: the
  // pick is already the value that is stored. Not a failure path.
  if ( !rasterCrs.isValid() || !destCrs.isValid() || rasterCrs == destCrs )
  {
    out.point = destPick;
    return out;
  }
  const std::optional<QgsPointXY> transformed =
    rsGeorefTransformPickBetweenCrs( rasterCrs, destCrs, context, destPick );
  if ( !transformed.has_value() )
  {
    // #1030 F-1030-P1-gcp: fail closed. Storing this point under the
    // destination CRS tag would let a silently wrong GCP reach the warp.
    out.refuse = true;
    out.point = destPick;
    return out;
  }
  if ( !std::isfinite( transformed->x() ) || !std::isfinite( transformed->y() ) )
  {
    // An out-of-domain pick PROJ could not place anywhere: refusing beats
    // persisting a NaN GCP.
    out.refuse = true;
    out.point = destPick;
    return out;
  }
  out.point = *transformed;
  return out;
}
