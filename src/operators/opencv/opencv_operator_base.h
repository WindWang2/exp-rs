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
 *
 * Windowed filters (blur/median/gradient) never see a full band: run() streams
 * the input through GdalBlockStream in 256x256 tiles with a kernel-radius halo
 * and writes each tile through GdalStreamingOutput, applying the very same
 * applyFilter() kernel to every tile. Filters that genuinely need full-frame
 * state (see neighborhoodRadius) keep the whole-band materializing path.
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
     * Neighborhood radius in pixels the filter needs beyond any output pixel,
     * or -1 when the filter cannot run on tiles because it needs full-frame
     * state (e.g. Canny normalizes with the global min/max). Determines
     * whether run() streams tiles (radius >= 0, used as the halo) or
     * materializes each band whole (radius < 0).
     */
    virtual int neighborhoodRadius(const Json::Value& params) const;

    /**
     * Applies the image-processing filter to a single band.
     * Implementations may modify srcDst in place or replace it.
     * When the filter is tiled, srcDst is a haloed tile window: the kernel's
     * own border extrapolation applies only at that window's edge, which
     * coincides with the raster border exactly where the full-frame call
     * would extrapolate.
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

private:
    /**
     * Tile-streaming implementation used when neighborhoodRadius() >= 0:
     * per band, 256x256 tiles with a kernel-radius halo are read via
     * GdalBlockStream, converted to the operator's NaN convention (#444),
     * filtered in place with applyFilter(), and written through
     * GdalStreamingOutput with the full-frame NoData semantics (#445).
     */
    Json::Value runStreaming(const std::string& inputPath,
                             const std::string& outputPath,
                             int halo,
                             const Json::Value& params,
                             RSOperatorContext& context);
};

} // namespace sicnu::operators::opencv
