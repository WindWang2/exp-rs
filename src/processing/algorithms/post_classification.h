// src/processing/algorithms/post_classification.h — thematic transition matrix
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace TransitionMatrix
{

/**
 * Counts class transitions between two thematic rasters into a row-major
 * transition matrix: matrix[from * classCount + to] += 1 for every valid
 * pixel whose class moved from `from` (before) to `to` (after). Pixels whose
 * class is unchanged land on the diagonal and are counted normally.
 *
 * @param before     class id per pixel (0-based or 1-based, as produced by the
 *                   classifier; must be in [0, classCount))
 * @param after      class id per pixel (same convention as @p before)
 * @param valid      per-pixel validity flag: 1 = count the transition,
 *                   0 = skip (e.g. NoData pixels). May be null to count all.
 * @param count      number of pixels
 * @param matrix     pre-sized to classCount * classCount entries, zeroed;
 *                   accumulates into it (caller may accumulate blocks)
 * @param classCount number of classes (matrix dimension)
 */
void countTransitions(const int32_t *before, const int32_t *after,
                      const uint8_t *valid, size_t count,
                      std::vector<uint64_t> &matrix, int classCount);

/**
 * Row sums (per-before-class totals) and column sums (per-after-class totals)
 * of a row-major transition matrix. @p matrix must hold classCount^2 entries.
 */
void marginals(const std::vector<uint64_t> &matrix, int classCount,
               std::vector<uint64_t> &fromTotals,
               std::vector<uint64_t> &toTotals);

} // namespace TransitionMatrix
