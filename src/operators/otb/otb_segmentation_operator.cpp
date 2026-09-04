/***************************************************************************
 * otb_segmentation_operator.cpp  —  OTB Segmentation argument builder
 ***************************************************************************/
#include "otb_segmentation_operator.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <QFile>
#include <QString>

namespace sicnu::operators::otb {

namespace {

const std::vector<std::string> s_filters = {
    "meanshift", "cc", "watershed", "mprofiles"
};

const std::vector<std::string> s_outputModes = {
    "vector", "raster"
};

} // anonymous namespace

Json::Value OtbSegmentationOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster file path");
    props["filter"] = makeEnumParam("filter", "Segmentation algorithm", s_filters, "meanshift");
    props["outputMode"] = makeEnumParam("outputMode",
                                        "Output type: vector polygons or raster labels",
                                        s_outputModes, "vector");

    // MeanShift parameters
    props["spatialRadius"] = makeIntegerParam("spatialRadius",
                                              "MeanShift spatial radius (pixels)", 5);
    setRange(props["spatialRadius"], 1, 1000);
    props["rangeRadius"] = makeNumberParam("rangeRadius",
                                           "MeanShift range radius (spectral distance)", 15.0);
    setRange(props["rangeRadius"], 0.1, 1000.0);
    props["minRegionSize"] = makeIntegerParam("minRegionSize",
                                              "MeanShift minimum region size (pixels)", 100);
    setRange(props["minRegionSize"], 1, 100000);
    props["maxIterations"] = makeIntegerParam("maxIterations",
                                              "MeanShift maximum iterations", 100);
    setRange(props["maxIterations"], 1, 10000);
    // threshold is shared: meanshift convergence (default 0.1) and watershed
    // (default 0.01). One schema default cannot express both — the declared
    // 0.01 matches the watershed fallback; the meanshift fallback is 0.1.
    // The description states both so agents do not infer a wrong default.
    props["threshold"] = makeNumberParam(
        "threshold",
        "MeanShift convergence / watershed level threshold "
        "(when omitted: meanshift 0.1, watershed 0.01)",
        0.01);
    setRange(props["threshold"], 0.0001, 1.0);
    props["threshold"]["required"] = false;

    // Connected components parameter
    props["ccExpression"] = makeStringParam("ccExpression",
                                            "Connected-components condition expression",
                                            "(p1b1 > 0)");
    props["ccExpression"]["required"] = false;

    // Morphological profiles parameters (mprofiles) — previously read but undeclared (S2)
    props["profileSize"] = makeIntegerParam("profileSize",
                                            "Morphological profile size", 5);
    setRange(props["profileSize"], 1, 100);
    props["profileSize"]["required"] = false;
    props["startRadius"] = makeIntegerParam("startRadius",
                                            "Morphological start radius", 1);
    setRange(props["startRadius"], 1, 100);
    props["startRadius"]["required"] = false;
    props["radiusStep"] = makeIntegerParam("radiusStep",
                                           "Morphological radius step", 1);
    setRange(props["radiusStep"], 1, 100);
    props["radiusStep"]["required"] = false;
    props["sigma"] = makeNumberParam("sigma",
                                     "Morphological sigma", 1.0);
    setRange(props["sigma"], 0.1, 100.0);
    props["sigma"]["required"] = false;

    // Output
    props["output"] = makeOutputParam("output",
                                      "Output vector file (mode=vector) or raster file (mode=raster)",
                                      "shp");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeStringParam("output", "Output file path");
    outputs["filter"] = makeStringParam("filter", "Applied segmentation algorithm");
    outputs["outputMode"] = makeStringParam("outputMode", "Output mode (vector/raster)");

    return buildSchema(displayName(), description(), props, outputs,
                       {"input", "output"});
}

Json::Value OtbSegmentationOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("segmentation");
    meta["tags"].append("otb");
    meta["tags"].append("meanshift");
    meta["tags"].append("object-based image analysis");
    meta["purpose"] = "Partition an image into meaningful regions for object-based analysis.";
    meta["prerequisites"].append("Input raster must be readable by GDAL.");
    meta["prerequisites"].append("For vector output, the output path should end with .shp or .sqlite.");
    meta["limitations"].append("Large images may require significant RAM in raster mode; use vector mode for large datasets.");
    meta["workflowHints"].append("Use otb:compute_images_statistics before classification if needed.");
    return meta;
}

QStringList OtbSegmentationOperator::buildOtbArgs(const Json::Value& params,
                                                  RSOperatorContext& context) const {
    Q_UNUSED(context);

    const std::string filter = getEnum(params, "filter", s_filters, "meanshift");
    const std::string outputMode = getEnum(params, "outputMode", s_outputModes, "vector");

    const std::string inputPath = requireString(params, "input");
    if (!QFile::exists(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    const std::string outputPath = requireString(params, "output");

    QStringList args;
    args << "-in" << QString::fromStdString(inputPath);
    args << "-filter" << QString::fromStdString(filter);
    args << "-mode" << QString::fromStdString(outputMode);

    if (filter == "meanshift") {
        const int spatialRadius = getInt(params, "spatialRadius", 5);
        const double rangeRadius = getDouble(params, "rangeRadius", 15.0);
        const int minRegionSize = getInt(params, "minRegionSize", 100);
        const int maxIterations = getInt(params, "maxIterations", 100);
        const double threshold = getDouble(params, "threshold", 0.1);

        args << "-filter.meanshift.spatialr" << QString::number(spatialRadius);
        args << "-filter.meanshift.ranger" << QString::number(rangeRadius, 'f', 2);
        args << "-filter.meanshift.minsize" << QString::number(minRegionSize);
        args << "-filter.meanshift.maxiter" << QString::number(maxIterations);
        args << "-filter.meanshift.thres" << QString::number(threshold, 'f', 4);
    } else if (filter == "cc") {
        const std::string expr = getString(params, "ccExpression", "(p1b1 > 0)");
        args << "-filter.cc.expr" << QString::fromStdString(expr);
    } else if (filter == "watershed") {
        const double threshold = getDouble(params, "threshold", 0.01);
        args << "-filter.watershed.threshold" << QString::number(threshold, 'f', 4);
    } else if (filter == "mprofiles") {
        // mprofiles has several parameters; expose a minimal set with defaults.
        const int profileSize = getInt(params, "profileSize", 5);
        const int startRadius = getInt(params, "startRadius", 1);
        const int radiusStep = getInt(params, "radiusStep", 1);
        const double sigma = getDouble(params, "sigma", 1.0);
        args << "-filter.mprofiles.size" << QString::number(profileSize);
        args << "-filter.mprofiles.start" << QString::number(startRadius);
        args << "-filter.mprofiles.step" << QString::number(radiusStep);
        args << "-filter.mprofiles.sigma" << QString::number(sigma, 'f', 4);
    }

    if (outputMode == "vector") {
        args << "-mode.vector.out" << QString::fromStdString(outputPath);
    } else {
        args << "-mode.raster.out" << QString::fromStdString(outputPath) << "uint32";
    }

    return args;
}

} // namespace sicnu::operators::otb
