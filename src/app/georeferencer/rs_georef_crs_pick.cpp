// rs_georef_crs_pick.cpp — pure CRS pick transform (F13, issue #1005).
#include "rs_georef_crs_pick.h"

#include <qgscoordinatetransform.h>
#include <qgsproject.h>

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
    // layer-CRS point — that silently stored a wrong GCP (#1005).
    return std::nullopt;
  }
}
