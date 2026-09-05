/***************************************************************************
 * rs_sar_terrain_flatten_operator.cpp — SAR radiometric terrain flattening
 ***************************************************************************/
#include "rs_sar_terrain_flatten_operator.h"

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

Json::Value RsSarTerrainFlattenOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input sigma0 raster (linear power)");
    props["input"]["x-rs-contract"] = makeSarInputContract();
    props["output"] = makeOutputParam("output", "Output terrain-flattened gamma0 raster (Float32)", "tif");
    props["band"] = makeIntegerParam("band", "1-based input band", 1);
    props["dem"] = makeRasterParam("dem", "Co-registered DEM covering the exact same grid (radar geometry)");
    props["dem"]["x-rs-contract"] = makeDemInputContract();
    props["incidenceDeg"] = makeNumberParam("incidenceDeg", "Scene incidence angle θ0 in degrees (near-range center)", 30.0);
    props["headingDeg"] = makeNumberParam("headingDeg", "Platform heading / look azimuth φ in degrees", 0.0);
    props["demUnit"] = makeEnumParam("demUnit", "DEM elevation unit (a declared SICNU_DEM_UNIT metadata on the DEM overrides it)", s_demUnits, "meters");
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the output", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the output", "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Terrain-flattened gamma0 raster path");
    outputs["calibration"] = makeStringParam("calibration", "Calibration state of the output (gamma0)");
    outputs["incidenceDeg"] = makeNumberParam("incidenceDeg", "Scene incidence angle used (degrees)");
    outputs["headingDeg"] = makeNumberParam("headingDeg", "Platform heading used (degrees)");
    outputs["demUnit"] = makeStringParam("demUnit", "Effective DEM elevation unit after metadata override");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "dem"});
    return root;
}

Json::Value RsSarTerrainFlattenOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("terrain");
    meta["tags"].append("radiometry");
    meta["purpose"] = "Radiometric terrain flattening: sigma0 to gamma0 "
                      "(sigma0·cosθ0/cosθi) using a co-registered DEM in radar "
                      "geometry, writing a single-band gamma0 SAR product.";
    meta["prerequisites"].append("sigma0 raster (linear power) and a DEM on the exact "
                                 "same grid (radar geometry for GRD products), plus the "
                                 "scene incidence angle and platform heading.");
    meta["workflowHints"].append("For the full product with the layover/shadow validity "
                                 "mask and the local incidence angle band use "
                                 "rs:sar_terrain_correction.");
    meta["limitations"].append("Plane-fit RTC model, NOT range-Doppler terrain correction.");
    meta["limitations"].append("The DEM must be co-registered with the input in radar "
                               "geometry; no resampling is performed.");
    meta["limitations"].append("Facets with a local incidence angle >= 85° are masked as "
                               "layover/shadow.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    // Second input contract: the DEM port (co-registered elevation raster).
    Json::Value demContract(Json::objectValue);
    demContract["modality"] = "dem";
    demContract["dataKind"] = "raster";
    demContract["gridRelation"] = "same-grid";
    contract["inputs"]["dem"] = demContract;
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarTerrainFlattenOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 6ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarTerrainFlattenOperator::run(const Json::Value& params,
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
    options.applyShadowMask = true;
    options.demUnitScale = demUnitScale;

    context.reportProgress(0.05, "Flattening terrain radiometry");
    // The kernel always writes the Byte validity mask as band 2 when
    // applyShadowMask is on (default): the output carries gamma0 + mask.
    GdalStreamingOutput dst(QString::fromStdString(outputPath), src.width(), src.height(),
                            2, GDT_Float32, src.geoTransform(), src.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create output raster");
    }
    dst.setNoDataValue(std::numeric_limits<float>::quiet_NaN());

    const bool ok = sicnu::sar::terrainFlattenRaster(src, band, demDs, options, nodata,
                                                     dst, 256, polarizations, sensor);
    if (!ok) {
        dst.abandon();
        throw RSOperatorError(ErrorCode::GdalError,
                              "SAR terrain flattening failed while streaming");
    }
    // Radiometric state in the shared vocabulary.
    dst.setMetadataItem("SICNU_RADIOMETRIC_STATE", "gamma0");

    QString error;
    if (!dst.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to finalize output: " +
                                                        error.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["calibration"] = "gamma0";
    result["incidenceDeg"] = incidenceDeg;
    result["headingDeg"] = headingDeg;
    result["demUnit"] = demUnit;
    context.reportProgress(1.0, "SAR terrain flattening complete");
    return result;
}

} // namespace sicnu::operators::rs
