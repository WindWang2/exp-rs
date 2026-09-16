// terrain_hydrology.h — hydrology kernels extending the terrain family
// (track terrain-hydrology-11). Contracts follow terrain_flow.h.
//
// Contents:
//   * resolveFlats        — epsilon-gradient priority-flood flat resolution
//                           (A. DEM conditioning). Same NoData-barrier
//                           semantics as TerrainFlow::fillDepressions; the
//                           output is monotone non-decreasing vs the input
//                           and strictly drains every flat: along any
//                           cell's downstream path on the resolved surface
//                           the elevation is non-increasing in float steps
//                           of at least @c report.epsilon.
//   * flowDirectionInf    — D∞ (Tarboton 1997) flow direction as the
//                           steepest-descent angle over the 8 triangular
//                           facets. Encoding: degrees clockwise from north
//                           in [0, 360) (same convention as aspect);
//                           NoData passthrough; −1 = undecided (no
//                           downslope facet: pit / un-resolved flat).
//   * dInfAccumulation    — single-receiver accumulation over the D∞ graph
//                           (receiver = the base node of the steepest facet
//                           nearest the facet exit point; ties → the facet's
//                           first node). Self-inclusive counts, identical
//                           contract to TerrainFlow::flowAccumulation.
//                           Fraction-weighted two-receiver splitting is NOT
//                           implemented (recorded follow-up); the
//                           single-receiver graph keeps the mass identity
//                           exact (every valid cell has exactly one
//                           receiver).
//   * detectOutlets       — drainage outlets of a D8 graph: valid cells
//                           with direction 0 (interior sinks, rim spill
//                           cells, NoData-adjacent cells whose only descent
//                           is into NoData). Run on a flat-RESOLVED surface
//                           for true basin outlets; on a merely filled
//                           surface interior flats are reported (they are
//                           genuinely undefined drains there).
//   * streamNetwork       — threshold extraction + Strahler order on the
//                           D8 graph (C. watershed/stream). Streams are
//                           cells with accumulation >= threshold on a D8
//                           graph (self-inclusive counts: threshold 1
//                           selects every cell).
//   * streamSegments      — polyline vectorization of the stream network
//                           into Strahler links in CELL coordinates: each
//                           link runs from a head (no upstream stream cell)
//                           or from a junction (exclusive) downstream to
//                           the next junction (exclusive) or the network
//                           end (inclusive). Every stream cell belongs to
//                           exactly one link.
//
// Determinism: fixed neighbour/facet orders, strict-improvement comparisons
// (first maximum wins), and documented tie-breaks. Optional cancellation:
// kernels accept a cancelled() predicate polled between row batches; when it
// returns true the kernel returns false with contents unspecified.
#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace TerrainHydrology
{

// --- A. Flat resolution ---------------------------------------------------

struct FlatResolutionReport
{
    /// Elevation step applied by the epsilon gradient (scaled from the DEM's
    /// own relief: max(relief·2⁻²⁰, 1e-6) so increments stay float-exact).
    double epsilon = 0.0;
    /// Number of cells whose value was raised above the input DEM.
    long long raisedCells = 0;
};

/// Priority-flood fill WITH flat resolution. Behaves like
/// TerrainFlow::fillDepressions except that a cell popped at level L whose
/// neighbour is not higher is raised to L + epsilon, which removes every
/// interior flat: on the returned surface each valid INTERIOR cell has a
/// strictly descending path (in epsilon steps) to the drain boundary.
/// Boundary cells (raster rim, NoData-adjacent) ARE the drain — they keep
/// their elevation and may legitimately remain D8 sinks (rim outflows, see
/// detectOutlets). NoData cells are barriers (never filled, never routed
/// across); the drain boundary is the raster rim plus every valid cell
/// adjacent to NoData (same seeding as fillDepressions, #848). @a filled
/// may alias @a dem. When the DEM has no flats/depressions the output
/// equals the input.
bool resolveFlats( const float *dem, float *filled, int width, int height,
                   float nodata, FlatResolutionReport *report = nullptr,
                   const std::function<bool()> &cancelled = {} );

// --- B. D∞ flow -----------------------------------------------------------

/// D∞ flow directions (Tarboton 1997). @a angle values are degrees clockwise
/// from north in [0, 360); NoData cells carry @p nodata; cells without any
/// strictly lower valid neighbour (pits, flats on a merely-filled surface)
/// carry -1. Cells whose facet analysis is degenerate (the descent exits
/// through a facet vertex — cone axes / radial walls) fall back to the D8
/// steepest-descent neighbour azimuth so every non-pit cell drains and the
/// accumulation mass identity stays exact.
bool flowDirectionInf( const float *filled, float *angle, int width, int height,
                       float nodata, const std::function<bool()> &cancelled = {} );

/// Downstream receiver of the cell at (@p col, @p row) under the D∞ rule
/// (base node of the steepest facet nearest the facet exit point; ties → the
/// facet's first node). Exposed for stream/watershed tools; @p angles must
/// come from flowDirectionInf. Returns (@p col, @p row) for undecided cells
/// (angle <= -1 or @p nodata).
std::pair<int, int> dInfReceiver( const float *angles,
                                  int width, int height, float nodata,
                                  int col, int row );

/// Single-receiver accumulation over the D∞ graph (self-inclusive counts).
/// @p angles values <= -1 (undecided) are sinks; @p nodata values mark
/// NoData cells which carry @p nodata in @a acc. Cells whose receiver leaves
/// the grid are sinks. Kahn topological peel — same algorithm class as
/// TerrainFlow::flowAccumulation.
bool dInfAccumulation( const float *angles, float *acc, int width, int height,
                       float nodata );

// --- B/C. Outlets ---------------------------------------------------------

struct Outlet
{
    int col;
    int row;
    bool atRim; ///< on the raster perimeter or adjacent to a NoData cell
};

/// All valid cells with D8 direction 0 on @p filled (see terrain_flow.h for
/// the D8 contract), in row-major order. @p dir comes from
/// TerrainFlow::flowDirections over @p filled.
std::vector<Outlet> detectOutlets( const float *filled, const float *dir,
                                   int width, int height, float nodata );

// --- C. Stream network ----------------------------------------------------

struct StreamNetwork
{
    std::vector<std::uint8_t> isStream;    ///< 1 = accumulation >= threshold
    std::vector<std::uint8_t> strahler;    ///< Strahler order (0 outside network)
};

/// Threshold extraction + Strahler order over the D8 @p dir graph. @p acc is
/// the self-inclusive accumulation (TerrainFlow::flowAccumulation). @p filled
/// + @p nodata exclude DEM-NoData cells (same masking contract as the
/// accumulation overload). Orders follow Strahler (1957): heads = 1; a
/// downstream cell is max(inflow orders), +1 when two or more inflows attain
/// that maximum. Deterministic peel in fixed neighbour order.
bool streamNetwork( const float *dir, const float *acc, int width, int height,
                    const float *filled, float nodata, float threshold,
                    StreamNetwork *out );

struct StreamSegment
{
    int order;                                 ///< Strahler order of the link
    std::vector<std::pair<int, int>> cells;    ///< (col, row) polyline, upstream → downstream
};

/// Vectorize the network into Strahler links (see header contract above).
/// @p network must come from streamNetwork over the same @p dir graph.
/// Links are returned in deterministic discovery order (row-major start
/// cells, fixed neighbour order). Returns false when @p network is
/// inconsistent with @p dir (never expected for paired outputs).
bool streamSegments( const float *dir, int width, int height, float nodata,
                     const StreamNetwork &network,
                     std::vector<StreamSegment> *out );

} // namespace TerrainHydrology
