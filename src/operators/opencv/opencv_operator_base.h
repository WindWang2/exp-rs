/***************************************************************************
 * opencv_operator_base.h  —  Common base for OpenCV-based RSOperators
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator.h"

#include <json/json.h>

#include <opencv2/core.hpp>

namespace sicnu::operators::opencv {

// Re-export shared JSON helpers for subclasses (same call style as before).
using params::fileExists;
using params::requireString;
using params::getInt;
using params::getDouble;

/**
 * Base class for OpenCV operators that process raster bands independently.
 *
 * Subclasses implement:
 *   - applyFilter(cv::Mat& srcDst, const Json::Value& params)
 *
 * The base class handles:
 *   - Reading `input`, `output`, and optional `band` parameters
 *   - Opening the input GeoTIFF via GDAL
 *   - Iterating bands and calling applyFilter()
 *   - Writing the result GeoTIFF with preserved georeferencing
 *   - Progress reporting and cancellation checks
 */
class OpenCvOperatorBase : public RSOperator {
public:
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;

protected:
    /**
     * Validates common parameters and throws RSOperatorError on failure.
     */
    void validateCommonParams(const Json::Value& params) const;

    /**
     * Applies the image-processing filter to a single band.
     * Implementations may modify srcDst in place or replace it.
     */
    virtual void applyFilter(cv::Mat& srcDst, const Json::Value& params) const = 0;

    /**
     * Override to provide operator-specific schema properties.
     */
    virtual Json::Value operatorSchemaProperties() const;

    /**
     * Override to provide operator-specific required parameter names.
     */
    virtual std::vector<std::string> operatorRequiredParams() const;

    Json::Value buildSchema(const std::string& title,
                            const std::string& description) const;
};

} // namespace sicnu::operators::opencv
