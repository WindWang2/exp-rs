// gdal_async_reader.h — GDAL async reader with double buffering
#pragma once

#include <vector>
#include <functional>
#include <atomic>
#include <QFuture>
#include <QFutureWatcher>

typedef void *GDALDatasetH;

/**
 * GDAL async reader with double buffering for large raster processing.
 *
 * Reads raster data in chunks, with pre-fetching of the next chunk
 * while the current chunk is being processed.
 *
 * Usage:
 *   GdalAsyncReader reader(dataset, bandCount);
 *   reader.open(width, height);
 *
 *   while (reader.hasNextChunk()) {
 *       auto chunk = reader.readNextChunk();
 *       // Process chunk.data while next chunk is being prefetched
 *       process(chunk);
 *   }
 */
class GdalAsyncReader
{
public:
    struct Chunk {
        int startRow;
        int endRow;
        int height;
        std::vector<float> data; // Interleaved: [band0_row0, band1_row0, ..., bandN_row0, band0_row1, ...]
    };

    GdalAsyncReader(GDALDatasetH dataset, int bandCount);
    ~GdalAsyncReader();

    /**
     * Open the reader for a raster of given dimensions.
     * @param width Raster width
     * @param height Raster height
     * @param chunkHeight Height of each chunk (default 256)
     */
    bool open(int width, int height, int chunkHeight = 256);

    /**
     * Check if there are more chunks to read.
     */
    bool hasNextChunk() const;

    /**
     * Read the next chunk. Data is read synchronously.
     * Call prefetchNextChunk() after processing to start reading the next chunk.
     */
    Chunk readNextChunk();

    /**
     * Start reading the next chunk asynchronously.
     * Call this after processing the current chunk to overlap I/O with computation.
     */
    void prefetchNextChunk();

    /**
     * Wait for the prefetched chunk to be ready and return it.
     */
    Chunk getPrefetchedChunk();

    /**
     * Get the total number of chunks.
     */
    int chunkCount() const { return m_chunkCount; }

    /**
     * Get the current chunk index.
     */
    int currentChunkIndex() const { return m_currentChunk; }

private:
    GDALDatasetH m_dataset;
    int m_bandCount;
    int m_width;
    int m_height;
    int m_chunkHeight;
    int m_chunkCount;
    int m_currentChunk;

    // Double buffering
    Chunk m_currentBuffer;
    Chunk m_prefetchBuffer;
    QFuture<Chunk> m_prefetchFuture;
    QFutureWatcher<Chunk> *m_prefetchWatcher = nullptr;
    std::atomic<bool> m_prefetchReady{false};

    Chunk readChunk(int chunkIndex);
};
