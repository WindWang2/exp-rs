// rs_pixel_rasterizer.h — Phase 10A Task 10.4: rasterize a vector geometry
// to a set of raster pixel indices.
//
// Used by:
//   * RsRoiToolBase subclasses (via main window) to compute the pixel set
//     covered by a drawn ROI (so training samples can be extracted from the
//     source raster).
//   * Later tasks (JM separability, training) that need pixel coverage.
//
// Encoding: each pixel index = row * width + col.
//
// Algorithm: clips the geometry to the raster footprint in map space, builds
// a 1-band GDAL `MEM` raster covering only the geometry's bounding-box
// window (never the full W*H), calls GDALRasterizeGeometries on the geometry
// with burn value 1.0, then scans the band collecting all non-zero pixel
// indices.
#pragma once

#include "qgis_analysis_export.h"
#include "qgsgeometry.h"

#include <QSet>

#include <cstdint>
#include <functional>

class QGIS_ANALYSIS_EXPORT RsPixelRasterizer
{
  public:
    /// Rasterize `geom` against a virtual raster of size W*H with affine
    /// GeoTransform `gt` (6 doubles, GDAL convention). Returns the set of
    /// pixel indices (row * W + col) that the geometry burns into.
    ///
    /// Fail-closed contract (#1056): when \a ok is non-null it is set to
    /// false iff an infrastructure failure prevented computing the coverage
    /// (non-invertible GeoTransform, GDAL driver/dataset creation failure,
    /// WKB decode failure, GDALRasterizeGeometries failure, or a per-scanline
    /// RasterIO failure); the returned set is then always empty. ok=true
    /// means the returned set is exact, including genuinely empty coverage
    /// (null/empty geometry, or geometry entirely outside the raster
    /// footprint). Callers that train on the coverage MUST pass \a ok and
    /// fail closed when it is false; callers that omit \a ok still receive
    /// the collected set, which is empty on infrastructure failure
    /// (previously such failures went undetected — or, for a mid-scan
    /// RasterIO error, could even yield a corrupt partial set).
    static QSet<quint64> rasterize( const QgsGeometry &geom,
                                    const double gt[6],
                                    int width,
                                    int height,
                                    bool *ok = nullptr );

    /// R30-T01 (WP04): cancellable, bounded-window overload.
    ///
    /// \param isCanceled polled before each burn window, inside
    ///        GDALRasterizeGeometries (as the GDAL progress callback) and
    ///        every 64 scanline reads. When it returns true the burn stops,
    ///        every GDAL/OGR handle is closed, \a ok (if non-null) is set to
    ///        FALSE (a cancelled burn has no exact coverage — it must never
    ///        read as empty success) and \a cancelled (if non-null) is set
    ///        to true so callers can distinguish cancellation from
    ///        infrastructure failure.
    /// \param maxWindowPixels bounds the MEM band allocated for one burn
    ///        window. 0 keeps the single geometry-bbox window (legacy
    ///        footprint); a positive value splits the window into row chunks
    ///        of at most this many pixels. Chunks are disjoint and burned
    ///        output equals the single-window burn for every transform the
    ///        equivalence tests pin (north-up, rotated, sheared — GDAL
    ///        rasterizes per pixel-center); under an arbitrary affine GT a
    ///        center lying exactly on a polygon edge can differ by one
    ///        inverse-GT rounding between chunk and single-window inversion.
    static QSet<quint64> rasterize( const QgsGeometry &geom,
                                    const double gt[6],
                                    int width,
                                    int height,
                                    bool *ok,
                                    const std::function<bool()> &isCanceled,
                                    bool *cancelled = nullptr,
                                    qint64 maxWindowPixels = 0 );

    // -- Test-only failure injection (#1056 regression-test seam) ----------

    /// When non-null, invoked just before GDALRasterizeGeometries; returning
    /// true forces that call to be treated as failed. Process-global state —
    /// only for single-threaded tests, which must restore the previous value.
    static bool ( *sForceRasterizeGeometriesFailure )();
    /// When non-null, invoked before each scanline RasterIO( GF_Read);
    /// returning true forces that read to be treated as failed. Same
    /// process-global caveats as above.
    static bool ( *sForceRasterIOFailure )();
};
