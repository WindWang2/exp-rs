// chunked_processor.cpp — Chunked parallel processing implementation
#include "chunked_processor.h"

#include <algorithm>
#include <atomic>
#include <QtConcurrent>
#include <QVector>
#include <qgsfeedback.h>

ChunkedProcessor::ChunkedProcessor(int width, int height, int overlap, int chunkHeight)
    : m_width(width)
    , m_height(height)
    , m_overlap(overlap)
{
    buildChunks(chunkHeight);
}

void ChunkedProcessor::buildChunks(int chunkHeight)
{
    m_chunks.clear();

    // Validate chunkHeight to prevent infinite loop or UB
    if (chunkHeight <= 0) {
        chunkHeight = 256; // Default safe value
    }

    for (int row = 0; row < m_height; row += chunkHeight) {
        Chunk chunk;
        chunk.startRow = row;
        chunk.endRow = std::min(row + chunkHeight, m_height);
        chunk.readStartRow = std::max(0, chunk.startRow - m_overlap);
        chunk.readEndRow = std::min(m_height, chunk.endRow + m_overlap);
        m_chunks.push_back(chunk);
    }
}

bool ChunkedProcessor::process(const ChunkCallback &callback, QgsFeedback *feedback)
{
    if (m_chunks.empty())
        return true;

    // Single chunk: process directly (no threading overhead)
    if (m_chunks.size() == 1) {
        bool result = callback(m_chunks[0]);
        if (feedback) feedback->setProgress(100);
        return result;
    }

    // Multiple chunks: process in parallel
    std::vector<uint8_t> results(m_chunks.size(), 0);

    // Use QtConcurrent::map for parallel execution
    QVector<int> indices(m_chunks.size());
    for (int i = 0; i < m_chunks.size(); ++i)
        indices[i] = i;

    std::atomic<int> completedChunks{0};
    const int totalChunks = m_chunks.size();

    QtConcurrent::blockingMap(indices, [&](int idx) {
        // Check for cancellation
        if (feedback && feedback->isCanceled()) {
            results[idx] = 0;
            return;
        }

        results[idx] = callback(m_chunks[idx]) ? 1 : 0;

        // Update progress
        if (feedback) {
            int currentCompleted = ++completedChunks;
            int progress = static_cast<int>((currentCompleted * 100) / totalChunks);
            feedback->setProgress(progress);
        }
    });

    // Check all results
    for (uint8_t r : results) {
        if (!r) return false;
    }
    return true;
}
