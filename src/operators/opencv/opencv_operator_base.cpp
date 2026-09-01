/***************************************************************************
 * opencv_operator_base.cpp  —  Common OpenCV operator logic
 ***************************************************************************/
#include "opencv_operator_base.h"
#include "opencv_utils.h"

#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_schema.h"

#include <QMutex>
#include <QString>

#include <opencv2/core.hpp>

namespace sicnu::operators::opencv {

namespace {
// Cap OpenCV's internal parallel_for threads (#692): GaussianBlur / median /
// Canny defaulted to one thread per core, multiplying the ChunkedProcessor
// fan and oversubscribing JobEngine workers. Applied once per process.
void capOpenCvThreadsOnce()
{
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    static bool capped = false;
    if (capped)
        return;
    capped = true;
    // SICNU_CV_THREADS overrides (a positive count sets it; an explicit 0
    // disables OpenCV-internal threading entirely).
    int threads = 2;
    if (qEnvironmentVariableIsSet("SICNU_CV_THREADS")) {
        const int envVal = qEnvironmentVariableIntValue("SICNU_CV_THREADS");
        if (envVal >= 0)
            threads = envVal;
    }
    cv::setNumThreads(threads);
}
} // namespace

Json::Value OpenCvOperatorBase::run(const Json::Value& params, RSOperatorContext& context) {
    capOpenCvThreadsOnce();
    validateCommonParams(params);

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    context.logInfo("Reading raster: " + inputPath);
    std::vector<cv::Mat> bands = readRasterBandsToMats(inputPath);
    if (bands.empty()) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Failed to read raster bands from: " + inputPath);
    }

    const int bandCount = static_cast<int>(bands.size());
    for (int i = 0; i < bandCount; ++i) {
        context.throwIfCancelled();
        context.reportProgress(static_cast<double>(i) / bandCount,
                               "Processing band " + std::to_string(i + 1) + "/" + std::to_string(bandCount));

        applyFilter(bands[i], params);

        context.reportProgress(static_cast<double>(i + 1) / bandCount,
                               "Finished band " + std::to_string(i + 1));
    }

    context.logInfo("Writing output: " + outputPath);
    std::string writeError;
    if (!writeMatsToRaster(outputPath, bands, inputPath, &writeError)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + writeError);
    }

    context.reportProgress(1.0, "Complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["bands"] = bandCount;
    return result;
}

void OpenCvOperatorBase::validateCommonParams(const Json::Value& params) const {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }
    // Touch required string fields so errors match requireString wording.
    requireString(params, "input");
    requireString(params, "output");
}

Json::Value OpenCvOperatorBase::operatorSchemaProperties() const {
    return Json::Value(Json::objectValue);
}

std::vector<std::string> OpenCvOperatorBase::operatorRequiredParams() const {
    return {};
}

Json::Value OpenCvOperatorBase::buildSchema(const std::string& title,
                                            const std::string& description) const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster file path");
    props["output"] = makeOutputParam("output", "Output raster file path", "tif");
    props["band"] = makeIntegerParam("band", "Band to process (1-based; ignored, all bands are processed)", 1);

    Json::Value opProps = operatorSchemaProperties();
    for (const auto& member : opProps.getMemberNames()) {
        props[member] = opProps[member];
    }

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster file path");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands");

    Json::Value root = makeRootSchema(title, description, props, outputs);

    std::vector<std::string> required = {"input", "output"};
    std::vector<std::string> opRequired = operatorRequiredParams();
    required.insert(required.end(), opRequired.begin(), opRequired.end());
    root["required"] = makeRequired(required);

    return root;
}

} // namespace sicnu::operators::opencv
