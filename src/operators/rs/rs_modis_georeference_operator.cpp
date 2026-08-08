/***************************************************************************
 * rs_modis_georeference_operator.cpp
 ***************************************************************************/
#include "rs_modis_georeference_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/satellite_products.h"

#include <QString>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_resampling = {
    "nearest", "bilinear", "cubic", "cubicspline", "lanczos"};

} // namespace

Json::Value RsModisGeoreferenceOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeStringParam("input", "Input MODIS raster (GeoTIFF or band)", "");
    props["output"] = makeOutputParam("output", "Georeferenced output GeoTIFF", "tif");
    props["dstCrs"] = makeStringParam(
        "dstCrs",
        "Destination CRS (EPSG:xxxx or WKT). Empty string keeps MODIS sinusoidal only.",
        "EPSG:4326");
    props["tileH"] = makeIntegerParam(
        "tileH", "MODIS horizontal tile index 0–35 (-1 = parse from filename)", -1);
    props["tileV"] = makeIntegerParam(
        "tileV", "MODIS vertical tile index 0–17 (-1 = parse from filename)", -1);
    props["resampling"] =
        makeEnumParam("resampling", "Warp resampling method", s_resampling, "bilinear");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Georeferenced GeoTIFF", "tif");
    outputs["tileH"] = makeIntegerParam("tileH", "Resolved horizontal tile", -1);
    outputs["tileV"] = makeIntegerParam("tileV", "Resolved vertical tile", -1);
    outputs["dstCrs"] = makeStringParam("dstCrs", "CRS written to output", "");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsModisGeoreferenceOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("modis");
    meta["tags"].append("georeference");
    meta["tags"].append("sinusoidal");
    meta["tags"].append("data-format");
    meta["purpose"] =
        "Place unreferenced MODIS tiles on the NASA sinusoidal grid and/or reproject";
    meta["prerequisites"].append(
        "Filename containing hXXvYY, or explicit tileH/tileV parameters");
    meta["workflowHints"].append(
        "dstCrs empty → write MODIS sinusoidal only (no resampling)");
    meta["workflowHints"].append(
        "dstCrs=EPSG:4326 for quick global overlays in teaching demos");
    return meta;
}

Json::Value RsModisGeoreferenceOperator::executionEstimate() const
{
    // FullRaster (default policy). The warp runs in-process through GDAL
    // (block-streamed); when the input needs sinusoidal assignment first, a
    // temporary Float32 copy of the input is written next to the output.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216; // ~2 warp tile buffers + 8 MiB fixed
    est["temporaryDiskBytes"] = 4194304; // temp .sinu_tmp.tif (1024x1024 Float32)
    return est;
}

Json::Value RsModisGeoreferenceOperator::run(const Json::Value& p, RSOperatorContext& context)
{
    const std::string inputPath = requireString(p, "input");
    const std::string outputPath = requireString(p, "output");
    // Allow explicit empty string to mean sinusoidal-only.
    const std::string dstCrs =
        p.isMember("dstCrs") && p["dstCrs"].isString() ? p["dstCrs"].asString()
                                                       : std::string("EPSG:4326");
    const int tileH = getInt(p, "tileH", -1);
    const int tileV = getInt(p, "tileV", -1);
    const std::string resampling = getEnum(p, "resampling", s_resampling, "bilinear");

    int resolvedH = tileH;
    int resolvedV = tileV;
    if (resolvedH < 0 || resolvedV < 0) {
        int ph = -1, pv = -1;
        if (SatelliteProducts::parseModisTileIndices(
                QString::fromStdString(inputPath), &ph, &pv)) {
            if (resolvedH < 0)
                resolvedH = ph;
            if (resolvedV < 0)
                resolvedV = pv;
        }
    }

    context.reportProgress(0.05, "Georeferencing MODIS raster");
    context.logInfo("MODIS georef: tile h"
                    + (resolvedH >= 0 ? std::to_string(resolvedH) : std::string("?"))
                    + "v"
                    + (resolvedV >= 0 ? std::to_string(resolvedV) : std::string("?"))
                    + (dstCrs.empty() ? " → sinusoidal only"
                                      : " → " + dstCrs));

    QString err;
    const bool ok = SatelliteProducts::georeferenceModis(
        QString::fromStdString(inputPath), QString::fromStdString(outputPath),
        QString::fromStdString(dstCrs), tileH, tileV, QString::fromStdString(resampling),
        &err,
        [&](double frac, const QString& msg) {
            context.reportProgress(frac, msg.toStdString());
            context.throwIfCancelled();
        });

    if (!ok) {
        throw RSOperatorError(
            ErrorCode::ComputationError,
            err.isEmpty() ? "MODIS georeference failed" : err.toStdString());
    }

    context.reportProgress(1.0, "MODIS georeference complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["tileH"] = resolvedH;
    result["tileV"] = resolvedV;
    result["dstCrs"] = dstCrs.empty() ? "MODIS Sinusoidal" : dstCrs;
    result["resampling"] = resampling;
    return result;
}

} // namespace sicnu::operators::rs
