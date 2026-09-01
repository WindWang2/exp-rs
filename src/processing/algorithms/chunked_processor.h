// chunked_processor.h — Chunked parallel processing for large rasters
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

class QgsFeedback;

/**
 * Chunked parallel processor for large raster operations.
 * Splits the image into horizontal strips and processes them in parallel
 * using Qt's thread pool.
 *
 * Usage:
 *   ChunkedProcessor proc(width, height, kernelOverlap);
 *   proc.process([&](int startRow, int endRow) {
 *       // Process rows [startRow, endRow)
 *       // Input/output buffers are pre-allocated
 *   });
 */
class ChunkedProcessor
{
public:
    /**
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param overlap Number of extra rows needed above/below for kernel operations
     * @param chunkHeight Target chunk height in rows (default 256)
     */
    ChunkedProcessor(int width, int height, int overlap = 0, int chunkHeight = 256);

    struct Chunk {
        int startRow;      // First row to process (output)
        int endRow;        // Last row to process (output, exclusive)
        int readStartRow;  // First row to read (includes overlap)
        int readEndRow;    // Last row to read (includes overlap, exclusive)
    };

    int chunkCount() const { return static_cast<int>(m_chunks.size()); }
    const Chunk &chunk(int index) const { return m_chunks[index]; }

    /**
     * Nested-parallelism token (#692): default cap on the number of chunks
     * processed concurrently. Derived from the core count divided by the
     * worst-case number of concurrent outer jobs (JobEngine clamps its worker
     * pool to 2..4 threads; sicnu_processing cannot link sicnu_jobs, so the
     * budget assumes 4). Callers that know their outer parallelism should
     * pass an explicit maxThreads to process() instead.
     */
    static int defaultMaxThreads();

    /**
     * Process all chunks in parallel using QtConcurrent.
     * The callback is called once per chunk, potentially from different threads.
     * @param callback Function to process each chunk
     * @param feedback Optional feedback for progress reporting and cancellation
     * @param maxThreads Nested-parallelism token (#692): cap on the number of
     *        chunks executed concurrently. Runs on a dedicated (non-global)
     *        thread pool with this bound. 0 = auto (defaultMaxThreads());
     *        values < 1 are clamped to 1.
     * @return true if all chunks succeeded
     */
    using ChunkCallback = std::function<bool(const Chunk &chunk)>;
    bool process(const ChunkCallback &callback, QgsFeedback *feedback = nullptr,
                 int maxThreads = 0);

private:
    int m_width;
    int m_height;
    int m_overlap;
    std::vector<Chunk> m_chunks;

    void buildChunks(int chunkHeight);
};
