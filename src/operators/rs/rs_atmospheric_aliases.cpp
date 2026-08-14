/***************************************************************************
 * rs_atmospheric_aliases.cpp  —  Atomic atmospheric correction operators
 ***************************************************************************/
#include "rs_atmospheric_aliases.h"
#include "rs_atmospheric_correction_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

namespace sicnu::operators::rs {

using namespace params;

// ============================================================================
// RsDnToRadianceOperator
// ============================================================================

Json::Value RsDnToRadianceOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input optical raster (raw Digital Numbers)");
    props["output"] = makeOutputParam("output", "Output spectral radiance raster", "tif");
    props["band"] = makeIntegerParam("band", "1-based band number", 1);
    props["metadata_path"] = makeStringParam("metadata_path",
        "Path to Landsat *_MTL.txt or Sentinel-2 MTD_*.xml (optional; auto-detected when omitted)", "");
    props["gain"] = makeNumberParam("gain", "Radiance gain (optional; resolved from metadata when omitted)", 1.0);
    props["bias"] = makeNumberParam("bias", "Radiance bias (optional; resolved from metadata when omitted)", 0.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method (dn_to_radiance)", "dn_to_radiance");
    outputs["band"] = makeIntegerParam("band", "Processed band", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsDnToRadianceOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("radiometric");
    meta["tags"].append("calibration");
    meta["tags"].append("radiance");
    meta["purpose"] = "Convert raw digital numbers (DN) to spectral radiance using sensor calibration parameters.";
    meta["workflowHints"].append("First step in physical calibration before surface reflectance retrieval.");
    meta["facadeOf"] = "atmospheric_correction";
    return meta;
}

Json::Value RsDnToRadianceOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 524288;
    return est;
}

Json::Value RsDnToRadianceOperator::run(const Json::Value& params,
                                        RSOperatorContext& context) {
    return atmospheric_detail::runAtmosphericCorrectionCore("dn_to_radiance", params, context);
}

// ============================================================================
// RsAtmosphericDos1Operator
// ============================================================================

Json::Value RsAtmosphericDos1Operator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input optical raster (DN or Radiance)");
    props["output"] = makeOutputParam("output", "Output surface reflectance raster", "tif");
    props["band"] = makeIntegerParam("band", "1-based band number", 1);
    props["metadata_path"] = makeStringParam("metadata_path",
        "Path to Landsat *_MTL.txt or Sentinel-2 MTD_*.xml (optional; auto-detected when omitted)", "");
    props["gain"] = makeNumberParam("gain", "Radiance gain (optional; resolved from metadata when omitted)", 1.0);
    props["bias"] = makeNumberParam("bias", "Radiance bias (optional; resolved from metadata when omitted)", 0.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method (dos1)", "dos1");
    outputs["band"] = makeIntegerParam("band", "Processed band", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsAtmosphericDos1Operator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("atmospheric");
    meta["tags"].append("dos");
    meta["tags"].append("dos1");
    meta["tags"].append("surface-reflectance");
    meta["purpose"] = "Estimate surface reflectance using single-band Dark Object Subtraction (DOS1).";
    meta["workflowHints"].append("Apply before computing spectral indices.");
    meta["facadeOf"] = "atmospheric_correction";
    return meta;
}

Json::Value RsAtmosphericDos1Operator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 524288;
    return est;
}

Json::Value RsAtmosphericDos1Operator::run(const Json::Value& params,
                                         RSOperatorContext& context) {
    return atmospheric_detail::runAtmosphericCorrectionCore("dos1", params, context);
}

// ============================================================================
// RsAtmosphericDos2Operator
// ============================================================================

Json::Value RsAtmosphericDos2Operator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input optical raster (DN or Radiance)");
    props["output"] = makeOutputParam("output", "Output surface reflectance raster", "tif");
    props["band"] = makeIntegerParam("band", "1-based band number", 1);
    props["metadata_path"] = makeStringParam("metadata_path",
        "Path to Landsat *_MTL.txt or Sentinel-2 MTD_*.xml (optional; auto-detected when omitted)", "");
    props["gain"] = makeNumberParam("gain", "Radiance gain (optional; resolved from metadata when omitted)", 1.0);
    props["bias"] = makeNumberParam("bias", "Radiance bias (optional; resolved from metadata when omitted)", 0.0);
    props["airmass"] = makeNumberParam("airmass", "Relative airmass for DOS2 transmittance", 1.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method (dos2)", "dos2");
    outputs["band"] = makeIntegerParam("band", "Processed band", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsAtmosphericDos2Operator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("atmospheric");
    meta["tags"].append("dos");
    meta["tags"].append("dos2");
    meta["tags"].append("surface-reflectance");
    meta["purpose"] = "Estimate surface reflectance using DOS2 incorporating solar zenith angle and atmospheric transmittance.";
    meta["workflowHints"].append("Apply before computing spectral indices.");
    meta["facadeOf"] = "atmospheric_correction";
    return meta;
}

Json::Value RsAtmosphericDos2Operator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 524288;
    return est;
}

Json::Value RsAtmosphericDos2Operator::run(const Json::Value& params,
                                         RSOperatorContext& context) {
    return atmospheric_detail::runAtmosphericCorrectionCore("dos2", params, context);
}

// ============================================================================
// RsAtmosphericQuacOperator
// ============================================================================

Json::Value RsAtmosphericQuacOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band optical raster");
    props["output"] = makeOutputParam("output", "Output surface reflectance raster", "tif");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method (quac)", "quac");
    outputs["band"] = makeIntegerParam("band", "Processed band (0 = all)", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsAtmosphericQuacOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("atmospheric");
    meta["tags"].append("quac");
    meta["tags"].append("multispectral");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("surface-reflectance");
    meta["purpose"] = "Joint multi-band surface reflectance retrieval using Quick Atmospheric Correction (QUAC).";
    meta["workflowHints"].append("Processes all bands jointly using scene statistics without requiring sensor calibration files.");
    meta["facadeOf"] = "atmospheric_correction";
    return meta;
}

Json::Value RsAtmosphericQuacOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0; // full-raster
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 67108864; // ~64 MiB typical multi-band scene
    est["temporaryDiskBytes"] = 0;
    return est;
}

Json::Value RsAtmosphericQuacOperator::estimateExecution(const Json::Value& params) const {
    return atmospheric_detail::estimateAtmosphericCorrectionRam("quac", params);
}

Json::Value RsAtmosphericQuacOperator::run(const Json::Value& params,
                                          RSOperatorContext& context) {
    return atmospheric_detail::runAtmosphericCorrectionCore("quac", params, context);
}

} // namespace sicnu::operators::rs
