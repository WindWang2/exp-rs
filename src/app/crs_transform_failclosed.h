// crs_transform_failclosed.h — fail-closed CRS helpers (#1005 / F-1030-P1-crs).
// On invalid CRS or QgsCsException, return nullopt; never the untransformed input.
#pragma once

#include <cmath>
#include <optional>

#include <qgsexception.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatetransformcontext.h>
#include <qgspointxy.h>
#include <qgsrectangle.h>

inline std::optional<QgsPointXY> rsTransformPointFailClosed(
  const QgsCoordinateReferenceSystem &sourceCrs,
  const QgsCoordinateReferenceSystem &destCrs,
  const QgsCoordinateTransformContext &context,
  const QgsPointXY &point )
{
  if ( !sourceCrs.isValid() || !destCrs.isValid() )
    return std::nullopt;
  if ( sourceCrs == destCrs )
    return point;
  try
  {
    const QgsCoordinateTransform ct( sourceCrs, destCrs, context );
    if ( !ct.isValid() )
      return std::nullopt;
    const QgsPointXY out = ct.transform( point );
    if ( !std::isfinite( out.x() ) || !std::isfinite( out.y() ) )
      return std::nullopt;
    return out;
  }
  catch ( const QgsCsException & )
  {
    return std::nullopt;
  }
}

inline std::optional<QgsRectangle> rsTransformBoundingBoxFailClosed(
  const QgsCoordinateReferenceSystem &sourceCrs,
  const QgsCoordinateReferenceSystem &destCrs,
  const QgsCoordinateTransformContext &context,
  const QgsRectangle &extent )
{
  if ( !sourceCrs.isValid() || !destCrs.isValid() )
    return std::nullopt;
  if ( sourceCrs == destCrs )
    return extent;
  try
  {
    const QgsCoordinateTransform ct( sourceCrs, destCrs, context );
    if ( !ct.isValid() )
      return std::nullopt;
    const QgsRectangle out = ct.transformBoundingBox( extent );
    if ( out.isEmpty() && !extent.isEmpty() )
      return std::nullopt;
    return out;
  }
  catch ( const QgsCsException & )
  {
    return std::nullopt;
  }
}
