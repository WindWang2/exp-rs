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
    Json::Value toJson() const;
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

  private:
    void *mHandle = nullptr;   // GDALDatasetH
    MultidimMetadata mMetadata;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_MULTIDIM_VIEW_H
