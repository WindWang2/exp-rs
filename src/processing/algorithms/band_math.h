// src/processing/algorithms/band_math.h
#pragma once

#include <QString>
#include <cstddef>
#include <map>
#include <vector>

/**
 * Band math engine: evaluate arbitrary expressions on multi-band raster data.
 *
 * Expression syntax:
 *   - Band references: b1, b2, ..., bN (1-based)
 *   - Constants: 42, 3.14, -1.5
 *   - Operators: +, -, *, /
 *   - Parentheses: (expr)
 *   - Operator precedence: * / before + -
 *
 * Example: "(b1 - b2) / (b1 + b2)" computes NDVI from bands 1 (NIR) and 2 (Red).
 */
namespace BandMath
{
    /// Map of band number (1-based) → pixel data array.
    using BandData = std::map<int, std::vector<float>>;

    /**
     * Evaluate an expression over multi-band pixel data.
     *
     * @param expression  The math expression string (e.g., "(b1 - b2) / (b1 + b2)")
     * @param bands       Map of band number → pixel data (each must have >= count elements)
     * @param out         Output buffer (pre-allocated, size >= count)
     * @param count       Number of pixels to process
     * @return true on success, false on invalid arguments or parse error
     *
     * Division by zero produces NaN. Missing band references return false.
     */
    bool evaluate(const QString &expression, const BandData &bands, float *out, size_t count);
}
