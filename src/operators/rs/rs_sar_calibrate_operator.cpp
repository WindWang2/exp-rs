/***************************************************************************
 * rs_sar_calibrate_operator.cpp — SAR radiometric calibration (Platform 3.0)
 ***************************************************************************/
#include "rs_sar_calibrate_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_calibration.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <limits>
#include <string>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_domains = { "linear_power", "db" };

Json::Value makeSarInputContract() {
    Json::Value c(Json::objectValue);
    c["modality"] = "sar";
    return c;
}

} // anonymous namespace

Json::Value RsSarCalibrateOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input SAR raster (DN or amplitude)");
    props["input"]["x-rs-contract"] = makeSarInputContract();
    props["output"] = makeOutputParam("output", "Output calibrated sigma0 raster (Float32)", "tif");
    props["band"] = makeIntegerParam("band", "1-based input band (0 = all bands calibrated independently)", 1);
    props["calibrationA"] = makeNumberParam("calibrationA", "Calibration constant A (sigma0 = DN²/A²; use the product's A value)", 1.0);
    props["noiseLinear"] = makeNumberParam("noiseLinear", "Additive noise power to subtract before scaling (linear, 0 disables)", 0.0);
    props["outputDomain"] = makeEnumParam("outputDomain", "Output numeric domain", s_domains, "linear_power");
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the output", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the output", "");
    props["incidenceDeg"] = makeNumberParam("incidenceDeg", "Scene incidence angle (degrees) recorded as metadata", 0.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Calibrated raster path");
    outputs["calibration"] = makeStringParam("calibration", "Calibration state of the output (sigma0)");
    outputs["domain"] = makeStringParam("domain", "Output numeric domain");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSarCalibrateOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("calibration");
    meta["tags"].append("radiometry");
    meta["purpose"] = "Convert SAR digital numbers to calibrated sigma0 backscatter "
                      "in linear power or dB with an explicit, recorded numeric domain.";
    meta["prerequisites"].append("SAR amplitude/DN raster; the calibration constant A must "
                                 "match the product convention (Sentinel-1 GRD: per-beam "
                                 "constant from the annotation, simplified here to one "
                                 "constant per run).");
    meta["workflowHints"].append("Calibrate before speckle filtering or change detection: "
                                 "rs:sar_calibrate -> rs:sar_speckle.");
    meta["workflowHints"].append("dB output is 10·log10(power); nonpositive power becomes NoData.");
    meta["limitations"].append("LUT-based calibration (per-block/per-pixel annotation LUTs) is "
                               "not applied; use a constant A or pre-calibrated input.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarCalibrateOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarCalibrateOperator::run(const Json::Value& params,
                                        RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    const int band = getInt(params, "band", 1);
    const double calibrationA = getDouble(params, "calibrationA", 1.0);
    const double noiseLinear = getDouble(params, "noiseLinear", 0.0);
    const std::string domainStr = getEnum(params, "outputDomain", s_domains, "linear_power");
    const sicnu::sar::SarDomain domain = domainStr == "db"
                                             ? sicnu::sar::SarDomain::Decibels
                                             : sicnu::sar::SarDomain::LinearPower;
    const QString polarizations =
        QString::fromStdString( getString( params, "polarizations", "" ) );
    const QString sensor = QString::fromStdString( getString( params, "sensor", "" ) );
    const double incidenceDeg = getDouble(params, "incidenceDeg", 0.0);
    if (calibrationA <= 0.0) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "calibrationA must be > 0 (sigma0 = DN²/A²)");
    }

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }

    const int bandCount = band > 0 ? 1 : src.bandCount();
    const int firstBand = band > 0 ? band : 1;
    if (firstBand < 1 || firstBand > src.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "band out of range: " + std::to_string(firstBand));
    }

    // Sentinel declared on the analysis band (NaN when undeclared).
    bool hasNodata = false;
    const double nodataRaw = src.bandNoDataValue(firstBand, &hasNodata);
    const float nodata = hasNodata ? static_cast<float>(nodataRaw)
                                   : std::numeric_limits<float>::quiet_NaN();

    context.reportProgress(0.05, "Calibrating SAR raster");
    GdalStreamingOutput dst(QString::fromStdString(outputPath), src.width(), src.height(),
                            bandCount, GDT_Float32, src.geoTransform(), src.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create output raster");
    }
    dst.setNoDataValue(std::numeric_limits<float>::quiet_NaN());

    // Single-band fast path streams through the calibrated kernel directly;
    // multi-band runs calibrate each band independently (same constants).
    bool ok = true;
    for (int b = 0; b < bandCount && ok; ++b) {
        context.throwIfCancelled();
        ok = sicnu::sar::calibrateRaster(src, firstBand + b, calibrationA, noiseLinear, domain,
                                         nodata, dst, 256, b + 1, polarizations, sensor,
                                         incidenceDeg, 0.0);
        context.reportProgress(0.1 + 0.85 * (b + 1) / bandCount, "Calibrated band");
    }
    if (!ok) {
        dst.abandon();
        throw RSOperatorError(ErrorCode::GdalError, "SAR calibration failed while streaming");
    }
    // Radiometric state in the shared vocabulary.
    dst.setMetadataItem("SICNU_RADIOMETRIC_STATE", "sigma0");

    QString error;
    if (!dst.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to finalize output: " +
                                                        error.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["calibration"] = "sigma0";
    result["domain"] = domainStr;
    result["bands"] = bandCount;
    context.reportProgress(1.0, "SAR calibration complete");
    return result;
}

} // namespace sicnu::operators::rs
