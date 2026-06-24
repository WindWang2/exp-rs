// gdal_async_reader.cpp — GDAL async reader implementation
#include "gdal_async_reader.h"

#include <gdal.h>
#include <algorithm>
#include <QtConcurrent>

GdalAsyncReader::GdalAsyncReader(GDALDatasetH dataset, int bandCount)
    : m_dataset(dataset)
    , m_bandCount(bandCount)
    , m_width(0)
    , m_height(0)
    , m_chunkHeight(256)
    , m_chunkCount(0)
    , m_currentChunk(0)
{
}

GdalAsyncReader::~GdalAsyncReader()
{
    if (m_prefetchWatcher) {
        if (m_prefetchWatcher->isRunning()) {
            m_prefetchFuture.cancel();
            m_prefetchWatcher->waitForFinished();
        }
        delete m_prefetchWatcher;
    }
}

bool GdalAsyncReader::open(int width, int height, int chunkHeight)
{
    if (!m_dataset || width <= 0 || height <= 0 || chunkHeight <= 0 || m_bandCount <= 0)
        return false;

    m_width = width;
    m_height = height;
    m_chunkHeight = chunkHeight;
    m_chunkCount = (height + chunkHeight - 1) / chunkHeight;
    m_currentChunk = 0;

    return true;
}

bool GdalAsyncReader::hasNextChunk() const
{
    return m_currentChunk < m_chunkCount;
}

GdalAsyncReader::Chunk GdalAsyncReader::readChunk(int chunkIndex)
{
    Chunk chunk;
    chunk.startRow = chunkIndex * m_chunkHeight;
    chunk.endRow = std::min(chunk.startRow + m_chunkHeight, m_height);
    chunk.height = chunk.endRow - chunk.startRow;

    size_t pixelCount = static_cast<size_t>(m_width) * chunk.height;
    chunk.data.resize(pixelCount * m_bandCount);

    // Read all bands for this chunk
    for (int b = 0; b < m_bandCount; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(m_dataset, b + 1);
        if (!band) {
            chunk.data.clear();
            return chunk;
        }

        CPLErr err = GDALRasterIO(band, GF_Read,
                                   0, chunk.startRow, m_width, chunk.height,
                                   chunk.data.data() + b * pixelCount,
                                   m_width, chunk.height, GDT_Float32, 0, 0);
        if (err != CE_None) {
            chunk.data.clear();
            return chunk;
        }
    }

    return chunk;
}

GdalAsyncReader::Chunk GdalAsyncReader::readNextChunk()
{
    if (!hasNextChunk()) {
        Chunk empty;
        return empty;
    }

    Chunk chunk = readChunk(m_currentChunk);
    m_currentChunk++;
    return chunk;
}

void GdalAsyncReader::prefetchNextChunk()
{
    int nextChunk = m_currentChunk;
    if (nextChunk >= m_chunkCount) {
        m_prefetchReady = false;
        return;
    }

    if (!m_prefetchWatcher) {
        m_prefetchWatcher = new QFutureWatcher<Chunk>();
        QObject::connect(m_prefetchWatcher, &QFutureWatcher<Chunk>::finished, [this]() {
            m_prefetchReady = true;
        });
    }

    // Capture values for async execution
    GDALDatasetH dataset = m_dataset;
    int bandCount = m_bandCount;
    int width = m_width;
    int height = m_height;
    int chunkHeight = m_chunkHeight;

    m_prefetchFuture = QtConcurrent::run([dataset, bandCount, width, height, chunkHeight, nextChunk]() -> Chunk {
        Chunk chunk;
        chunk.startRow = nextChunk * chunkHeight;
        chunk.endRow = std::min(chunk.startRow + chunkHeight, height);
        chunk.height = chunk.endRow - chunk.startRow;

        size_t pixelCount = static_cast<size_t>(width) * chunk.height;
        chunk.data.resize(pixelCount * bandCount);

        for (int b = 0; b < bandCount; ++b) {
            GDALRasterBandH band = GDALGetRasterBand(dataset, b + 1);
            if (!band) {
                chunk.data.clear();
                return chunk;
            }

            CPLErr err = GDALRasterIO(band, GF_Read,
                                       0, chunk.startRow, width, chunk.height,
                                       chunk.data.data() + b * pixelCount,
                                       width, chunk.height, GDT_Float32, 0, 0);
            if (err != CE_None) {
                chunk.data.clear();
                return chunk;
            }
        }

        return chunk;
    });

    m_prefetchWatcher->setFuture(m_prefetchFuture);
}

GdalAsyncReader::Chunk GdalAsyncReader::getPrefetchedChunk()
{
    if (!m_prefetchReady) {
        // Wait for prefetch to complete
        if (m_prefetchWatcher && m_prefetchWatcher->isRunning()) {
            m_prefetchWatcher->waitForFinished();
        }
    }

    m_prefetchReady = false;
    m_currentChunk++;
    return m_prefetchFuture.result();
}
