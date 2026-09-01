/***************************************************************************
 * rs_landsat_import_operator.cpp
 ***************************************************************************/
#include "rs_landsat_import_operator.h"

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

QStringList parseBandList(const Json::Value& p)
{
    QStringList names;
    if (!p.isMember("bands") || !p["bands"].isArray() || p["bands"].empty())
        return SatelliteProducts::defaultLandsatOpticalBands();
    for (Json::ArrayIndex i = 0; i < p["bands"].size(); ++i) {
        if (p["bands"][i].isString())
            names << QString::fromStdString(p["bands"][i].asString());
        else if (p["bands"][i].isIntegral())
            names << QStringLiteral("B%1").arg(p["bands"][i].asInt());
    }
    return names;
}

} // namespace

Json::Value RsLandsatImportOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeStringParam("input", "Path to Landsat *_MTL.txt or scene directory", "");
    props["output"] = makeOutputParam("output", "Output multi-band GeoTIFF", "tif");
    Json::Value bands = makeStringParam("bands", "Optional band list (e.g. B2,B3,B4,B5)", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "string";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Stacked GeoTIFF", "tif");
    outputs["productId"] = makeStringParam("productId", "Landsat product id", "");
    outputs["bandCount"] = makeIntegerParam("bandCount", "Number of stacked bands", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsLandsatImportOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("landsat");
    meta["tags"].append("import");
    meta["tags"].append("data-format");
    meta["purpose"] = "Convert a Landsat MTL scene into analysis-ready multi-band GeoTIFF";
    meta["prerequisites"].append("Scene directory with *_MTL.txt and band GeoTIFFs");
    meta["workflowHints"].append("Stack B2–B5 then run rs:spectral_index for NDVI");
    return meta;
}

Json::Value RsLandsatImportOperator::executionEstimate() const
{
    // FullRaster (default policy). Band stacking copies one full Float32 band at
    // a time: peak RAM is a single 1024x1024 band buffer plus fixed overhead.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 12582912; // 1 x 1024x1024 Float32 band + 8 MiB fixed
    return est;
}

Json::Value RsLandsatImportOperator::run(const Json::Value& p, RSOperatorContext& context)
{
    const std::string inputPath = requireString(p, "input");
    const std::string outputPath = requireString(p, "output");
    const QStringList bandNames = parseBandList(p);

    context.reportProgress(0.05, "Discovering Landsat product");
    SatelliteProducts::ProductInfo product;
    QString err;
    if (!SatelliteProducts::discoverLandsat(QString::fromStdString(inputPath), &product, &err)) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              err.isEmpty() ? "Landsat discovery failed" : err.toStdString());
    }

    context.logInfo("Landsat product: " + product.productId.toStdString()
                    + " (" + product.spacecraft.toStdString() + ", "
                    + std::to_string(product.bands.size()) + " files)");
    context.throwIfCancelled();

    // Fail closed when any requested band cannot be resolved (#676): the old
    // path silently dropped it, reported the requested bandCount, and shifted
    // every positional band reference downstream.
    const QStringList missing = SatelliteProducts::unresolvableBands(product, bandNames);
    if (!missing.isEmpty()) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              ("Requested bands not found in product (missingBands: "
                               + missing.join(QStringLiteral(", ")) + ")")
                                  .toStdString());
    }

    context.reportProgress(0.15, "Stacking bands to GeoTIFF");
    const bool ok = SatelliteProducts::stackToGeoTiff(
        product, bandNames, QString::fromStdString(outputPath), &err,
        [&](double frac, const QString& msg) {
            context.reportProgress(0.15 + 0.8 * frac, msg.toStdString());
            context.throwIfCancelled();
        });

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              err.isEmpty() ? "Failed to stack Landsat bands" : err.toStdString());
    }

    context.reportProgress(1.0, "Landsat import complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["productId"] = product.productId.toStdString();
    result["spacecraft"] = product.spacecraft.toStdString();
    result["processingLevel"] = product.processingLevel.toStdString();
    result["acquisitionDate"] = product.acquisitionDate.toStdString();
    result["bandCount"] = static_cast<int>(bandNames.size());
    result["bands"] = Json::Value(Json::arrayValue);
    for (const QString& b : bandNames)
        result["bands"].append(b.toStdString());
    return result;
}

} // namespace sicnu::operators::rs
