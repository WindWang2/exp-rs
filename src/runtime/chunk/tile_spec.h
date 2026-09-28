// tile_spec.h — Tile geometry for the chunk execution graph (Data Plane 3.0).
//
// Pure value types: Qt-free, noexcept, safe to move across threads. The
// geometry mirrors GdalBlockStream::Tile so GDAL-backed producers and
// GdalBlockStream share one mental model, but nothing here depends on GDAL.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace sicnu::runtime::chunk
{

/// Geometry of one tile in a tiled raster traversal.
struct TileSpec
{
    int index = 0;         ///< 0-based tile index in row-major visit order
    int totalTiles = 0;    ///< total tile count of the grid
    int xOffset = 0;       ///< pixel column of the tile's left edge (0-based)
    int yOffset = 0;       ///< pixel row of the tile's top edge (0-based)
    int width = 0;         ///< tile width in pixels (<= tileWidth, edge-clamped)
    int height = 0;        ///< tile height in pixels (<= tileHeight, edge-clamped)
    int halo = 0;          ///< halo / margin radius in pixels (0 if none)
    int bufferWidth = 0;   ///< pixel buffer width (width + 2*halo)
    int bufferHeight = 0;  ///< pixel buffer height (height + 2*halo)
    int rasterWidth = 0;   ///< full raster width (fill positions outside it are
                           ///< edge-replicated; kernels must clamp window rects)
    int rasterHeight = 0;
    int bands = 1;         ///< band count carried by the tile payload
    /// Band subset (LSEE 10.0): the payload's first band within the SOURCE
    /// raster. 0 = payload starts at band 1 (the historical reading). Pure
    /// provenance for consumers/planners — the buffer still carries exactly
    /// @p bands interleaved bands.
    int bandOffset = 0;
    /// Time chunk (LSEE 10.0): 0-based index of the payload within a temporal
    /// stack (one chunk per acquisition). 0 = the single/historical time step.
    int timeIndex = 0;

    /// Buffer element count (interleaved bands, band-major within a pixel is
    /// the producer's choice; see the pipeline contract).
    size_t bufferElementCount() const
    {
        return static_cast<size_t>( bufferWidth ) * static_cast<size_t>( bufferHeight )
               * static_cast<size_t>( bands );
    }
    /// Number of valid (non-halo) pixel elements: width*height*bands.
    size_t coreElementCount() const
    {
        return static_cast<size_t>( width ) * static_cast<size_t>( height )
               * static_cast<size_t>( bands );
    }
};

/// Row-major tile grid for a raster with optional halo.
///
/// Overflow contract (#1056): the grid arithmetic previously relied on
/// assert-only preconditions — compiled out of release builds, so dims near
/// INT_MAX (e.g. GDAL-reported sizes flowing through the fused-chain seam)
/// wrapped `rasterWidth + tileWidth - 1` and `cols * rows` into negative
/// ints. All grid math is now computed in int64 and typed-thrown when the
/// result cannot be represented. The typed checks fully supersede the old
/// asserts (they run in EVERY build type and throw instead of aborting — an
/// assert here would preempt the typed path in debug builds).
inline std::vector<TileSpec> buildTileGrid( int rasterWidth, int rasterHeight,
                                            int tileWidth, int tileHeight,
                                            int halo, int bands )
{
    if ( rasterWidth <= 0 || rasterHeight <= 0 )
        throw std::invalid_argument( "buildTileGrid: raster dimensions must be positive (" +
                                     std::to_string( rasterWidth ) + "x" +
                                     std::to_string( rasterHeight ) + ")" );
    if ( tileWidth <= 0 || tileHeight <= 0 )
        throw std::invalid_argument( "buildTileGrid: tile dimensions must be positive (" +
                                     std::to_string( tileWidth ) + "x" +
                                     std::to_string( tileHeight ) + ")" );
    if ( halo < 0 )
        throw std::invalid_argument( "buildTileGrid: halo must be >= 0 (got " +
                                     std::to_string( halo ) + ")" );
    if ( bands <= 0 )
        throw std::invalid_argument( "buildTileGrid: bands must be positive (got " +
                                     std::to_string( bands ) + ")" );

    const std::int64_t cols64 =
        ( static_cast<std::int64_t>( rasterWidth ) + tileWidth - 1 ) / tileWidth;
    const std::int64_t rows64 =
        ( static_cast<std::int64_t>( rasterHeight ) + tileHeight - 1 ) / tileHeight;
    const std::int64_t total64 = cols64 * rows64;
    if ( cols64 > std::numeric_limits<int>::max() || rows64 > std::numeric_limits<int>::max() ||
         total64 > std::numeric_limits<int>::max() )
        throw std::overflow_error( "buildTileGrid: tile grid " + std::to_string( cols64 ) + "x" +
                                   std::to_string( rows64 ) + " (" + std::to_string( total64 ) +
                                   " tiles) overflows the int tile index domain — shrink the "
                                   "tile size or bound the raster extent first" );
    const int cols = static_cast<int>( cols64 );
    const int rows = static_cast<int>( rows64 );
    const int total = static_cast<int>( total64 );
    // Both buffer spans are computed in int64: the widest/tallest tile plus
    // the halo must stay inside int, or TileSpec::bufferWidth/bufferHeight
    // would wrap negative for downstream buffer sizing.
    if ( const std::int64_t bufferSpanW =
             static_cast<std::int64_t>( std::min( tileWidth, rasterWidth ) ) +
             2 * static_cast<std::int64_t>( halo );
         bufferSpanW > std::numeric_limits<int>::max() )
        throw std::overflow_error( "buildTileGrid: tile buffer width + 2*halo = " +
                                   std::to_string( bufferSpanW ) + " overflows int — reduce the halo" );
    if ( const std::int64_t bufferSpanH =
             static_cast<std::int64_t>( std::min( tileHeight, rasterHeight ) ) +
             2 * static_cast<std::int64_t>( halo );
         bufferSpanH > std::numeric_limits<int>::max() )
        throw std::overflow_error( "buildTileGrid: tile buffer height + 2*halo = " +
                                   std::to_string( bufferSpanH ) + " overflows int — reduce the halo" );

    std::vector<TileSpec> tiles;
    tiles.reserve( static_cast<size_t>( total ) );
    int index = 0;
    for ( int row = 0; row < rows; ++row )
    {
        for ( int col = 0; col < cols; ++col )
        {
            TileSpec t;
            t.index = index++;
            t.totalTiles = total;
            t.xOffset = col * tileWidth;
            t.yOffset = row * tileHeight;
            t.width = std::min( tileWidth, rasterWidth - t.xOffset );
            t.height = std::min( tileHeight, rasterHeight - t.yOffset );
            t.halo = halo;
            t.bufferWidth = t.width + 2 * halo;
            t.bufferHeight = t.height + 2 * halo;
            t.rasterWidth = rasterWidth;
            t.rasterHeight = rasterHeight;
            t.bands = bands;
            tiles.push_back( t );
        }
    }
    return tiles;
}

} // namespace sicnu::runtime::chunk
