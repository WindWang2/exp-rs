/***************************************************************************
  geospatial/fabric/chunk_plan.h
  Cloud-Native Data Fabric / Data Cube 10.0 — named-dimension chunk plans.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  A chunk plan turns a logical cube (a VirtualCube over scene assets, or a
  MultidimCubeDescriptor over one multidim store) into an ORDERED, BOUNDED
  enumeration of chunk requests over named dimensions (time / y / x / band
  for EO cubes; the descriptor's own dimension names for multidim stores).

  THE CONTRACT THAT MATTERS (DECISIONS D-1008): the plan carries a u64
  chunkCountTotal() that can name MILLIONS of logical chunks, and the only
  way to see them is materializeChunks(begin, maxCount) — a bounded window
  enumeration. There is no API that returns all chunks. Slicing (time
  range, spatial window, band selection) narrows the enumeration BEFORE
  counts are computed.

  Determinism: chunk index order is fixed (slowest dimension first, the
  order of dims()); the same plan input yields the same chunk at the same
  index, always. Estimated bytes come from declared facts (band dtype size
  × chunk cells; asset-known byte facts when present) — never from a
  network probe.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FABRIC_CHUNK_PLAN_H
#define SICNU_GEOSPATIAL_FABRIC_CHUNK_PLAN_H

#include "geospatial/common.h"
#include "geospatial/fabric/virtual_cube.h"
#include "geospatial/multidim/multidim_cube.h"

#include <json/json.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sicnu::geo
{

/// Chunk shape per named dimension (0 = use the cube default for that dim).
struct CubeChunkShape
{
    std::int64_t time = 1;
    std::int64_t y = 256;
    std::int64_t x = 256;
    std::int64_t band = 1;
    /// 11.0: per-name chunk override for EXTRA named dimensions (multidim
    /// stores). A dimension not named here keeps the whole-extent default.
    /// Non-positive values are refused (validated with the base fields).
    std::map<std::string, std::int64_t> perDimension;

    Json::Value toJson() const;
};

/// Slicing applied BEFORE chunking. Every field is optional ("" / false /
/// empty = that dimension is whole). The time bounds are UTC instants
/// (unparseable bounds are typed errors — no string comparisons).
struct CubeSlice
{
    std::string timeStartUtc;       ///< inclusive ("" = open)
    std::string timeEndUtc;         ///< exclusive ("" = open)
    bool hasSpatialSlice = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    /// Band selection by canonical role (empty = all bands of the slice).
    std::vector<std::string> bandRoles;
    /// Band selection by explicit 1-based source band index (empty = all).
    std::vector<int> bandIndices;
    /// 11.0: explicit [begin,end) range over an EXTRA named dimension
    /// (multidim stores — e.g. "level", "band"). Half-open, bounds-checked
    /// against the axis size at plan time; begin<0 / empty range refused.
    /// Time/y/x are addressed by their own fields, never by this map.
    std::map<std::string, std::pair<std::int64_t, std::int64_t>> dimensionRanges;

    void validate() const;          ///< throws GeoError(InvalidArgument)
};

/// One planned chunk (the bounded enumeration's element).
struct CubeChunkRequest
{
    std::uint64_t index = 0;                 ///< position in the total order
    std::vector<std::int64_t> chunkCoords;   ///< per dims() (chunk units)
    /// Per-dimension logical extent (post-slice, element units):
    /// time → the chunk's [begin,end) into the sliced instant list; y/x →
    /// grid pixel windows; band → [begin,end) into the sliced band list.
    std::vector<std::int64_t> dimOffsets;
    std::vector<std::int64_t> dimSizes;

    /// EO facts (virtual cubes): the time instant of the chunk's first
    /// time step ("" when the cube has no time dim) and the spatial extent
    /// in grid coordinates (valid when hasExtent).
    std::string timeUtc;
    bool hasExtent = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    std::uint64_t estimatedBytes = 0;        ///< declared-fact estimate
    /// Virtual cubes: the asset id whose selection-order slot covers this
    /// chunk's first time step (FirstWins policy) — a routing hint, not a
    /// guarantee (holes fall through to later assets at read time).
    std::string assetIdHint;

    Json::Value toJson() const;
};

/// One planned dimension.
struct CubeChunkDim
{
    std::string name;         ///< "time" | "y" | "x" | "band" | descriptor name
    std::int64_t size = 0;    ///< post-slice logical size (>= 0)
    std::int64_t chunk = 0;   ///< chunk size along this dim (>= 1)
    std::int64_t count = 0;   ///< ceil(size / chunk)

    Json::Value toJson() const;
};

class CubeChunkPlan
{
  friend class FabricPlan;   // plans embed a chunk plan (private ctor)
  public:
    /// Plans the EO cube (time/y/x/band). The virtual cube's selection
    /// order IS the time dimension (each asset = one time step). A temporal
    /// slice DROPS undated assets (absence is not evidence — they cannot
    /// prove membership); without a slice they keep their selection slot.
    /// Throws GeoError(InvalidArgument) for non-positive chunk extents and
    /// GeoError(ResourceExhausted) when the u64 count would overflow.
    static CubeChunkPlan forVirtualCube( const VirtualCube &cube, const CubeChunkShape &shape,
                                         const CubeSlice &slice = {} );

    /// Plans a multidim descriptor (its own dimension names; the time axis
    /// is the TEMPORAL-typed axis or an axis named "time"; the two trailing
    /// non-time dims map to y/x — a trailing time axis is a typed refusal,
    /// never a silently-zero plan). 11.0 slices: time (descriptor instants,
    /// bounded-capture axes refuse), spatial bbox (geotransform), and
    /// explicit dimensionRanges over any named axis (incl. "band");
    /// bandRoles/bandIndices stay EO-only (use dimensionRanges here).
    /// Non-default slices narrow counts BEFORE the u64 total is computed.
    static CubeChunkPlan forMultidimDescriptor( const MultidimCubeDescriptor &descriptor,
                                                const CubeChunkShape &shape,
                                                const CubeSlice &slice = {} );

    const std::vector<CubeChunkDim> &dims() const { return mDims; }
    /// Total logical chunks (u64 product; overflow refused at plan time).
    std::uint64_t chunkCountTotal() const { return mChunkCountTotal; }
    /// EO-cube plan (vs multidim descriptor plan).
    bool isEo() const { return mIsEo; }
    /// Whether a time slice actually narrowed the time dim (honest stats).
    bool timeSliced() const { return mTimeSliced; }
    bool spatialSliced() const { return mSpatialSliced; }
    bool bandSliced() const { return mBandSliced; }

    /// 11.0: the descriptor a MULTIDIM plan was built from (empty when EO).
    const MultidimCubeDescriptor &multidimDescriptor() const { return mMultidimDescriptor; }
    /// 11.0: per-named-dimension POST-SLICE index → SOURCE index mapping
    /// for sliced multidim dimensions (only sliced dims appear). Chunk
    /// dimOffsets address post-slice space; execution maps back to source
    /// axis indices through this (time uses the same mapping; y/x windows
    /// are contiguous so their offset maps directly).
    const std::map<std::string, std::vector<std::int64_t>> &multidimSelection() const
    {
      return mMultidimSelection;
    }

    /// The ONLY enumeration surface: chunks [begin, begin+maxCount) of the
    /// fixed total order. Throws GeoError(InvalidArgument) when begin ≥
    /// total; returns fewer than maxCount at the tail — never pads.
    std::vector<CubeChunkRequest> materializeChunks( std::uint64_t begin,
                                                     std::size_t maxCount ) const;

    Json::Value toJson() const;   ///< dims + counts + slice facts (no chunks)

  private:
    CubeChunkPlan() = default;
    std::vector<CubeChunkDim> mDims;
    std::uint64_t mChunkCountTotal = 0;
    bool mTimeSliced = false;
    bool mSpatialSliced = false;
    bool mBandSliced = false;
    // EO specifics (empty for multidim plans):
    std::vector<std::string> mInstants;          ///< per time step ("" undated),
                                                 ///< already post-slice
    std::vector<std::string> mAssetIdByTime;     ///< selection-order routing
    VirtualCubeGrid mGrid;                        ///< valid() only for EO plans
    bool mIsEo = false;
    double mBytesPerCell = 0.0;                   ///< dtype fact (0 unknown)
    // 11.0 multidim specifics (empty for EO plans):
    MultidimCubeDescriptor mMultidimDescriptor;   ///< the planned store
    /// post-slice index → source axis index per sliced dimension
    std::map<std::string, std::vector<std::int64_t>> mMultidimSelection;
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FABRIC_CHUNK_PLAN_H
