/***************************************************************************
 * rs_sar_backscatter_operator.cpp — SAR backscatter conversion (Platform 3.0)
 ***************************************************************************/
#include "rs_sar_backscatter_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_calibration.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_block_stream.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_domains = { "linear_power", "db" };
const std::vector<std::string> s_states = { "sigma0", "gamma0", "beta0" };
const std::vector<std::string> s_fromStates = { "sigma0", "gamma0", "beta0", "dn" };
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

} // anonymous namespace

Json::Value RsSarBackscatterOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input SAR raster (sigma0/gamma0/beta0, linear power or dB)");
    props["output"] = makeOutputParam("output", "Output converted raster (Float32)", "tif");
    props["band"] = makeIntegerParam("band", "1-based input band", 1);
    props["fromCalibration"] = makeEnumParam("fromCalibration", "Radiometric state of the input", s_fromStates, "sigma0");
    props["toCalibration"] = makeEnumParam("toCalibration", "Target radiometric state", s_states, "gamma0");
    props["inputDomain"] = makeEnumParam("inputDomain", "Numeric domain of the input", s_domains, "linear_power");
    props["outputDomain"] = makeEnumParam("outputDomain", "Numeric domain of the output", s_domains, "linear_power");
    props["incidenceDeg"] = makeNumberParam("incidenceDeg", "Constant scene incidence angle in degrees (required > 0 for state conversions without incidenceRaster)", 0.0);
    props["incidenceRaster"] = makeRasterParam("incidenceRaster", "Optional per-pixel local incidence angle raster in degrees (band 1, same grid as input)", false);
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the output", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the output", "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Converted raster path");
    outputs["calibration"] = makeStringParam("calibration", "Calibration state of the output (toCalibration)");
    outputs["domain"] = makeStringParam("domain", "Output numeric domain");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSarBackscatterOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("backscatter");
    meta["tags"].append("radiometry");
    meta["purpose"] = "Convert SAR backscatter between radiometric states (sigma0/gamma0/"
                      "beta0) and/or numeric domains (linear power <-> dB) with a constant "
                      "scene incidence angle or a per-pixel incidence raster.";
    meta["workflowHints"].append("Chain from rs:sar_calibrate when the input is still "
                                 "digital numbers (DN).");
    meta["workflowHints"].append("For DEM-based local incidence angles (terrain-flattened "
                                 "gamma0) use rs:sar_terrain_correction instead.");
    meta["limitations"].append("beta0 needs the incidence angle from geometry: set "
                               "incidenceDeg > 0 or provide incidenceRaster.");
    meta["limitations"].append("DN input is unsupported; calibrate first with "
                               "rs:sar_calibrate.");
    meta["limitations"].append("inputDomain=db cannot be combined with a calibration-state "
                               "conversion; convert the numeric domain in a separate step.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarBackscatterOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarBackscatterOperator::run(const Json::Value& params,
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
    const std::string fromStr = getEnum(params, "fromCalibration", s_fromStates, "sigma0");
    const std::string toStr = getEnum(params, "toCalibration", s_states, "gamma0");
    const std::string inputDomainStr = getEnum(params, "inputDomain", s_domains, "linear_power");
    const std::string outputDomainStr = getEnum(params, "outputDomain", s_domains, "linear_power");
    const double incidenceDeg = getDouble(params, "incidenceDeg", 0.0);
    const std::string incidenceRaster = getString(params, "incidenceRaster", "");
    const QString polarizations =
        QString::fromStdString( getString( params, "polarizations", "" ) );
    const QString sensor = QString::fromStdString( getString( params, "sensor", "" ) );

    if (fromStr == "dn") {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "fromCalibration=dn is not supported here; calibrate "
                              "digital numbers first with rs:sar_calibrate");
    }
    const sicnu::sar::SarDomain inputDomain = inputDomainStr == "db"
                                                  ? sicnu::sar::SarDomain::Decibels
                                                  : sicnu::sar::SarDomain::LinearPower;
    const sicnu::sar::SarDomain outputDomain = outputDomainStr == "db"
                                                   ? sicnu::sar::SarDomain::Decibels
                                                   : sicnu::sar::SarDomain::LinearPower;

    const bool sameState = fromStr == toStr;
    if (!sameState && inputDomain == sicnu::sar::SarDomain::Decibels) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "inputDomain=db cannot be converted across calibration "
                              "states; convert the domain first with rs:sar_calibrate "
                              "or run this operator twice");
    }
    if (!sameState && incidenceDeg <= 0.0 && incidenceRaster.empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "geometry required: set incidenceDeg > 0 or provide an "
                              "incidenceRaster when converting between calibration states");
    }
    if (!incidenceRaster.empty() && !fileExists(incidenceRaster)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Incidence raster not found: " + incidenceRaster);
    }

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }

    if (band < 1 || band > src.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "band out of range: " + std::to_string(band));
    }

    // Sentinel declared on the analysis band (NaN when undeclared).
    bool hasNodata = false;
    const double nodataRaw = src.bandNoDataValue(band, &hasNodata);
    const float nodata = hasNodata ? static_cast<float>(nodataRaw) : kNan;

    context.reportProgress(0.05, sameState ? "Converting numeric domain"
                                           : "Converting backscatter calibration");
    GdalStreamingOutput dst(QString::fromStdString(outputPath), src.width(), src.height(),
                            1, GDT_Float32, src.geoTransform(), src.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create output raster");
    }
    dst.setNoDataValue(kNan);

    bool ok = true;
    if (sameState) {
        // Pure numeric-domain conversion: no geometry needed, stream the
        // per-pixel domain math directly.
        GdalBlockStream stream(src, band, 256, 256, 0);
        ok = stream.forEach([&](const GdalBlockStream::Tile &tile, const float *pixels) {
            context.throwIfCancelled();
            std::vector<float> out(static_cast<size_t>(tile.width) * tile.height);
            for (int y = 0; y < tile.height; ++y) {
                for (int x = 0; x < tile.width; ++x) {
                    const float v = pixels[y * tile.width + x];
                    const size_t idx = static_cast<size_t>(y) * tile.width + x;
                    if (!std::isfinite(v) || v == nodata) {
                        out[idx] = kNan;
                        continue;
                    }
                    const double linear = inputDomain == sicnu::sar::SarDomain::Decibels
                                              ? sicnu::sar::dbToLinear(v)
                                              : static_cast<double>(v);
                    out[idx] = outputDomain != sicnu::sar::SarDomain::Decibels
                                   ? static_cast<float>(linear)
                                   : (linear > 0.0
                                          ? static_cast<float>(sicnu::sar::linearToDb(linear))
                                          : kNan);
                }
            }
            context.reportProgress(0.1 + 0.85 * (tile.index + 1) / tile.totalTiles,
                                   "Converting numeric domain");
            return dst.writeTile(1, tile, out.data());
        });
        if (ok) {
            sicnu::sar::writeSarOutputMetadata(dst, QString::fromStdString(toStr),
                                               sicnu::sar::sarDomainToString(outputDomain),
                                               polarizations, sensor, incidenceDeg, 0.0);
        }
    } else {
        sicnu::sar::BackscatterConvertOptions options;
        options.fromCalibration = QString::fromStdString(fromStr);
        options.toCalibration = QString::fromStdString(toStr);
        options.outputDomain = outputDomain;
        options.constantIncidenceDeg = incidenceDeg;
        options.incidenceRasterPath = QString::fromStdString(incidenceRaster);
        ok = sicnu::sar::convertBackscatterRaster(src, band, options, nodata, dst, 256,
                                                  polarizations, sensor);
    }
    if (!ok) {
        dst.abandon();
        throw RSOperatorError(ErrorCode::GdalError,
                              sameState ? "SAR domain conversion failed while streaming"
                                        : "SAR backscatter conversion failed while streaming");
    }
    // Radiometric state in the shared vocabulary.
    dst.setMetadataItem("SICNU_RADIOMETRIC_STATE", QString::fromStdString(toStr));

    QString error;
    if (!dst.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to finalize output: " +
                                                        error.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["calibration"] = toStr;
    result["domain"] = outputDomainStr;
    result["bands"] = 1;
    context.reportProgress(1.0, "SAR backscatter conversion complete");
    return result;
}

} // namespace sicnu::operators::rs
