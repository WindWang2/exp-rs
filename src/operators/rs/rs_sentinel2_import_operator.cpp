/***************************************************************************
 * rs_sentinel2_import_operator.cpp
 ***************************************************************************/
#include "rs_sentinel2_import_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/satellite_products.h"

#include <QString>
#include <QStringList>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_resolutions = {"10m", "20m", "60m"};

QStringList parseBandList(const Json::Value& p, const std::string& resolution)
{
    QStringList names;
    if (p.isMember("bands") && p["bands"].isArray() && !p["bands"].empty()) {
        for (Json::ArrayIndex i = 0; i < p["bands"].size(); ++i) {
            if (p["bands"][i].isString())
                names << QString::fromStdString(p["bands"][i].asString());
        }
        return names;
    }
    if (resolution == "20m")
        return SatelliteProducts::defaultSentinel2Bands20m();
    if (resolution == "60m")
        return {QStringLiteral("B1"), QStringLiteral("B9")};
    return SatelliteProducts::defaultSentinel2Bands10m();
}

} // namespace

Json::Value RsSentinel2ImportOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeStringParam("input", "Path to .SAFE directory or MTD_MSIL*.xml", "");
    props["output"] = makeOutputParam("output", "Output multi-band GeoTIFF", "tif");
    props["resolution"] = makeEnumParam("resolution", "Band resolution folder (L2A)",
                                        s_resolutions, "10m");
    Json::Value bands = makeStringParam("bands", "Optional band list (e.g. B2,B3,B4,B8)", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "string";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Stacked GeoTIFF", "tif");
    outputs["productId"] = makeStringParam("productId", "Sentinel-2 product id", "");
    outputs["bandCount"] = makeIntegerParam("bandCount", "Number of stacked bands", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSentinel2ImportOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("sentinel-2");
    meta["tags"].append("import");
    meta["tags"].append("data-format");
    meta["purpose"] = "Convert a Sentinel-2 SAFE product into analysis-ready multi-band GeoTIFF";
    meta["prerequisites"].append("Unzipped .SAFE tree with GRANULE/IMG_DATA rasters");
    meta["workflowHints"].append("Use resolution=10m with B2–B4,B8 for NDVI / true-colour teaching labs");
    return meta;
}

Json::Value RsSentinel2ImportOperator::run(const Json::Value& p, RSOperatorContext& context)
{
    const std::string inputPath = requireString(p, "input");
    const std::string outputPath = requireString(p, "output");
    const std::string resolution = getEnum(p, "resolution", s_resolutions, "10m");
    const QStringList bandNames = parseBandList(p, resolution);

    context.reportProgress(0.05, "Discovering Sentinel-2 product (" + resolution + ")");
    SatelliteProducts::ProductInfo product;
    QString err;
    if (!SatelliteProducts::discoverSentinel2(QString::fromStdString(inputPath), &product,
                                              QString::fromStdString(resolution), &err)) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              err.isEmpty() ? "Sentinel-2 discovery failed" : err.toStdString());
    }

    context.logInfo("Sentinel-2 product: " + product.productId.toStdString()
                    + " (" + product.processingLevel.toStdString() + ", "
                    + std::to_string(product.bands.size()) + " band files @ " + resolution + ")");
    context.throwIfCancelled();

    context.reportProgress(0.15, "Stacking bands to GeoTIFF");
    const bool ok = SatelliteProducts::stackToGeoTiff(
        product, bandNames, QString::fromStdString(outputPath), &err,
        [&](double frac, const QString& msg) {
            context.reportProgress(0.15 + 0.8 * frac, msg.toStdString());
            context.throwIfCancelled();
        });

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              err.isEmpty() ? "Failed to stack Sentinel-2 bands" : err.toStdString());
    }

    context.reportProgress(1.0, "Sentinel-2 import complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["productId"] = product.productId.toStdString();
    result["spacecraft"] = product.spacecraft.toStdString();
    result["processingLevel"] = product.processingLevel.toStdString();
    result["acquisitionDate"] = product.acquisitionDate.toStdString();
    result["resolution"] = resolution;
    result["bandCount"] = static_cast<int>(bandNames.size());
    result["bands"] = Json::Value(Json::arrayValue);
    for (const QString& b : bandNames)
        result["bands"].append(b.toStdString());
    return result;
}

} // namespace sicnu::operators::rs
