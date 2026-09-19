// rs_georef_crs_pick.cpp — pure CRS pick transform (F13, issue #1005).
#include "rs_georef_crs_pick.h"

#include <qgscoordinatetransform.h>
#include <qgsexception.h>
#include <qgsproject.h>

#include <exception>

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
    // A valid-but-unbuildable operation (e.g. horizontal -> vertical CRS)
    // makes transform() return the INPUT silently instead of throwing, so
    // it must be refused explicitly (#1037).
    if ( !ct.isValid() )
      return std::nullopt;
    return ct.transform( canvasMapPt );
  }
  catch ( ... )
  {
    // A failed transform must never surface the raw canvas coordinate as a
    // layer-CRS point — that silently stored a wrong GCP (#1005).
    return std::nullopt;
  }
}

std::optional<QgsPointXY> rsGeorefTransformDestinationForStore(
  const QgsCoordinateReferenceSystem &rasterCrs, const QgsCoordinateReferenceSystem &targetCrs,
  const QgsCoordinateTransformContext &context, const QgsPointXY &destMap )
{
  // An unreferenced raster (or a panel without a target CRS yet) stores the
  // raw reference-canvas coordinate; the legacy guard passed it through and
  // that workflow is preserved.
  if ( !rasterCrs.isValid() || !targetCrs.isValid() )
    return destMap;
  // Same CRS: the identity is mathematically exact, not a failure path.
  if ( rasterCrs == targetCrs )
    return destMap;

  try
  {
    const QgsCoordinateTransform ct( rasterCrs, targetCrs, context );
    // Fail closed (#1037 F-1030-P1-gcp): an unbuildable operation would make
    // transform() return the untransformed input, which must never be stored
    // on a GCP tagged with the target CRS.
    if ( !ct.isValid() )
      return std::nullopt;
    return ct.transform( destMap );
  }
  catch ( const QgsCsException & )
  {
    return std::nullopt;
  }
  catch ( const std::exception & )
  {
    return std::nullopt;
  }
}
