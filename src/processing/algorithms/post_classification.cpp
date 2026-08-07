// src/processing/algorithms/post_classification.cpp — thematic transition matrix
#include "post_classification.h"

namespace TransitionMatrix
{

void countTransitions(const int32_t *before, const int32_t *after,
                      const uint8_t *valid, size_t count,
                      std::vector<uint64_t> &matrix, int classCount)
{
    if (!before || !after || classCount <= 0)
        return;
    if (matrix.size() != static_cast<size_t>(classCount) * classCount)
        matrix.assign(static_cast<size_t>(classCount) * classCount, 0);

    const size_t dimension = static_cast<size_t>(classCount);
    for (size_t i = 0; i < count; ++i) {
        if (valid && valid[i] == 0)
            continue;
        const int64_t from = before[i];
        const int64_t to = after[i];
        if (from < 0 || from >= classCount || to < 0 || to >= classCount)
            continue;
        ++matrix[static_cast<size_t>(from) * dimension + static_cast<size_t>(to)];
    }
}

void marginals(const std::vector<uint64_t> &matrix, int classCount,
               std::vector<uint64_t> &fromTotals,
               std::vector<uint64_t> &toTotals)
{
    fromTotals.assign(static_cast<size_t>(classCount), 0);
    toTotals.assign(static_cast<size_t>(classCount), 0);
    if (matrix.size() != static_cast<size_t>(classCount) * classCount)
        return;
    for (int from = 0; from < classCount; ++from) {
        for (int to = 0; to < classCount; ++to) {
            const uint64_t v = matrix[static_cast<size_t>(from) * classCount + to];
            fromTotals[from] += v;
            toTotals[to] += v;
        }
    }
}

} // namespace TransitionMatrix
