/***************************************************************************
 * rs_hj_import_operator.cpp
 ***************************************************************************/
#include "rs_hj_import_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_schema.h"
#include "rs_cn_import_operator.h"

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsHjImportOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeStringParam(
        "input", "HJ-1A/1B CCD product directory, sidecar XML or image TIFF", "");
    props["output"] = makeOutputParam("output", "Output multi-band GeoTIFF", "tif");
    Json::Value bands = makeStringParam(
        "bands", "Optional band list (default: all bands declared by the sidecar)", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "string";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Stacked GeoTIFF", "tif");
    outputs["productId"] = makeStringParam("productId", "HJ-1 product id", "");
    outputs["bandCount"] = makeIntegerParam("bandCount", "Number of stacked bands", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsHjImportOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("hj-1");
    meta["tags"].append("cn-satellite");
    meta["tags"].append("import");
    meta["tags"].append("data-format");
    meta["purpose"] = "Convert a HJ-1A/1B CCD L1A product into analysis-ready multi-band GeoTIFF";
    meta["prerequisites"].append("Product directory with CRESDA sidecar XML and TIFF (offline)");
    meta["workflowHints"].append("Stack B1-B4 then run rs:spectral_index for NDVI (30 m CCD grids)");
    return meta;
}

Json::Value RsHjImportOperator::executionEstimate() const
{
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 12582912; // 1 x 1024x1024 Float32 band + 8 MiB fixed
    return est;
}

Json::Value RsHjImportOperator::run(const Json::Value& p, RSOperatorContext& context)
{
    const std::string inputPath = requireString(p, "input");
    const std::string outputPath = requireString(p, "output");

    context.reportProgress(0.05, "Identifying HJ-1 CCD product");
    const CnImportProduct product =
        resolveCnImportProduct(inputPath, sicnu::geo::ProductKind::HjCcdProduct, "hj_ccd_product");
    context.logInfo("HJ-1 product: " + product.metadata.productId
                    + " (" + product.metadata.platform + "/" + product.metadata.sensor
                    + ", " + std::to_string(product.bandNames.size()) + " declared bands)");
    context.throwIfCancelled();

    const SatelliteProducts::ProductInfo info = buildCnProductInfo(product);
    const bool explicitBands = p.isMember("bands") && p["bands"].isArray() && !p["bands"].empty();
    QStringList requested;
    if (explicitBands) {
        for (Json::ArrayIndex i = 0; i < p["bands"].size(); ++i) {
            if (p["bands"][i].isString())
                requested << QString::fromStdString(p["bands"][i].asString());
        }
    } else {
        requested = product.bandNames;
    }

    const QStringList missing = SatelliteProducts::unresolvableBands(info, requested);
    if (!missing.isEmpty()) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              ("Requested bands not found in product (missingBands: "
                               + missing.join(QStringLiteral(", ")) + ")")
                                  .toStdString());
    }

    context.reportProgress(0.15, "Stacking bands to GeoTIFF");
    QString err;
    const bool ok = SatelliteProducts::stackToGeoTiff(
        info, requested, QString::fromStdString(outputPath), &err,
        [&](double frac, const QString& msg) {
            context.reportProgress(0.15 + 0.75 * frac, msg.toStdString());
            context.throwIfCancelled();
        });
    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              err.isEmpty() ? "Failed to stack HJ-1 bands" : err.toStdString());
    }

    context.reportProgress(0.95, "Writing HJ-1 metadata");
    if (!writeCnImportMetadata(QString::fromStdString(outputPath), product.metadata,
                               requested, QString::fromStdString(product.identity.kindName), &err)) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              err.isEmpty() ? "Failed to write HJ-1 import metadata"
                                            : err.toStdString());
    }

    context.reportProgress(1.0, "HJ-1 import complete");
    return cnImportResult(product, outputPath, requested);
}

} // namespace sicnu::operators::rs
