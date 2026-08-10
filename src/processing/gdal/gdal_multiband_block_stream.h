// gdal_multiband_block_stream.h — multi-band out-of-core streaming iterator.
//
// Companion to GdalBlockStream (single-band). Several per-pixel hyperspectral
// operators (RX anomaly, spectral unmixing, SAM/SID classify, endmember
// extraction, spectral resampling) all share one shape: read every band, scatter
// into a pixel-interleaved (BIP) buffer of size pixelCount*bandCount, run a
// per-pixel kernel (no neighborhood), then re-scatter the output. That pattern
// materializes the whole raster at once and is the largest single-operator
// memory cost in the processing stack (perf/architecture goal §2c).
//
// GdalMultibandBlockStream turns that shape into a tile loop: each tile yields a
// BIP window (all bands, one tile) so a per-pixel kernel runs with O(tile)
// memory instead of O(raster). The tile geometry is identical to GdalBlockStream
// so single- and multi-band streams compose naturally.
//
// Threading contract: forEach() is single-threaded and sequential (GDAL reads
// are not concurrent-safe on one dataset). The BIP buffer is BORROWED for the
// duration of one callback invocation and overwritten by the next tile.
#pragma once

#include "gdal_block_stream.h" // for Tile geometry (reused)

#include <gdal.h>

#include <QString>

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

class GdalDatasetWrapper;

/**
 * Streaming multi-band tile iterator over a GDAL raster.
 *
 * Tiles share GdalBlockStream::Tile geometry (row-major, edge-clamped). Each
 * tile's bands are read into a pixel-interleaved buffer:
 *   pixels[(y * width + x) * bandCount + b]
 * handed to the callback alongside the tile geometry.
 */
class GdalMultibandBlockStream
{
  public:
    /// Reuse GdalBlockStream's tile geometry (identical fields + meaning).
    using Tile = GdalBlockStream::Tile;

    /**
     * @param ds         open dataset wrapper
     * @param bandCount  number of bands to interleave (1..ds.bandCount(), read
     *                   consecutively from band 1)
     * @param tileWidth  nominal tile width in pixels (default 256)
     * @param tileHeight nominal tile height in pixels (default 256)
     */
    GdalMultibandBlockStream( const GdalDatasetWrapper &ds, int bandCount,
                              int tileWidth = 256, int tileHeight = 256 );

    /**
     * Band-subset overload: interleave only the listed (1-based) band numbers.
     * bandCount() == bandList.size(). Used by band-subset operators (SAM/SID
     * classify, spectral index selection, …) that process a subset of bands.
     * @a bandList must be non-empty, 1-based, within ds.bandCount().
     */
    GdalMultibandBlockStream( const GdalDatasetWrapper &ds,
                              const std::vector<int> &bandList,
                              int tileWidth = 256, int tileHeight = 256 );

    int tileCount() const { return static_cast<int>( m_tiles.size() ); }
    const Tile &tile( int i ) const { return m_tiles[i]; }
    int tileWidth() const { return m_tileWidth; }
    int tileHeight() const { return m_tileHeight; }
    int rasterWidth() const { return m_rasterWidth; }
    int rasterHeight() const { return m_rasterHeight; }
    int bandCount() const { return static_cast<int>( m_bandList.size() ); }

    /**
     * Stream every tile. For each tile the callback receives the tile geometry
     * and a pixel-interleaved float buffer of size tile.width*tile.height*
     * bandCount filled with all bands' pixel values for that window.
     *
     * BIP layout: pixels[((y * tile.width + x) * bandCount) + b]. The buffer is
     * reused across tiles (borrowed — see the file-level threading contract).
     *
     * @param callback  called once per tile; return false to abort early
     * @return true if all tiles were visited, false if the callback aborted or
     *         a read failed
     */
    using TileCallback =
        std::function<bool( const Tile &tile, const float *pixelsBip )>;
    bool forEach( const TileCallback &callback ) const;

  private:
    const GdalDatasetWrapper &m_ds;
    std::vector<int> m_bandList; ///< 1-based band numbers to interleave
    int m_tileWidth;
    int m_tileHeight;
    int m_rasterWidth;
    int m_rasterHeight;
    std::vector<Tile> m_tiles;

    void buildTiles();
};

/**
 * Streaming output writer: a GeoTIFF created once, written tile-by-tile. The
 * band-major counterpart to writeGdalOutput (which writes the whole raster in
 * one call). Each band is written per tile via writeBandWindow. This lets
 * streaming operators emit output without materializing a full-raster buffer.
 *
 * Usage: construct with the output geometry (mirroring the input), then call
 * writeTile() once per tile per band as the input streams. close() (or the
 * destructor) flushes.
 */
class GdalStreamingOutput
{
  public:
    /// @a dtype is a GDALDataType (GDT_Float32, …) as an int, matching
    /// createOutputTiff's convention.
    GdalStreamingOutput( const QString &path, int width, int height, int bands,
                         int dtype,
                         const std::array<double, 6> &geoTransform,
                         const QString &projection );
    ~GdalStreamingOutput();
    GdalStreamingOutput( const GdalStreamingOutput & ) = delete;
    GdalStreamingOutput &operator=( const GdalStreamingOutput & ) = delete;

    bool isOpen() const { return m_ds != nullptr; }

    /// Write one tile of one band. @a pixels is row-major, tile.width*tile.height
    /// floats for band @a band (1-based). Returns false on write failure.
    bool writeTile( int band, const GdalBlockStream::Tile &tile, const float *pixels );

    /// Flush and release the dataset. Idempotent.
    void close();

  private:
    GDALDatasetH m_ds = nullptr;
};
