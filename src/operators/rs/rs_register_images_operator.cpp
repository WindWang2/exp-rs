/***************************************************************************
 * rs_register_images_operator.cpp — F13
 ***************************************************************************/
#include "rs_register_images_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/geometric_transform.h"
#include "processing/algorithms/registration/multimodal_matcher.h"
#include "processing/algorithms/registration/registration_quality.h"
#include "processing/algorithms/resampler.h"

#include <gdal_priv.h>

#include <QFile>
#include <QString>

#include <algorithm>
#include <memory>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_metric = {
    "auto", "phase_correlation", "mutual_information", "ncc"};
const std::vector<std::string> s_resampling = {"nearest", "bilinear", "cubic", "lanczos"};

struct DatasetCloser {
    void operator()(GDALDataset* dataset) const
    {
        if (dataset)
            GDALClose(dataset);
    }
};

std::unique_ptr<GDALDataset, DatasetCloser> openReadOnly(const std::string& path)
{
    GDALAllRegister();
    return std::unique_ptr<GDALDataset, DatasetCloser>(
        static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly)));
}

/// Bounded band-1 reader with the dataset's geotransform. Reports the RAW
/// raster dims alongside the decimated buffer dims so callers can rescale
/// geotransforms for the buffer grid.
bool readBand1Bounded(GDALDataset* dataset, int maxDim, int& width, int& height,
                      std::vector<float>& buffer, double geoTransform[6], int& rawW, int& rawH)
{
    rawW = dataset->GetRasterXSize();
    rawH = dataset->GetRasterYSize();
    const int longest = std::max(rawW, rawH);
    width = longest > maxDim ? std::max(1, rawW * maxDim / longest) : rawW;
    height = longest > maxDim ? std::max(1, rawH * maxDim / longest) : rawH;
    dataset->GetGeoTransform(geoTransform);
    buffer.assign(static_cast<std::size_t>(width) * height, 0.0f);
    return dataset->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, rawW, rawH, buffer.data(), width,
                                               height, GDT_Float32, 0, 0)
           == CE_None;
}

::rs::algorithms::ResampleMethod parseResampling(const std::string& name)
{
    if (name == "nearest")
        return ::rs::algorithms::ResampleMethod::NearestNeighbor;
    if (name == "cubic")
        return ::rs::algorithms::ResampleMethod::CubicConvolution;
    if (name == "lanczos")
        return ::rs::algorithms::ResampleMethod::Lanczos;
    return ::rs::algorithms::ResampleMethod::Bilinear;
}

} // namespace

Json::Value RsRegisterImagesOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["source"] = makeStringParam("source", "Moving raster (band 1) to register", "");
    props["reference"] = makeStringParam("reference", "Reference raster (defines output grid)", "");
    props["output"] = makeOutputParam("output", "Registered output GeoTIFF (Float32)", "tif");
    props["reportPath"] = makeStringParam(
        "reportPath", "Optional JSON quality report sidecar "
                      "(exp_rs_registration_quality/1)",
        "");
    props["metric"] = makeEnumParam("metric", "Cross-modal similarity metric", s_metric, "auto");
    props["maxDim"] = makeIntegerParam(
        "maxDim", "Long-side processing cap in px (bounded memory)", 1024);
    props["resampling"] = makeEnumParam("resampling", "Warp resampling kernel", s_resampling,
                                        "bilinear");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Registered output GeoTIFF", "tif");
    outputs["status"] = makeStringParam("status", "success | low_confidence", "");
    outputs["reason"] = makeStringParam("reason", "Refusal/low-confidence reason code", "");
    outputs["inlierCount"] = makeIntegerParam("inlierCount", "Consensus inlier count", 0);
    outputs["inlierRmsePx"] = makeNumberParam("inlierRmsePx", "Consensus RMSE (px)", 0.0);
    outputs["coverageRatio"] = makeNumberParam("coverageRatio", "Full-extent coverage ratio", 0.0);
    outputs["rmsePx"] = makeNumberParam("rmsePx", "Quality RMSE (px)", 0.0);
    outputs["ce90Px"] = makeNumberParam("ce90Px", "Empirical CE90 (px)", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"source", "reference", "output"});
    return root;
}

Json::Value RsRegisterImagesOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("registration");
    meta["tags"].append("multimodal");
    meta["tags"].append("optical-sar");
    meta["purpose"] = "Cross-modal image registration with honest refusal semantics";
    meta["prerequisites"].append("Source and reference rasters readable by GDAL");
    meta["workflowHints"].append(
        "metric=auto runs phase correlation coarsely and mutual information finely");
    meta["workflowHints"].append(
        "A refused job writes no output; low_confidence outputs need review");
    meta["workflowHints"].append(
        "Pairwise tx/ty from this operator feeds rs:stack_register for multi-scene adjustment");
    return meta;
}

Json::Value RsRegisterImagesOperator::executionEstimate() const
{
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    // Three Float32 buffers at the maxDim cap + FFT scratch, bounded.
    const double dim = 1024.0;
    est["estimatedRamBytes"] = 3.0 * dim * dim * 4.0 + 4.0 * 1024 * 1024;
    est["temporaryDiskBytes"] = 0;
    return est;
}

Json::Value RsRegisterImagesOperator::run(const Json::Value& p, RSOperatorContext& context)
{
    const std::string sourcePath = requireString(p, "source");
    const std::string refPath = requireString(p, "reference");
    const std::string outputPath = requireString(p, "output");
    const std::string reportPath =
        p.isMember("reportPath") && p["reportPath"].isString() ? p["reportPath"].asString()
                                                               : std::string();
    const std::string metric = getEnum(p, "metric", s_metric, "auto");
    int maxDim = getInt(p, "maxDim", 1024);
    maxDim = std::clamp(maxDim, 64, 4096);
    const std::string resampling = getEnum(p, "resampling", s_resampling, "bilinear");

    context.reportProgress(0.05, "Opening rasters");
    context.throwIfCancelled();

    auto src = openReadOnly(sourcePath);
    auto ref = openReadOnly(refPath);
    if (!src || !ref)
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Failed to open source or reference raster with GDAL");

    int srcW = 0, srcH = 0, refW = 0, refH = 0;
    double refGt[6] = {0, 1, 0, 0, 0, 1};
    std::vector<float> srcData, refData;
    int rawSrcW = 0, rawSrcH = 0, rawRefW = 0, rawRefH = 0;
    double srcGtUnused[6] = {0, 1, 0, 0, 0, 1};
    if (!readBand1Bounded(src.get(), maxDim, srcW, srcH, srcData, srcGtUnused, rawSrcW, rawSrcH)
        || !readBand1Bounded(ref.get(), maxDim, refW, refH, refData, refGt, rawRefW, rawRefH))
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Failed to read band 1 of source or reference raster");
    const std::string refProjection = [&] {
        const char* proj = ref->GetProjectionRef();
        return (proj && *proj) ? std::string(proj) : std::string();
    }();
    src.reset();
    ref.reset();

    context.reportProgress(0.25, "Cross-modal matching");
    context.throwIfCancelled();

    sicnu::registration::MultimodalMatchOptions options;
    if (metric == "phase_correlation")
        options.metric = sicnu::registration::MatchMetric::PhaseCorrelation;
    else if (metric == "mutual_information")
        options.metric = sicnu::registration::MatchMetric::MutualInformation;
    else if (metric == "ncc")
        options.metric = sicnu::registration::MatchMetric::NormalizedCrossCorrelation;

    const auto match = sicnu::registration::MultimodalMatcher::matchImages(
        srcData.data(), srcW, srcH, refData.data(), refW, refH, options);

    if (match.status == sicnu::registration::RegistrationStatus::Refused) {
        // Fail closed: a refusal writes nothing.
        throw RSOperatorError(
            ErrorCode::ComputationError,
            "Registration refused (" + match.reason.toStdString() + "); no output written");
    }

    context.reportProgress(0.65, "Fitting transform and warping");
    context.throwIfCancelled();

    // Affine fit over the consensus inliers (backward map for the warp).
    std::vector<std::pair<double, double>> fitSrc, fitDst;
    for (const auto& pt : match.points) {
        if (!pt.inlier)
            continue;
        fitSrc.emplace_back(pt.srcX, pt.srcY);
        fitDst.emplace_back(pt.dstX, pt.dstY);
    }
    if (fitSrc.size() < 3)
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Fewer than three consensus inliers; no output written");
    const auto transform =
        ::rs::algorithms::GeometricTransform::solve(::rs::algorithms::TransformModel::Affine, fitSrc,
                                                  fitDst);
    if (!transform.success)
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Post-consensus affine fit failed; no output written");

    // The fitted affine lives in DECIMATED-BUFFER pixel space (the same space
    // the matcher used). The warp therefore runs with identity geotransforms —
    // buffer pixel in, buffer pixel out — and the output file carries the
    // DECIMATION-AWARE reference geotransform so world coordinates stay exact
    // (P0 review finding: mixing pixel-space fits with world-space gts
    // silently warped products; buffer-pixel space keeps one frame).
    double refGtBuf[6] = {refGt[0], refGt[1] * static_cast<double>(rawRefW) / refW, refGt[2],
                          refGt[3], refGt[5] * static_cast<double>(rawRefH) / refH, refGt[4]};
    const double identityGt[6] = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

    std::vector<float> outData(static_cast<std::size_t>(refW) * refH, 0.0f);
    ::rs::algorithms::WarpOptions warp;
    warp.method = parseResampling(resampling);
    // The product keeps the source radiometry: no [0,1] clamping (the
    // WarpOptions default range is for normalized rasters only).
    warp.clampRange = false;
    if (!::rs::algorithms::Resampler::warpRaster(srcData.data(), srcW, srcH, identityGt,
                                                 outData.data(), refW, refH, identityGt,
                                                 [&transform](double x, double y) {
                                                     return ::rs::algorithms::GeometricTransform::
                                                         applyBackward(transform, x, y);
                                                 },
                                                 warp))
        throw RSOperatorError(ErrorCode::ComputationError, "Warp failed; no output written");

    // Write output GeoTIFF on the reference grid.
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver)
        throw RSOperatorError(ErrorCode::ComputationError, "GTiff driver unavailable");
    const QString outQ = QString::fromStdString(outputPath);
    GDALDataset* outDs = driver->Create(outQ.toUtf8().constData(), refW, refH, 1, GDT_Float32,
                                        nullptr);
    if (!outDs)
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Cannot create output raster: " + outputPath);
    outDs->SetGeoTransform(refGtBuf);
    if (!refProjection.empty())
        outDs->SetProjection(refProjection.c_str());
    if (outDs->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, refW, refH, outData.data(), refW, refH,
                                          GDT_Float32, 0, 0)
        != CE_None) {
        GDALClose(outDs);
        throw RSOperatorError(ErrorCode::ComputationError, "Failed to write output raster");
    }
    GDALClose(outDs);

    context.reportProgress(0.9, "Quality report");
    // Points live in the decimated SOURCE pixel grid; the quality products
    // must anchor to that extent (P2 review finding).
    const auto quality = sicnu::registration::RegistrationQuality::evaluate(
        match.points, srcW, srcH, match.coverageRatio);
    const auto field =
        sicnu::registration::RegistrationQuality::residualField(match.points, srcW, srcH, 8);

    if (!reportPath.empty()) {
        const QJsonObject doc = sicnu::registration::RegistrationQuality::toJson(
            quality, field, sicnu::registration::statusToString(match.status), match.reason);
        if (!sicnu::registration::RegistrationQuality::writeReportAtomic(
                QString::fromStdString(reportPath), doc))
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "Failed to write the quality report sidecar (output raster "
                                  "was already written)");
    }

    context.reportProgress(1.0, "Registration complete");
    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["status"] = sicnu::registration::statusToString(match.status).toStdString();
    result["reason"] = match.reason.toStdString();
    result["inlierCount"] = match.inlierCount;
    result["inlierRmsePx"] = match.inlierRmse;
    result["coverageRatio"] = match.coverageRatio;
    result["rmsePx"] = quality.rmse;
    result["ce90Px"] = quality.ce90;
    result["matchGrid"] = std::to_string(srcW) + "x" + std::to_string(srcH) + " -> "
                          + std::to_string(refW) + "x" + std::to_string(refH);
    return result;
}

} // namespace sicnu::operators::rs
