/***************************************************************************
  geospatial/multidim/multidim_view.h
  Geospatial I/O Foundation 4.0 — lazy multidimensional access contract.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
  * variable / time / level selection is LAZY: listing never reads array data
  * slices address dimensions BY NAME with an index; a slice that names an
    unknown dimension or variable is a structured error (coordinate-value
    matching on the indexing variable is a documented follow-up, not
    implemented — indices only)
  * a slice always yields an explicit (rows × cols) grid — the layer never
    flattens extra dimensions into pseudo-bands (that is a fidelity violation)
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_MULTIDIM_VIEW_H
#define SICNU_GEOSPATIAL_MULTIDIM_VIEW_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::geo
{

struct MultidimGrid
{
    std::size_t rows = 0;
    std::size_t cols = 0;
    std::vector<double> values;   ///< stored values, row-major (scale/offset NOT applied)
    bool hasNoData = false;
    double noDataValue = 0.0;
    bool noDataIsNaN = false;

    // 7.0 — missing-value accounting over the returned grid: cells equal to
    // the declared NoData (or NaN when NoData is NaN) are counted, never
    // silently rewritten — masking stays the caller's explicit decision.
    std::size_t missingCount = 0;

    Json::Value toJson() const;
};

/// How a requested coordinate value is matched against a captured axis.
enum class CoordinateMatch
{
  Exact,    ///< the axis must contain the value exactly (an absent value is
            ///< a typed error, never a guess)
  Nearest   ///< the closest axis value wins; `tolerance` bounds the accepted
            ///< distance (a miss beyond tolerance is a typed error)
};

const char *coordinateMatchName( CoordinateMatch match );
CoordinateMatch coordinateMatchFromName( const std::string &name ); ///< throws

/// The result of resolving a coordinate value against a captured axis.
struct CoordinateSliceMatch
{
    std::int64_t index = -1;      ///< resolved axis index (>= 0 on success)
    double resolvedValue = 0.0;   ///< the axis value at the resolved index
    double distance = 0.0;        ///< |requested - resolved| (0 for exact)
    bool exact = false;           ///< true when the match was exact
};

class MultidimView
{
  public:
    /// Opens the multidimensional view (root group). Throws
    /// GeoError(OpenFailed/Unsupported) when the store has no MDArray API.
    static MultidimView open( const std::string &path );

    MultidimView() = default;
    ~MultidimView();
    MultidimView( const MultidimView & ) = delete;
    MultidimView &operator=( const MultidimView & ) = delete;
    MultidimView( MultidimView &&other ) noexcept;
    MultidimView &operator=( MultidimView &&other ) noexcept;

    bool isOpen() const { return mHandle != nullptr; }
    void close();
    const MultidimMetadata &metadata() const { return mMetadata; }

    /// Bounded window over a variable: every non-spatial dimension must be
    /// fixed by `dimSlices` (dimension name → index) so exactly two free
    /// dimensions remain (rows × cols). Unknown names → GeoError.
    MultidimGrid readSlice( const std::string &variable,
                            const std::vector<std::pair<std::string, std::int64_t>> &dimSlices,
                            std::size_t maxCells = 64ull * 1024 * 1024 );

    /// Convenience: fix every non-spatial dimension to index 0 except the
    /// named one (e.g. slice only time). Extra dims beyond one are an error.
    MultidimGrid readTemporalOrLevelSlice( const std::string &variable,
                                           const std::string &dimensionName,
                                           std::int64_t index,
                                           std::size_t maxCells = 64ull * 1024 * 1024 );

    // --- 7.0: coordinate-aware slicing, spatial windows, missing values --

    /// Resolves a coordinate VALUE against the captured axis of `dimension`
    /// (time steps, depths, ...). Requires a captured numeric axis; Exact
    /// demands presence, Nearest bounds |distance| by `tolerance`. The
    /// result names an INDEX — the actual read still goes through readSlice
    /// (one read path, no duplicated slicing machinery).
    CoordinateSliceMatch resolveCoordinateIndex( const std::string &dimensionName, double value,
                                                 CoordinateMatch matchMode, double tolerance = 0.0 ) const;

    /// 8.0: resolves a STRING axis label (CF datetime strings, categorical
    /// labels) against the captured string axis of `dimension`. An exact
    /// label match wins; an offset-normalized EQUAL instant matches when the
    /// label and axis entry parse as ISO-8601 (mixed offsets select
    /// correctly). Everything else is a typed miss — never a nearest guess.
    /// The result names an INDEX — the actual read still goes through
    /// readSlice.
    CoordinateSliceMatch resolveCoordinateIndexByString( const std::string &dimensionName,
                                                         const std::string &value ) const;

    /// Coordinate-value slice: `dimValues` name non-spatial dimensions and
    /// the coordinate values to fix them at (resolved through
    /// resolveCoordinateIndex); exactly the two trailing spatial dimensions
    /// stay free. Same contract as readSlice otherwise.
    MultidimGrid readSliceByCoordinateValues(
      const std::string &variable,
      const std::vector<std::pair<std::string, double>> &dimValues,
      CoordinateMatch matchMode, double tolerance = 0.0,
      std::size_t maxCells = 64ull * 1024 * 1024 );

    /// Bounded spatial sub-window of a slice: dimSlices fix every
    /// non-spatial dimension; (rowOff, colOff) and (rows, cols) address the
    /// two free (trailing) dimensions. Reads touch only the requested
    /// window — chunked storages (Zarr, chunked NetCDF/HDF5) are read at
    /// their chunk granularity by GDAL, and the cell budget bounds the
    /// materialized result. Chunk shapes are reported per variable in
    /// MultidimMetadata (VariableInfo::blockShape) for read planning.
    MultidimGrid readSliceWindow( const std::string &variable,
                                  const std::vector<std::pair<std::string, std::int64_t>> &dimSlices,
                                  std::int64_t rowOff, std::int64_t colOff,
                                  std::size_t rows, std::size_t cols,
                                  std::size_t maxCells = 64ull * 1024 * 1024 );

  private:
    void *mHandle = nullptr;   // GDALDatasetH
    MultidimMetadata mMetadata;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_MULTIDIM_VIEW_H
