/***************************************************************************
 * rs_band_math_operator.h  —  Band math RSOperator
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * Evaluates an arbitrary arithmetic expression on multi-band raster data.
 *
 * Expression syntax:
 *   - Band references: b1, b2, ..., bN (1-based)
 *   - Constants: 42, 3.14, -1.5
 *   - Operators: +, -, *, /
 *   - Parentheses: (expr)
 *   - Operator precedence: * / before + -
 *
 * Example: "(b1 - b2) / (b1 + b2)" computes NDVI from bands 1 and 2.
 *
 * Parameters:
 *   input       (string, required) Input multi-band raster path
 *   output      (string, required) Output single-band raster path
 *   expression  (string, required) Math expression string
 *
 * Returns JSON object with:
 *   output      (string) Output raster path
 *   expression  (string) Evaluated expression
 *   width       (int)    Output width
 *   height      (int)    Output height
 */
class RsBandMathOperator : public RSOperator {
public:
    std::string name() const override { return "rs:band_math"; }
    std::string displayName() const override { return "Band Math"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Evaluate an arithmetic expression over raster bands.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
