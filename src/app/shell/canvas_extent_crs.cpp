/***************************************************************************
 * canvas_extent_crs.cpp — see header for the contract
 ***************************************************************************/
#include "canvas_extent_crs.h"

#include <qgscoordinatetransform.h>
#include <qgsexception.h>

#include <exception>

namespace sicnu::app
{

std::optional<QgsRectangle> rsTransformExtentForCanvas(
  const QgsRectangle &extent, const QgsCoordinateReferenceSystem &layerCrs,
  const QgsCoordinateReferenceSystem &canvasCrs,
  const QgsCoordinateTransformContext &context )
{
  // No transform required: unreferenced layer, unknown canvas CRS, or the
  // exact identity. This preserves the georeferencing / raw-pixel workflow.
  if ( !layerCrs.isValid() || !canvasCrs.isValid() || layerCrs == canvasCrs )
    return extent;

  try
  {
    const QgsCoordinateTransform ct( layerCrs, canvasCrs, context );
    // A valid-but-unbuildable operation would otherwise return the input
    // rectangle silently (see the header) — refuse it explicitly.
    if ( !ct.isValid() )
      return std::nullopt;
    return ct.transformBoundingBox( extent );
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

} // namespace sicnu::app
