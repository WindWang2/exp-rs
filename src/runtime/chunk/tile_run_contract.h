// tile_run_contract.h — the unified ChunkTask/TileRun contract (Execution
// Runtime Convergence 11.0, WP-B).
//
// One operator-independent description of a chunked invocation: partition
// identity (WHICH tiles exist, and any geometry change MUST change it),
// implementation/input identity (reusing the TileCheckpoint FNV-1a family),
// determinism grade, and the output artifact. Everything the resume driver
// (resumable_tile_run.h), the adoption kit (operators/framework/
// chunked_run.h) and the memory planner needs to reason about a tile run —
// WITHOUT importing operator, Qt or GDAL semantics into the Qt-free runtime.
//
// Contract laws (tested in tests/test_chunk_contract_11.cpp with independent
// FNV-1a oracle vectors):
//   L1 partition digest stability — same partition ⇒ same digest, forever.
//   L2 partition drift sensitivity — ANY geometry field change ⇒ different
//      digest (resume must refuse old tile states, never silently reuse).
//   L3 identity compatibility — operatorIdentity/inputIdentity semantics are
//      exactly TileCheckpoint's (same hash family, same fail-closed gates).
#pragma once

#include "tile_checkpoint.h"
#include "tile_spec.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

namespace sicnu::runtime::chunk
{

/// Determinism grade of a tile run. Mirrors the operator surface's
/// RSOperatorDeterminism one-to-one (the adoption bridge converts); kept as
/// its own enum so this header stays operator-free.
enum class TileRunDeterminism
{
    BitExact,
    Tolerance
};

/// Everything that defines WHICH tiles a run traverses. This is NOT just
/// geometry convenience: partitionDigest(partition) is part of the run
/// identity, so a tile grid change invalidates resume state by construction.
struct TileRunPartition
{
    int rasterWidth = 0;
    int rasterHeight = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int halo = 0;
    int bands = 1;
    int bandOffset = 0; ///< provenance: first source band carried by payloads
    int timeIndex = 0;  ///< provenance: temporal chunk index (0 = single step)

    // Overflow contract (#1056 discipline; same int index domain as
    // buildTileGrid, which refuses construction with std::length_error): the
    // partition arithmetic previously ran in plain int — a zero/negative tile
    // or raster dimension was a division-by-zero UB and dims near INT_MAX
    // wrapped. All math is int64 and typed-thrown when the result cannot be
    // represented; the typed checks run in EVERY build type.
    int tilesAcross() const
    {
        if ( tileWidth <= 0 || rasterWidth <= 0 )
            throw std::invalid_argument( "TileRunPartition: raster and tile dimensions must be "
                                         "positive (raster "
                                         + std::to_string( rasterWidth ) + " x tile "
                                         + std::to_string( tileWidth ) + ")" );
        const std::int64_t cols64 =
            ( static_cast<std::int64_t>( rasterWidth ) + tileWidth - 1 ) / tileWidth;
        if ( cols64 > std::numeric_limits<int>::max() )
            throw std::overflow_error( "TileRunPartition: tilesAcross " + std::to_string( cols64 )
                                       + " overflows the int tile index domain — shrink the tile "
                                         "size or bound the raster extent first" );
        return static_cast<int>( cols64 );
    }
    int tilesDown() const
    {
        if ( tileHeight <= 0 || rasterHeight <= 0 )
            throw std::invalid_argument( "TileRunPartition: raster and tile dimensions must be "
                                         "positive (raster "
                                         + std::to_string( rasterHeight ) + " x tile "
                                         + std::to_string( tileHeight ) + ")" );
        const std::int64_t rows64 =
            ( static_cast<std::int64_t>( rasterHeight ) + tileHeight - 1 ) / tileHeight;
        if ( rows64 > std::numeric_limits<int>::max() )
            throw std::overflow_error( "TileRunPartition: tilesDown " + std::to_string( rows64 )
                                       + " overflows the int tile index domain — shrink the tile "
                                         "size or bound the raster extent first" );
        return static_cast<int>( rows64 );
    }
    /// Total logical tiles (never materialized: O(1) arithmetic, safe for
    /// 10^6+ tile plans). Typed-thrown when the grid leaves the int tile
    /// index domain (same bound as buildTileGrid).
    std::uint64_t totalTiles() const
    {
        const std::int64_t cols64 = static_cast<std::int64_t>( tilesAcross() );
        const std::int64_t rows64 = static_cast<std::int64_t>( tilesDown() );
        const std::int64_t total64 = cols64 * rows64;
        if ( total64 > std::numeric_limits<int>::max() )
            throw std::overflow_error( "TileRunPartition: tile grid " + std::to_string( cols64 )
                                       + "x" + std::to_string( rows64 ) + " ("
                                       + std::to_string( total64 )
                                       + " tiles) overflows the int tile index domain — shrink "
                                         "the tile size or bound the raster extent first" );
        return static_cast<std::uint64_t>( total64 );
    }
};

/// FNV-1a 64 over the partition's canonical byte encoding (tagged 8-byte
/// little-endian fields, fixed order). Stable across releases: the encoding
/// IS the format — adding fields appends new tags at the end (old digests
/// stay comparable only for unchanged field sets).
std::uint64_t tileRunPartitionDigest( const TileRunPartition &partition );

/// Full identity of a tile run for resume/publication gates.
struct TileRunIdentity
{
    /// Implementation identity — same family/semantics as
    /// TileCheckpoint::operatorIdentity (implementation changed ⇒ refuse).
    std::uint64_t operatorIdentity = 0;
    /// Params + input identity — same family/semantics as
    /// TileCheckpoint::inputIdentity (drift ⇒ refuse).
    std::uint64_t inputIdentity = 0;
    /// Partition geometry digest (see TileRunPartition).
    std::uint64_t partitionDigest = 0;
};

/// Stable 3×16-hex key for run-scoped paths (scratch dirs, journals,
/// published markers). Round-trips through tileRunIdentityFromKey.
std::string tileRunIdentityKey( const TileRunIdentity &identity );
/// Inverse of tileRunIdentityKey; nullopt-shaped (all-zero) identity when the
/// string is malformed — callers gate on validity explicitly.
TileRunIdentity tileRunIdentityFromKey( const std::string &key );

/// Where a run's bytes go. Publication is .part → rename only (see
/// resumable_tile_run.h); finalPath is the artifact's committed location.
struct TileRunArtifact
{
    std::string finalPath;
    std::string kind; ///< informational ("raster", "table", "sidecar", ...)
};

/// The complete operator-independent description of one chunked invocation.
struct TileRunSpec
{
    TileRunIdentity identity;
    TileRunPartition partition;
    TileRunDeterminism determinism = TileRunDeterminism::BitExact;
    TileRunArtifact output;
};

/// O(1) tile lookup equivalent to buildTileGrid()[index] without
/// materializing the grid (million-tile plans stay arithmetic-only).
/// Throws std::invalid_argument when @p index is outside the partition and
/// std::overflow_error when the grid leaves the int domain (same bound as
/// TileRunPartition; construction-side buildTileGrid refuses the same domain
/// with std::length_error).
/// Equality with buildTileGrid is contract-tested in
/// tests/test_execution_scale_fault_11.cpp.
inline TileSpec tileSpecAt( const TileRunPartition &p, std::uint64_t index )
{
    const std::uint64_t total = p.totalTiles(); // typed: overflow-refusing
    if ( index >= total )
        throw std::invalid_argument( "tileSpecAt: tile index " + std::to_string( index )
                                     + " outside the partition range (0.."
                                     + std::to_string( total - 1 ) + ")" );
    const std::uint64_t across = static_cast<std::uint64_t>( p.tilesAcross() );
    TileSpec t;
    t.index = static_cast<int>( index );
    t.totalTiles = static_cast<int>( total );
    t.xOffset = static_cast<int>( ( index % across ) * static_cast<std::uint64_t>( p.tileWidth ) );
    t.yOffset = static_cast<int>( ( index / across ) * static_cast<std::uint64_t>( p.tileHeight ) );
    t.width = p.tileWidth < p.rasterWidth - t.xOffset ? p.tileWidth : p.rasterWidth - t.xOffset;
    t.height = p.tileHeight < p.rasterHeight - t.yOffset ? p.tileHeight : p.rasterHeight - t.yOffset;
    t.halo = p.halo;
    t.bufferWidth = t.width + 2 * p.halo;
    t.bufferHeight = t.height + 2 * p.halo;
    t.rasterWidth = p.rasterWidth;
    t.rasterHeight = p.rasterHeight;
    t.bands = p.bands;
    t.bandOffset = p.bandOffset;
    t.timeIndex = p.timeIndex;
    return t;
}

/// Cancellation source the runtime layer understands: an atomic flag (fast
/// path — JobEngine's per-job flag) OR a predicate (callback-based contexts).
/// Mirrors RSOperatorContext::isCancelled semantics (flag wins) without
/// depending on the operators layer.
struct TileRunCancelSource
{
    const std::atomic<bool> *flag = nullptr;
    std::function<bool()> predicate;

    bool cancelled() const
    {
        if ( flag && flag->load( std::memory_order_relaxed ) )
            return true;
        if ( predicate && predicate() )
            return true;
        return false;
    }
};

} // namespace sicnu::runtime::chunk
