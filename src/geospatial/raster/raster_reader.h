/***************************************************************************
  geospatial/raster/raster_reader.h
  Geospatial I/O Foundation 4.0 — streaming raster read contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
  * open() captures canonical metadata in one read-only open.
  * Window reads are the only bulk access mode; whole-raster reads are an
    explicit readFull() guarded by a byte budget (no accidental full loads).
  * Returned values are STORED values: NoData/scale/offset are reported by
    metadata and applied only through explicit helpers — never implicitly.
  * Window arguments are validated strictly; clamping is an explicit helper.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_RASTER_READER_H
#define SICNU_GEOSPATIAL_RASTER_READER_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// Pixel window in pixel coordinates (x/y offset, x/y size). Values are
/// non-negative; xOff+width must not exceed the raster extent.
struct RasterWindow
{
    int xOff = 0;
    int yOff = 0;
    int width = 0;
    int height = 0;
};

inline bool operator==( const RasterWindow &a, const RasterWindow &b )
{
  return a.xOff == b.xOff && a.yOff == b.yOff && a.width == b.width && a.height == b.height;
}

/// Restricts a window to the raster extent. Returns false when the window does
/// not intersect the raster at all (out of bounds on any side).
bool clampWindowToRaster( const RasterMetadata &metadata, RasterWindow &window );

/// Overview selection policy (5.0). Algorithms default to `Exact` — a
/// silently-sampled overview is a wrong-answer factory. Preview/UI surfaces
/// may opt into `Nearest` explicitly; `Auto` defers to the driver.
enum class OverviewPolicy
{
  Exact,   ///< ignore overviews; read at native resolution (default)
  Nearest, ///< smallest overview whose dimensions still cover the request
  Auto,    ///< driver-chosen level for the request (GDAL default behavior)
};

/// One tile of a caller-planned tile walk (absolute pixel coordinates,
/// clamped at the requested window's edges).
struct TileSlice
{
  int xOff = 0;              ///< absolute pixel column of the tile origin
  int yOff = 0;              ///< absolute pixel row of the tile origin
  int width = 0;
  int height = 0;
  int tileX = 0;             ///< tile column index in the plan
  int tileY = 0;             ///< tile row index in the plan
};

/// A planned tile walk: `tilesX × tilesY` bounded slices over a window.
struct TilePlan
{
  int tilesX = 0;
  int tilesY = 0;
  int tileWidth = 0;
  int tileHeight = 0;
  RasterWindow window; ///< the covered window (already clamped to the raster)

  std::size_t tileCount() const
  {
    return static_cast<std::size_t>( tilesX ) * static_cast<std::size_t>( tilesY );
  }
  TileSlice slice( int tileX, int tileY ) const;
};

/// Plans a bounded tile walk over `window` (pass a full-extent window for a
/// whole-raster walk). Tile sizes must be positive; the last column/row is
/// clamped. Throws GeoError(InvalidArgument) on bad input; an empty window
/// intersection throws rather than planning a zero-tile walk.
TilePlan planTileWalk( const RasterMetadata &metadata, const RasterWindow &window,
                       int tileWidth, int tileHeight );

class RasterReader
{
  public:
    /// Opens read-only and captures canonical metadata (no pixel access).
    /// Throws GeoError(OpenFailed/InvalidArgument).
    static RasterReader open( const std::string &path );

    RasterReader() = default;
    ~RasterReader();
    RasterReader( const RasterReader & ) = delete;
    RasterReader &operator=( const RasterReader & ) = delete;
    RasterReader( RasterReader &&other ) noexcept;
    RasterReader &operator=( RasterReader &&other ) noexcept;

    bool isOpen() const { return mHandle != nullptr; }
    void close();
    const RasterMetadata &metadata() const { return mMetadata; }

    /// Window size sanity: returns false and a message when the window does
    /// not lie inside the raster or has degenerate size.
    static bool validateWindow( const RasterMetadata &metadata, const RasterWindow &window, std::string *error );

    /// Memory footprint (bytes) of a multi-band window read into double
    /// buffers — the budget unit for readFull() and caller planning.
    static std::size_t windowByteBudget( const RasterMetadata &metadata, const RasterWindow &window,
                                         const std::vector<int> &bands );

    /// Reads stored pixel values (as doubles; integer types are exact) for
    /// the given 1-based bands. Output layout is band-sequential:
    /// [band0(w*h), band1(w*h), ...]. Throws GeoError(InvalidArgument) on a
    /// bad window/band list, GeoError(Unsupported) for complex pixel types,
    /// and GeoError(Unsupported) when the read exceeds the default window
    /// byte budget (#808 — an oversized request is a typed error, never an
    /// uncaught bad_alloc). Callers wanting larger reads must stream.
    std::vector<double> readWindow( const std::vector<int> &bands, const RasterWindow &window ) const;

    /// Window read with an explicit byte budget (#808). Exceeding the budget
    /// throws GeoError(Unsupported) with the measured size in details — the
    /// same contract as readFull; fall back to smaller windows / streaming.
    std::vector<double> readWindow( const std::vector<int> &bands, const RasterWindow &window,
                                    std::size_t maxBytes ) const;

    /// Default window byte budget (1 GiB of doubles) enforced by the plain
    /// readWindow/readBlock/iterateTiles entry points.
    static constexpr std::size_t kDefaultWindowBudgetBytes = 1024ULL * 1024ULL * 1024ULL;

    /// Whole-raster read with an explicit byte budget. Exceeding the budget
    /// throws GeoError(Unsupported) with the measured size in details —
    /// callers are expected to fall back to windowed streaming.
    std::vector<double> readFull( const std::vector<int> &bands, std::size_t maxBytes ) const;

    /// Validity mask for the window: 255 where the pixel is a valid value,
    /// 0 where it matches the band's declared NoData (or is NaN for float
    /// bands without declared NoData... a NaN pixel is invalid only when the
    /// band declares NaN as NoData — undeclared NaNs are data). Bands with
    /// no NoData produce an all-valid mask. Multi-band masks AND-combine.
    std::vector<std::uint8_t> readMask( const RasterWindow &window, const std::vector<int> &bands ) const;

    /// Physical values for a stored value: value * scale + offset (declared
    /// or default 1/0). Pure arithmetic helper — keeps "apply" explicit.
    static double applyScaleOffset( const BandInfo &band, double storedValue );

    /// GDAL band handle for advanced/expert access (overviews, RAT, ...).
    /// The reader remains the owner. Null when closed or index out of range.
    void *bandHandle( int bandIndex1Based ) const;

    // --- 5.0: block / tile / overview contracts ---------------------------

    /// Native block size of a band {width, height}. {0,0} for a closed reader
    /// or out-of-range band (never throws — callers plan with it).
    std::pair<int, int> blockSize( int bandIndex1Based ) const;

    /// Reads one native block of one band (block coordinates, not pixels).
    /// Stored values, band-sequential layout of exactly blockSize() elements
    /// EVEN at raster edges (#790: edge blocks are padded with the band's
    /// declared NoData — 0.0 when none declared — so callers indexing by
    /// blockSize() can never read out of bounds on a truncated buffer).
    /// Throws GeoError(InvalidArgument) for out-of-range arguments and
    /// GeoError(Unsupported) when the native block exceeds the default
    /// window byte budget (#808 — degenerate block geometries only).
    std::vector<double> readBlock( int bandIndex1Based, int blockX, int blockY ) const;

    /// Walks `plan` tile by tile, handing each bounded slice's stored values
    /// (band-sequential) to `sink`. Never materializes more than one tile ×
    /// bands at a time — that is the bounded-memory point of the walk.
    /// `cancelled` is polled before each tile; returning true stops the walk
    /// with GeoError(Cancelled) (tiles already delivered stay delivered).
    void iterateTiles( const TilePlan &plan, const std::vector<int> &bands,
                       const std::function<void( const TileSlice &, const std::vector<double> & )> &sink,
                       const std::function<bool()> &cancelled = {} ) const;

    /// Overview introspection for the band: count, and per-level dimensions
    /// (flattened {w0,h0,w1,h1,...} at native level, index = level-1).
    int overviewCount( int bandIndex1Based ) const;
    std::vector<int> overviewDimensions( int bandIndex1Based ) const;

    /// Chooses an overview level under `policy` for a target reading size.
    /// Returns 0 (= native) for Exact; the smallest level whose dimensions
    /// still cover (targetWidth,targetHeight) for Nearest; the driver-decided
    /// level for Auto (GDAL picks during IO). Throws nothing; level is a
    /// 1-based overview index, 0 when none selected / none exist.
    int selectOverview( int bandIndex1Based, int targetWidth, int targetHeight,
                        OverviewPolicy policy ) const;

    /// Explicitly downsampled window read. This is the ONLY resampling entry
    /// point: dstWidth/dstHeight are the caller's choice, `level` is the
    /// overview to read from (0 = native), `method` is the resample kernel.
    /// Nothing about this call is implicit — the returned buffer is exactly
    /// dstWidth × dstHeight per band, and a missing overview level with
    /// policy-exact expectations is a structured error, never a silent native
    /// read. Stored values; GeoError(InvalidArgument) on bad geometry.
    std::vector<double> readWindowResampled( const std::vector<int> &bands, const RasterWindow &window,
                                             int dstWidth, int dstHeight, int overviewLevel,
                                             OverviewPolicy policy, const std::string &resampling ) const;

  private:
    void *mHandle = nullptr; // GDALDatasetH, kept void* to contain gdal headers
    RasterMetadata mMetadata;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_RASTER_READER_H
