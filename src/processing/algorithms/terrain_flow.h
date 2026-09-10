// terrain_flow.h — depression filling and D8 flow routing
// (Foundation 5.0, Milestone F).
//
// Contracts:
//   * fillDepressions: priority-flood (Barnes et al. 2014). Every non-NoData
//     cell is raised to the minimum level at which it drains to the raster
//     boundary; NoData cells are barriers — they are neither filled nor
//     routed across (a cell draining into NoData is a sink).
//   * flowDirections: D8 steepest descent over the FILLED surface (pass the
//     fillDepressions output). Direction codes are the ESRI powers-of-two
//     (E=1, SE=2, S=4, SW=8, W=16, NW=32, N=64, NE=64*2=128 — see the table
//     below), 0 = sink/undefined (no lower neighbour, flat, or NoData).
//     Steepness compares (z − z_nb)/distance with distance in cells
//     (1 orthogonal, sqrt(2) diagonal); equal steepness resolves to the
//     first neighbour in the fixed E,SE,S,SW,W,NW,N,NE order (deterministic).
//   * flowAccumulation: number of cells draining through each cell,
//     INCLUDING the cell itself (ridge cells = 1). Computed by Kahn's
//     topological peel over the direction forest — acyclic by construction
//     because D8 routes strictly downhill on a filled surface (filled flats
//     are sinks with direction 0).
//
// All three are full-frame kernels (O(N log N) fill, O(N) routing): the DEM
// frames follow the terrain family's established memory contract.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace TerrainFlow
{

/// Priority-flood depression filling. @a dem and @a filled are
/// @p width × @p height floats; @a filled may alias @a dem.
bool fillDepressions( const float *dem, float *filled, int width, int height, float nodata );

/// D8 flow directions over the filled surface (see the code table above).
bool flowDirections( const float *filled, float *dir, int width, int height, float nodata );

/// Drainage accumulation from the D8 directions (self-inclusive counts).
/// Overload with the filled DEM + nodata sentinel (#783): cells that are
/// NoData on the DEM are excluded from the routing graph entirely and carry
/// @p nodata in @a acc (they previously seeded a spurious 1.0 ridge along
/// every masked/ocean boundary). The 3-arg form keeps accumulating over
/// every cell — for callers that already routed with a clean (nodata-free)
/// surface.
bool flowAccumulation( const float *dir, float *acc, int width, int height );
bool flowAccumulation( const float *dir, float *acc, int width, int height,
                       const float *filled, float nodata );

/// Watershed delineation on the D8 graph: labels every cell whose flow path
/// reaches one of @a pourPoints (zero-based (col,row) pairs). Cells draining
/// to pour point k receive label k+1; cells on no path to any pour point
/// (including direction-0 sinks that are not pour points) receive 0. A pour
/// point always carries its own label. Deterministic: the upstream BFS is
/// seeded in pour-point order and a reachable-from-both cell keeps the label
/// of the FIRST pour point (documented tie-break — a cell with two
/// downstream paths drains to exactly one under D8, so ambiguity requires
/// duplicate pour points or ties between separate sinks).
bool watershedLabels( const float *dir, int width, int height,
                      const std::vector<std::pair<int, int>> &pourPoints,
                      std::vector<float> *labels );

} // namespace TerrainFlow
