// rs_roi_io.h — Phase 10A: persist RsRoiCollection to ESRI Shapefile + sidecar JSON
#pragma once
#include "qgis_analysis_export.h"
#include "qgscoordinatereferencesystem.h"
#include <QString>

class RsRoiCollection;

class QGIS_ANALYSIS_EXPORT RsRoiIO
{
  public:
    /**
     * Saves \a col to an ESRI Shapefile at \a shapefilePath. A sidecar JSON file
     * named "<basename>.classes.json" is written alongside containing the class
     * definitions (id/name/color).
     *
     * \a crs is the CRS of the ROI geometries (typically the source raster CRS).
     * When valid, the shapefile is written in that CRS with no coordinate
     * transform. When invalid, falls back to EPSG:4326 and logs a warning —
     * geometries are written as-is without a transform.
     *
     * NOTE: pixel indices are NOT persisted — they are raster-coordinate
     * dependent and must be recomputed by the caller against the active raster.
     */
    static bool save( const QString &shapefilePath, const RsRoiCollection &col,
                      const QgsCoordinateReferenceSystem &crs = QgsCoordinateReferenceSystem() );

    /**
     * Loads ROIs from \a shapefilePath into \a col. The sidecar JSON file (if
     * present) is also read to restore class definitions. Loaded ROIs have
     * empty pixelIndices() — the caller must recompute them.
     *
     * Features are read in the layer's native CRS. When \a targetCrs is valid
     * and differs from the layer CRS, geometries are transformed into
     * \a targetCrs before append. When \a targetCrs is invalid, geometries
     * are left in the layer CRS.
     */
    static bool load( const QString &shapefilePath, RsRoiCollection &col,
                      const QgsCoordinateReferenceSystem &targetCrs = QgsCoordinateReferenceSystem() );
};
