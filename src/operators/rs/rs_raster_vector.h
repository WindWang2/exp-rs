/***************************************************************************
 * rs_raster_vector.h — shared streaming raster↔vector seam (8.0, package D)
 *
 * GUI-free foundation for the rs:rasterize and rs:zonal_stats operators:
 * one pixel-membership implementation (GDALRasterizeGeometries into bounded
 * MEM windows) so burn semantics cannot drift between the two operators,
 * and one bounded feature cache over the geospatial VectorReader contract
 * (features stream in batches; geometry arrives in the raster's CRS through
 * the reader's declared target-CRS transform).
 *
 * Memory contracts:
 *   * the feature cache is byte-budgeted (kFeatureCacheBytes) — oversized
 *     vectors are a typed refusal, never an unbounded buffer;
 *   * every rasterization happens inside a ≤ kWindowDim² MEM window;
 *     geometry windows over the output grid are tiled by the callers.
 ***************************************************************************/
#pragma once

#include <QString>

#include <json/json.h>

#include <gdal.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{
class Crs;
}

namespace sicnu::operators::rs {

/// Byte budget for the cached feature set (geometry handles + metadata, not
/// pixels). Overshooting it is a typed refusal — a vector that large needs
/// a spatial subset, and silently materializing it would be the exact
/// unbounded-buffer failure mode the geospatial contracts forbid.
constexpr size_t kFeatureCacheBytes = 256ull * 1024ull * 1024ull;

/// One cached vector feature, already transformed into the raster grid's
/// CRS (the VectorReader owns the transform; this struct only carries the
/// result) with its pixel-space bounds precomputed.
struct CachedFeature
{
    std::int64_t fid = -1;
    std::string zoneKey;               ///< FID or attribute value, string form
    OGRGeometryH geometry = nullptr;   ///< owned, in the raster CRS; null when geometryless
    double burnValue = 0.0;            ///< constant burn or numeric attribute value
    // Pixel-space bounds (minCol, minRow, maxCol, maxRow) of the geometry's
    // georeferenced envelope against the grid; intersects == false when the
    // envelope misses the grid entirely.
    bool intersects = false;
    std::array<double, 4> bounds{ 0.0, 0.0, 0.0, 0.0 };
};

/// Cached, ordered feature set (feature order = burn precedence, last wins).
/// Owns the parsed geometry handles.
struct FeatureCache
{
    std::vector<CachedFeature> features;
    size_t bytes = 0;                  ///< approximate geometry bytes held
    std::uint64_t geometryless = 0;    ///< features skipped (no geometry)
    std::uint64_t outsideGrid = 0;     ///< features whose envelope misses the grid

    FeatureCache() = default;
    FeatureCache( const FeatureCache & ) = delete;
    FeatureCache &operator=( const FeatureCache & ) = delete;
    FeatureCache( FeatureCache && ) = default;
    ~FeatureCache();
};

/// Loading contract for loadFeatureCache: which attribute carries the burn
/// value (empty = constant @a constantValue) and which carries the zone key
/// (empty = FID).
struct FeatureLoadSpec
{
    QString vectorPath;
    QString layerSelector;   ///< "" = first layer
    QString valueField;      ///< "" = constant burn
    QString zoneField;       ///< "" = FID keys
    double constantValue = 1.0;
};

/// Streams the vector through the geospatial VectorReader (declared CRS
/// transform into @a targetCrs — the raster grid's CRS), parses geometries,
/// and caches them with pixel bounds. Throws RSOperatorError on open/CRS
/// failures, malformed WKT, a missing/non-numeric field value, or a cache
/// over kFeatureCacheBytes.
FeatureCache loadFeatureCache( const FeatureLoadSpec &spec, const RasterGrid &grid,
                               const sicnu::geo::Crs &targetCrs );


/// North-up affine grid (the terrain/raster-vector family contract; rotated
/// geotransforms are refused by the operators before this seam runs).
struct RasterGrid
{
    int width = 0;
    int height = 0;
    std::array<double, 6> gt{ 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };

    /// Georeferenced envelope of a pixel-space rectangle (window incl.).
    std::array<double, 4> windowGeoBounds( int xOff, int yOff, int winW, int winH ) const;
};

/// Pixel-space bounds of a georeferenced envelope against the grid.
/// Returns false when the envelope misses the grid entirely.
bool boundsFromGeo( const RasterGrid &grid, double minX, double minY, double maxX, double maxY,
                    std::array<double, 4> *out );

/// Rasterizes @a geometries (georeferenced, raster CRS) into a freshly
/// NaN-initialized @a winW×@a winH float window whose top-left corner is at
/// georeferenced @a originX/@a originY. @a burnValues has one entry per
/// geometry (last wins on overlap). @a allTouched selects GDAL's
/// ALL_TOUCHED pixel selection; pixel-center selection otherwise.
/// Returns false on GDAL failure (callers surface a typed error).
bool rasterizeWindow( GDALDatasetH memDataset, int band,
                      const std::vector<OGRGeometryH> &geometries,
                      const std::vector<double> &burnValues,
                      double originX, double originY,
                      double pixelSizeX, double pixelSizeY,
                      bool allTouched );

/// Creates a NaN-initialized MEM raster window (owned; GDALClose to free).
GDALDatasetH createMemWindow( int winW, int winH );

/// Fills a float window with @a value in place.
void fillWindow( GDALDatasetH dataset, int band, int winW, int winH, float value );

/// Reads a MEM window band back into @a out (winW×winH floats).
bool readMemWindow( GDALDatasetH dataset, int band, int winW, int winH, float *out );

/// Parses WKT (raster CRS) into an OGR geometry (owned; OGR_G_DestroyGeometry).
/// Returns null on malformed WKT — callers refuse with a typed error.
OGRGeometryH geometryFromWkt( const std::string &wkt );

/// Georeferenced envelope of an OGR geometry.
void geometryEnvelope( OGRGeometryH geometry, double *minX, double *minY,
                       double *maxX, double *maxY );

/// Formats a double burn value deterministically for CSV/JSON output.
std::string formatDouble( double v );

} // namespace sicnu::operators::rs
