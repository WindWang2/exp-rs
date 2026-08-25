// src/processing/algorithms/band_math.h
#pragma once

#include "band_math_ast.h"
#include "band_math_simd.h"

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
 *   - Arithmetic operators: +, -, *, /, %, ^ (precedence: ^ before * / % before + -)
 *   - Comparison operators: <, >, <=, >=, ==, != (return 1.0 / 0.0, NaN on non-finite)
 *   - Logical operators: &&, ||, ! (non-zero = true)
 *   - Conditional: cond ? true_expr : false_expr or if(cond, true_val, false_val)
 *   - Parentheses: (expr)
 *   - Functions:
 *       Single-arg: sin, cos, tan, asin, acos, atan, sqrt, cbrt, exp, ln(log), log10, abs, ceil, floor, round
 *       Two-arg:    pow(base, exp), min(a, b), max(a, b), std::min(a, b), std::max(a, b), atan2(y, x)
 *       Three-arg:  clamp(x, lo, hi), if(cond, true_val, false_val)
 *       Zero-arg:   pi()
 *       Macros:     ndvi(nir, red), ndwi(green, nir), mndwi(green, swir), evi(nir, red, blue),
 *                   savi(nir, red, L), nbr(nir, swir2), bsi(swir, red, nir, blue)
 *
 * Operator precedence (low -> high):
 *   ?: -> || -> && -> comparison -> + - -> * / % -> ^ -> unary - ! -> primary
 */
namespace BandMath
{
    /**
     * Evaluate an expression over multi-band pixel data using SIMD execution kernels.
     *
     * @param expression  The math expression string (e.g., "(b1 - b2) / (b1 + b2)")
     * @param bands       Map of band number -> pixel data (each must have >= count elements)
     * @param out         Output buffer (pre-allocated, size >= count)
     * @param count       Number of pixels to process
     * @return true on success, false on invalid arguments or parse error
     *
     * Division by zero produces NaN. Missing band references return false.
     */
    bool evaluate(const QString &expression, const BandData &bands, float *out, size_t count);

    /**
     * Evaluate an expression using scalar execution path for differential oracle verification.
     */
    bool evaluateScalar(const QString &expression, const BandData &bands, float *out, size_t count);

    /**
     * Read a multi-band GeoTIFF, evaluate an expression via bounded tile streaming (<64MB RAM),
     * and write a single-band output GeoTIFF.
     * @return true on success; optional errorMessage receives failure reason.
     */
    bool processFile(const QString &sourcePath, const QString &outputPath,
                     const QString &expression, QString *errorMessage = nullptr);

    /// Return sorted unique band numbers referenced by @a expression (e.g. "b1+b3" -> {1,3}).
    /// Returns empty vector on parse error.
    std::vector<int> referencedBands(const QString &expression);
}
