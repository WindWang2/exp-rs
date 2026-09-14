/***************************************************************************
 * rs_hj_import_operator.cpp
 ***************************************************************************/
#include "rs_hj_import_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_schema.h"
#include "rs_product_import_plan.h"

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
    Json::Value applyCalibration = makeBooleanParam(
        "apply_calibration",
        "Apply declared gain/bias coefficients (radiance = DN * gain + bias); requires every "
        "requested band to declare both coefficients", false);
    applyCalibration["required"] = false;
    props["apply_calibration"] = applyCalibration;

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
    const bool applyCalibration = getBool(p, "apply_calibration", false);

    context.reportProgress(0.05, "Identifying HJ CCD product");
    const ProductImportPlan plan = planCnProductImport(inputPath, "hj_ccd_product");
    context.logInfo("HJ product: " + plan.metadata.productId
                    + " (" + plan.metadata.platform + "/" + plan.metadata.sensor
                    + ", " + std::to_string(plan.bandNames.size()) + " declared bands)");
    context.throwIfCancelled();

    const bool explicitBands = p.isMember("bands") && p["bands"].isArray() && !p["bands"].empty();
    QStringList requested;
    if (explicitBands) {
        for (Json::ArrayIndex i = 0; i < p["bands"].size(); ++i) {
            if (p["bands"][i].isString())
                requested << QString::fromStdString(p["bands"][i].asString());
        }
    } else {
        requested = plan.bandNames;
    }

    return executeCnProductImport(plan, outputPath, requested, applyCalibration, context);
}

} // namespace sicnu::operators::rs
