// gdal_block_stream.h — Out-of-core streaming iterator for huge GeoTIFFs.
//
// Iterates a raster band tile-by-tile using GDAL's native block layout (or a
// caller-chosen tile size), invoking a callback per tile. Memory footprint is
// O(tileWidth*tileHeight) rather than O(width*height), so rasters larger than
// RAM can be processed without OOM.
//
// Complements ChunkedProcessor (which parallel-processes an in-memory buffer):
// use GdalBlockStream when the input is too large to read in one go, then hand
// each tile to a kernel for the compute.
//
// Threading contract: forEach() is single-threaded and sequential (GDAL reads
// are not concurrent-safe on one dataset). The callback receives a BORROWED
// pixel buffer: it is valid only for the duration of the callback invocation
// and is overwritten by the next tile's read. If the callback needs to defer
// work (e.g. dispatch to a thread pool) it MUST copy the tile data out before
// returning — retaining or concurrently reading the pointer is a data race.
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

class GdalDatasetWrapper;

/**
 * Streaming tile iterator over a GDAL raster band.
 *
 * Tiles are visited in row-major order. Each tile is read into a caller-owned
 * float buffer via GdalDatasetWrapper, then handed to the callback along with
 * its pixel geometry. Edge tiles may be narrower/shorter than the nominal tile
 * size to fit the raster extent.
 *
 * The iterator is single-threaded by design (GDAL reads are sequential); the
 * callback is free to dispatch compute onto a thread pool.
 */
class GdalBlockStream
{
  public:
    /// Geometry of one streamed tile.
    struct Tile
    {
        int xOffset;       ///< pixel column of the tile's left edge (0-based)
        int yOffset;       ///< pixel row of the tile's top edge (0-based)
        int width;         ///< tile width in pixels (<= tileWidth, edge-clamped)
        int height;        ///< tile height in pixels (<= tileHeight, edge-clamped)
        int index;         ///< 0-based tile index in row-major visit order
        int totalTiles;    ///< total tile count
    };

    /**
     * @param ds         open dataset wrapper
     * @param bandNum    1-based band number
     * @param tileWidth  nominal tile width in pixels (default 256)
     * @param tileHeight nominal tile height in pixels (default 256)
     */
    GdalBlockStream( const GdalDatasetWrapper &ds, int bandNum,
                     int tileWidth = 256, int tileHeight = 256 );

    /// Total number of tiles the iterator will visit.
    int tileCount() const { return static_cast<int>( m_tiles.size() ); }

    /// The (edge-clamped) tile geometry for index i.
    const Tile &tile( int i ) const { return m_tiles[i]; }

    /// Nominal tile width/height (before edge clamping).
    int tileWidth() const { return m_tileWidth; }
    int tileHeight() const { return m_tileHeight; }

    /// Raster width/height the iterator was constructed from.
    int rasterWidth() const { return m_rasterWidth; }
    int rasterHeight() const { return m_rasterHeight; }

    /**
     * Stream every tile. For each tile the callback receives the tile geometry
     * and a float buffer of size tile.width*tile.height filled with the band's
     * pixel values for that window.
     *
     * Buffer layout: row-major with stride exactly tile.width (NOT the nominal
     * tileWidth()). Index pixels as `pixels[y * tile.width + x]`. The buffer is
     * reused across tiles — see the file-level threading/borrow contract.
     *
     * @param callback  called once per tile; return false to abort early
     * @return true if all tiles were visited, false if the callback aborted or
     *         a read failed
     */
    using TileCallback = std::function<bool( const Tile &tile, const float *pixels )>;
    bool forEach( const TileCallback &callback ) const;

  private:
    const GdalDatasetWrapper &m_ds;
    int m_bandNum;
    int m_tileWidth;
    int m_tileHeight;
    int m_rasterWidth;
    int m_rasterHeight;
    std::vector<Tile> m_tiles;

    void buildTiles();
};
