/***************************************************************************
 * rs_terrain_analysis_operator.cpp  —  Terrain analysis RSOperator
 ***************************************************************************/
#include "rs_terrain_analysis_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/terrain_analysis.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_products = {
    "slope", "aspect", "hillshade", "roughness", "tri", "tpi"
};

} // anonymous namespace

Json::Value RsTerrainAnalysisOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input DEM raster");
    props["output"] = makeOutputParam("output", "Output terrain product raster", "tif");
    props["product"] = makeEnumParam("product", "Terrain product to compute", s_products, "slope");
    props["cellSize"] = makeNumberParam("cellSize", "Pixel size in map units", 30.0);
    props["nodata"] = makeNumberParam("nodata", "DEM no-data value", -9999.0);
    props["sunAzimuth"] = makeNumberParam("sunAzimuth", "Sun azimuth for hillshade (degrees)", 315.0);
    props["sunElevation"] = makeNumberParam("sunElevation", "Sun elevation for hillshade (degrees)", 45.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["product"] = makeStringParam("product", "Computed product", "");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "product"});
    return root;
}

Json::Value RsTerrainAnalysisOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("terrain");
    meta["tags"].append("dem");
    meta["tags"].append("slope");
    meta["tags"].append("hillshade");
    meta["purpose"] = "Derive terrain parameters from elevation data.";
    meta["workflowHints"].append("Use slope/aspect for landform classification; hillshade for visualization.");
    return meta;
}

Json::Value RsTerrainAnalysisOperator::executionEstimate() const
{
    // FullRaster (default policy): the whole DEM band plus one Float32 output
    // band are resident; the 3x3-window kernels use no extra full-raster state.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 12582912; // 2 x 1024x1024 Float32 buffers + 4 MiB fixed
    return est;
}

Json::Value RsTerrainAnalysisOperator::run(const Json::Value& params,
                                           RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string product = getEnum(params, "product", s_products, "slope");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input DEM not found: " + inputPath);
    }

    const float sunAzimuth = static_cast<float>(getDouble(params, "sunAzimuth", 315.0));
    const float sunElevation = static_cast<float>(getDouble(params, "sunElevation", 45.0));

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input DEM: " + inputPath);
    }

    float cellSize = 30.0f;
    if (params.isMember("cellSize") && params["cellSize"].isNumeric()) {
        cellSize = static_cast<float>(params["cellSize"].asDouble());
    } else {
        const std::array<double, 6> gt = ds.geoTransform();
        if (std::abs(gt[1]) > 1e-7) {
            cellSize = static_cast<float>(std::abs(gt[1]));
        }
    }

    float nodata = -9999.0f;
    if (params.isMember("nodata") && params["nodata"].isNumeric()) {
        nodata = static_cast<float>(params["nodata"].asDouble());
    } else {
        bool hasNodata = false;
        double dsNodata = ds.bandNoDataValue(1, &hasNodata);
        if (hasNodata && std::isfinite(dsNodata)) {
            nodata = static_cast<float>(dsNodata);
        }
    }

    const int width = ds.width();
    const int height = ds.height();

    context.logInfo("Computing " + product + " from " + inputPath + " (cellSize=" + std::to_string(cellSize) + ")");
    context.reportProgress(0.2, "Reading DEM");

    std::vector<float> dem;
    dem.resize(static_cast<size_t>(width) * height);
    if (!ds.readBandData(1, dem.data(), width, height)) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to read DEM band 1");
    }

    std::vector<float> out;
    out.resize(dem.size());

    context.reportProgress(0.5, "Computing " + product);

    bool ok = false;
    if (product == "slope") {
        ok = TerrainAnalysis::slope(dem.data(), out.data(), width, height, cellSize, nodata);
    } else if (product == "aspect") {
        ok = TerrainAnalysis::aspect(dem.data(), out.data(), width, height, cellSize, nodata);
    } else if (product == "hillshade") {
        ok = TerrainAnalysis::hillshade(dem.data(), out.data(), width, height, cellSize, nodata,
                                        sunAzimuth, sunElevation);
    } else if (product == "roughness") {
        ok = TerrainAnalysis::roughness(dem.data(), out.data(), width, height, nodata);
    } else if (product == "tri") {
        ok = TerrainAnalysis::tri(dem.data(), out.data(), width, height, nodata);
    } else if (product == "tpi") {
        ok = TerrainAnalysis::tpi(dem.data(), out.data(), width, height, nodata);
    }

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Terrain analysis computation failed");
    }

    context.throwIfCancelled();
    context.reportProgress(0.8, "Writing output raster");

    std::vector<std::vector<float>> bands = {std::move(out)};
    QString errorMessage;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, bands,
                         ds.geoTransform(), ds.projection(), &errorMessage)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + errorMessage.toStdString());
    }

    ds.close();

    context.reportProgress(1.0, product + " complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["product"] = product;
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace sicnu::operators::rs
