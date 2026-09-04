/***************************************************************************
 * rs_sar_terrain_correction_operator.cpp — SAR DEM terrain correction product
 ***************************************************************************/
#include "rs_sar_terrain_correction_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_terrain.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_demUnits = { "meters", "feet", "decimeters" };

Json::Value makeSarInputContract() {
    Json::Value c(Json::objectValue);
    c["modality"] = "sar";
    return c;
}

Json::Value makeDemInputContract() {
    Json::Value c(Json::objectValue);
    c["modality"] = "dem";
    c["gridRelation"] = "same-grid";
    return c;
}

} // anonymous namespace

Json::Value RsSarTerrainCorrectionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input sigma0 raster (linear power)");
    props["input"]["x-rs-contract"] = makeSarInputContract();
    props["output"] = makeOutputParam("output", "Output terrain correction raster (Float32 + optional Byte mask)", "tif");
    props["band"] = makeIntegerParam("band", "1-based input band", 1);
    props["dem"] = makeRasterParam("dem", "Co-registered DEM covering the exact same grid (radar geometry)");
    props["dem"]["x-rs-contract"] = makeDemInputContract();
    props["incidenceDeg"] = makeNumberParam("incidenceDeg", "Scene incidence angle θ0 in degrees (near-range center)", 30.0);
    props["headingDeg"] = makeNumberParam("headingDeg", "Platform heading / look azimuth φ in degrees", 0.0);
    props["demUnit"] = makeEnumParam("demUnit", "DEM elevation unit (a declared SICNU_DEM_UNIT metadata on the DEM overrides it)", s_demUnits, "meters");
    props["flagMask"] = makeBooleanParam("flagMask", "Write the Byte layover/shadow validity mask band", true);
    props["flagIncidence"] = makeBooleanParam("flagIncidence", "Write the local incidence angle band (degrees)", true);
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the output", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the output", "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Terrain correction raster path");
    outputs["calibration"] = makeStringParam("calibration", "Calibration state of band 1 (gamma0)");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands");
    outputs["incidenceBand"] = makeIntegerParam("incidenceBand", "1-based local incidence angle band (0 when disabled)");
    outputs["maskBand"] = makeIntegerParam("maskBand", "1-based validity mask band (0 when disabled)");
    outputs["layout"] = makeStringParam("layout", "Human-readable band layout description");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "dem"});
    return root;
}

Json::Value RsSarTerrainCorrectionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("terrain");
    meta["tags"].append("radiometry");
    meta["purpose"] = "Full DEM terrain correction product: terrain-flattened gamma0 "
                      "(sigma0·cosθ0/cosθi from a co-registered DEM in radar geometry) "
                      "plus an optional layover/shadow validity mask and the local "
                      "incidence angle band.";
    meta["prerequisites"].append("sigma0 raster (linear power) and a DEM on the exact "
                                 "same grid (radar geometry for GRD products), plus the "
                                 "scene incidence angle and platform heading.");
    meta["workflowHints"].append("For a plain single-band flattened gamma0 product use "
                                 "rs:sar_terrain_flatten.");
    meta["limitations"].append("Plane-fit RTC model, NOT range-Doppler terrain correction.");
    meta["limitations"].append("The DEM must be co-registered with the input in radar "
                               "geometry; no resampling is performed.");
    meta["limitations"].append("Facets with a local incidence angle >= 85° are masked as "
                               "layover/shadow.");
    meta["limitations"].append("The output includes the layover/shadow validity mask and "
                               "the local incidence angle band (both optional via "
                               "flagMask/flagIncidence).");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarTerrainCorrectionOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 8ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarTerrainCorrectionOperator::run(const Json::Value& params,
                                                RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string demPath = requireString(params, "dem");
    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }
    if (!fileExists(demPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "DEM raster not found: " + demPath);
    }

    const int band = getInt(params, "band", 1);
    const double incidenceDeg = getDouble(params, "incidenceDeg", 30.0);
    const double headingDeg = getDouble(params, "headingDeg", 0.0);
    std::string demUnit = getEnum(params, "demUnit", s_demUnits, "meters");
    const bool flagMask = getBool(params, "flagMask", true);
    const bool flagIncidence = getBool(params, "flagIncidence", true);
    const QString polarizations =
        QString::fromStdString( getString( params, "polarizations", "" ) );
    const QString sensor = QString::fromStdString( getString( params, "sensor", "" ) );

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }
    const QString declaredDomain = sicnu::sar::readDomain( src );
    if ( declaredDomain == QLatin1String( "db" ) )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "input declares SICNU_SAR_DOMAIN=db; these filters operate on "
                               "linear power — convert with rs:sar_backscatter (or rs:sar_calibrate) first" );
    if (band < 1 || band > src.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "band out of range: " + std::to_string(band));
    }

    GdalDatasetWrapper demDs;
    if (!demDs.open(QString::fromStdString(demPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open DEM raster: " + demPath);
    }
    // Grids must match exactly; no hidden resampling.
    if (src.width() != demDs.width() || src.height() != demDs.height()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "DEM and input grids differ: input is " +
                                  std::to_string(src.width()) + "x" +
                                  std::to_string(src.height()) + ", DEM is " +
                                  std::to_string(demDs.width()) + "x" +
                                  std::to_string(demDs.height()));
    }

    // A declared DEM unit overrides the parameter when present.
    const QString declaredUnit = sicnu::sar::datasetMeta(demDs, "SICNU_DEM_UNIT").toLower();
    if (!declaredUnit.isEmpty()) {
        demUnit = declaredUnit.toStdString();
    }
    double demUnitScale = 1.0;
    if (demUnit == "feet") {
        demUnitScale = 0.3048;
    } else if (demUnit == "decimeters") {
        demUnitScale = 0.1;
    } else if (demUnit != "meters") {
        context.logWarning("Unknown DEM unit '" + demUnit + "' (SICNU_DEM_UNIT); "
                           "assuming meters");
        demUnit = "meters";
        demUnitScale = 1.0;
    }

    // Sentinel declared on the analysis band (NaN when undeclared).
    bool hasNodata = false;
    const double nodataRaw = src.bandNoDataValue(band, &hasNodata);
    const float nodata = hasNodata ? static_cast<float>(nodataRaw)
                                   : std::numeric_limits<float>::quiet_NaN();

    sicnu::sar::TerrainCorrectionOptions options;
    options.incidenceDeg = incidenceDeg;
    options.headingDeg = headingDeg;
    options.applyFlattening = true;
    options.applyShadowMask = flagMask;
    options.writeIncidenceBand = flagIncidence;
    options.demUnitScale = demUnitScale;

    // Kernel band layout: band 1 = gamma0; band 2 = Byte mask when enabled;
    // last band = local incidence (degrees) when enabled.
    const int outBands = 1 + (flagMask ? 1 : 0) + (flagIncidence ? 1 : 0);
    const int maskBand = flagMask ? 2 : 0;
    const int incidenceBand = flagIncidence ? (flagMask ? 3 : 2) : 0;

    context.reportProgress(0.05, "Running DEM terrain correction");
    GdalStreamingOutput dst(QString::fromStdString(outputPath), src.width(), src.height(),
                            outBands, GDT_Float32, src.geoTransform(), src.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create output raster");
    }
    dst.setBandNoDataValue(1, std::numeric_limits<float>::quiet_NaN());
    if (maskBand > 0) {
        dst.setBandNoDataValue(maskBand, 255.0); // kernel mask sentinel
    }
    if (incidenceBand > 0) {
        dst.setBandNoDataValue(incidenceBand, std::numeric_limits<float>::quiet_NaN());
    }

    const bool ok = sicnu::sar::terrainFlattenRaster(src, band, demDs, options, nodata,
                                                     dst, 256, polarizations, sensor);
    if (!ok) {
        dst.abandon();
        throw RSOperatorError(ErrorCode::GdalError,
                              "SAR terrain correction failed while streaming");
    }
    // Radiometric state in the shared vocabulary.
    dst.setMetadataItem("SICNU_RADIOMETRIC_STATE", "gamma0");

    QString error;
    if (!dst.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to finalize output: " +
                                                        error.toStdString());
    }

    std::string layout = "band 1: gamma0 (Float32)";
    if (maskBand > 0) {
        layout += "; band 2: validity mask (Byte: 1=valid, 0=layover/shadow, 255=nodata)";
    }
    if (incidenceBand > 0) {
        layout += "; band " + std::to_string(incidenceBand) +
                  ": local incidence angle (degrees, Float32)";
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["calibration"] = "gamma0";
    result["bands"] = outBands;
    result["incidenceBand"] = incidenceBand;
    result["maskBand"] = maskBand;
    result["layout"] = layout;
    context.reportProgress(1.0, "SAR terrain correction complete");
    return result;
}

} // namespace sicnu::operators::rs
