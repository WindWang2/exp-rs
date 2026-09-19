/***************************************************************************
 * canvas_extent_crs.h — fail-closed canvas extent reprojection
 *
 * A layer extent that cannot be transformed into the canvas CRS must never be
 * applied anyway (source-CRS numbers on a destination-CRS canvas = silently
 * wrong spatial product, #1005/#1037). Two QGIS behaviours make a plain
 * try/catch insufficient:
 *   - transformBoundingBox throws for geocentric CRS pairs, and
 *   - a valid-but-unbuildable operation (ct.isValid() == false, e.g. a
 *     horizontal -> vertical CRS pair) makes transform() return the INPUT
 *     without throwing.
 * The single seam below encodes both, so every zoom/extent path shares one
 * testable contract.
 ***************************************************************************/
#pragma once

#include <optional>

#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransformcontext.h>
#include <qgsrectangle.h>

namespace sicnu::app
{

/**
 * Transform @p extent from @p layerCrs into @p canvasCrs.
 *
 * Returns the input extent unchanged when no transform is required (either
 * CRS invalid, or both equal — the unreferenced-raster workflow), and
 * std::nullopt when a REQUIRED transform is unbuildable or throws. Callers
 * must leave the canvas untouched on nullopt; they must never present the
 * source-CRS rectangle on the target canvas.
 */
std::optional<QgsRectangle> rsTransformExtentForCanvas(
  const QgsRectangle &extent, const QgsCoordinateReferenceSystem &layerCrs,
  const QgsCoordinateReferenceSystem &canvasCrs,
  const QgsCoordinateTransformContext &context );

} // namespace sicnu::app
