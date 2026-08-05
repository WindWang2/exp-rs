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
 *   - Constants: 42, 3.14, -1.5, 1e-6 (scientific notation supported)
 *   - Arithmetic operators: +, -, *, / (precedence: * / before + -)
 *   - Comparison operators: <, >, <=, >=, ==, != (return 1.0 / 0.0)
 *   - Logical operators: &&, || (non-zero = true; short-circuit evaluated)
 *   - Conditional: cond ? true_expr : false_expr
 *   - Parentheses: (expr)
 *   - Functions:
 *       Single-arg: sin, cos, tan, exp, ln(log), log10, sqrt, abs, asin, acos, atan
 *       Two-arg:    pow(base, exp), min(a, b), max(a, b), atan2(y, x)
 *       Zero-arg:   pi()
 *
 * Operator precedence (low → high):
 *   ||  →  &&  →  comparison  →  + -  →  * /  →  unary -  →  ternary ?:  →  primary
 *
 * Examples:
 *   "(b1 - b2) / (b1 + b2)"                          — NDVI
 *   "sqrt(b1*b1 + b2*b2)"                            — vector magnitude
 *   "b1 > 0.4 ? 1 : 0"                               — threshold mask
 *   "(b1 > 0.3 && b2 < 0.25) ? b1 : 0"               — conditional with logic
 *   "pow(b1 / (b1 + b2 + b3), 2)"                    — normalized ratio squared
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

    /**
     * Read a multi-band GeoTIFF, evaluate an expression, and write a single-band output.
     * @return true on success; optional errorMessage receives failure reason.
     */
    bool processFile(const QString &sourcePath, const QString &outputPath,
                     const QString &expression, QString *errorMessage = nullptr);
}
