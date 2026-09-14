/***************************************************************************
 * rs_cn_product_import_operator.cpp — unified Chinese-satellite product
 * import (ADR 0147).
 ***************************************************************************/
#include "rs_cn_product_import_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_schema.h"
#include "rs_product_import_plan.h"

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsCnProductImportOperator::schema() const
{
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeStringParam(
        "input",
        "CN product directory, CRESDA sidecar XML or image TIFF (GF-1/2/6/7, ZY-3, ZY-1 02C, "
        "HJ-1/2 CCD families; unsupported CN names are refused with a diagnosis)",
        "");
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
    outputs["productId"] = makeStringParam("productId", "Product id", "");
    outputs["bandCount"] = makeIntegerParam("bandCount", "Number of stacked bands", 0);
    outputs["sensorKey"] = makeStringParam("sensorKey", "Resolved sensor profile key", "");
    outputs["completeness"] = makeStringParam("completeness", "Constituent completeness verdict", "");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsCnProductImportOperator::metadata() const
{
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("cn-satellite");
    meta["tags"].append("gaofen");
    meta["tags"].append("zy3");
    meta["tags"].append("hj");
    meta["tags"].append("import");
    meta["tags"].append("data-format");
    meta["purpose"] = "Convert any supported Chinese satellite L1A product into "
                      "analysis-ready multi-band GeoTIFF with full provenance";
    meta["prerequisites"].append("Product directory with CRESDA sidecar XML and TIFF (offline)");
    meta["workflowHints"].append(
        "The result carries the sensor profile, sidecar generation, completeness verdict and "
        "missingDeclaredFields so agents can decide usability without re-reading the product");
    meta["workflowHints"].append(
        "apply_calibration=true converts DN to radiance only when every requested band "
        "declares gain and bias; otherwise a typed refusal names the bands");
    return meta;
}

Json::Value RsCnProductImportOperator::executionEstimate() const
{
    // FullRaster (default policy). Band stacking copies one full Float32 band
    // at a time: peak RAM is a single 1024x1024 band buffer plus fixed overhead.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 12582912; // 1 x 1024x1024 Float32 band + 8 MiB fixed
    return est;
}

Json::Value RsCnProductImportOperator::run(const Json::Value& p, RSOperatorContext& context)
{
    const std::string inputPath = requireString(p, "input");
    const std::string outputPath = requireString(p, "output");
    const bool applyCalibration = getBool(p, "apply_calibration", false);

    context.reportProgress(0.05, "Identifying Chinese satellite product");
    const ProductImportPlan plan = planCnProductImport(inputPath, nullptr);
    context.logInfo("CN product: " + plan.metadata.productId
                    + " (" + plan.metadata.platform + "/" + plan.metadata.sensor
                    + ", sensor key " + plan.sensorKey
                    + ", " + std::to_string(plan.bandNames.size()) + " bands)");
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
