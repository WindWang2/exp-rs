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
    /// bad window/band list, GeoError(Unsupported) for complex pixel types.
    std::vector<double> readWindow( const std::vector<int> &bands, const RasterWindow &window ) const;

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

  private:
    void *mHandle = nullptr; // GDALDatasetH, kept void* to contain gdal headers
    RasterMetadata mMetadata;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_RASTER_READER_H
