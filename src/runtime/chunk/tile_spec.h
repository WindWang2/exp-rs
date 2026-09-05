// tile_spec.h — Tile geometry for the chunk execution graph (Data Plane 3.0).
//
// Pure value types: Qt-free, noexcept, safe to move across threads. The
// geometry mirrors GdalBlockStream::Tile so GDAL-backed producers and
// GdalBlockStream share one mental model, but nothing here depends on GDAL.
#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
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
inline std::vector<TileSpec> buildTileGrid( int rasterWidth, int rasterHeight,
                                            int tileWidth, int tileHeight,
                                            int halo, int bands )
{
    assert( rasterWidth > 0 && rasterHeight > 0 );
    assert( tileWidth > 0 && tileHeight > 0 );
    assert( halo >= 0 );
    assert( bands > 0 );
    std::vector<TileSpec> tiles;
    const int cols = ( rasterWidth + tileWidth - 1 ) / tileWidth;
    const int rows = ( rasterHeight + tileHeight - 1 ) / tileHeight;
    tiles.reserve( static_cast<size_t>( cols ) * rows );
    const int total = cols * rows;
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
