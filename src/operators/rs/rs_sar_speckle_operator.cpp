/***************************************************************************
 * rs_sar_speckle_operator.cpp — SAR speckle filtering (Platform 3.0)
 ***************************************************************************/
#include "rs_sar_speckle_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
// Included before sar_speckle.h, which references GdalBlockStream::Tile in
// the refinedLeeTile declaration without including its own header.
#include "processing/gdal/gdal_block_stream.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_speckle.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>
#include <QStringList>

#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = { "lee", "enhanced_lee", "frost", "kuan",
                                             "gamma_map", "refined_lee",
                                             "multitemporal" };

Json::Value makeSarInputContract() {
    Json::Value c(Json::objectValue);
    c["modality"] = "sar";
    return c;
}

} // anonymous namespace

Json::Value RsSarSpeckleOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input SAR intensity raster (calibrated sigma0 recommended)");
    props["input"]["x-rs-contract"] = makeSarInputContract();
    props["output"] = makeOutputParam("output", "Output despeckled raster (Float32)", "tif");
    props["band"] = makeIntegerParam("band", "1-based input band to filter (0 = all bands)", 1);
    props["method"] = makeEnumParam("method", "Speckle filter kernel", s_methods, "lee");
    Json::Value kernelSize = makeIntegerParam("kernelSize", "Odd filter window size", 3);
    setRange(kernelSize, 3, 15);
    props["kernelSize"] = kernelSize;
    props["noiseVariance"] = makeNumberParam("noiseVariance", "Model noise variance (Lee / Kuan / Gamma-MAP)", 0.25);
    props["dampingFactor"] = makeNumberParam("dampingFactor", "Frost exponential damping factor", 1.0);
    props["looks"] = makeIntegerParam("looks", "Equivalent number of looks (refined Lee / multitemporal)", 1);
    props["deviationK"] = makeNumberParam("deviationK", "Multitemporal gate: reject scenes deviating by more than k · localStd", 1.0);
    Json::Value companionScenes = makeStringParam("companionScenes",
                                                  "Co-registered same-grid scene paths (multitemporal only)",
                                                  "");
    companionScenes["type"] = "array";
    companionScenes["items"] = Json::Value(Json::objectValue);
    companionScenes["items"]["type"] = "string";
    props["companionScenes"] = companionScenes;
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the output", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the output", "");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Despeckled raster path");
    outputs["method"] = makeStringParam("method", "Speckle filter applied");
    outputs["kernelSize"] = makeIntegerParam("kernelSize", "Filter window size used");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSarSpeckleOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("speckle");
    meta["tags"].append("filtering");
    meta["purpose"] = "Reduce coherent speckle in SAR intensity imagery with classic "
                      "adaptive kernels or a multitemporal stack filter while keeping "
                      "the output on the input grid.";
    meta["prerequisites"].append("SAR intensity raster; calibrate first (rs:sar_calibrate) "
                                 "so filter statistics operate on physically scaled data.");
    meta["workflowHints"].append("rs:sar_calibrate -> rs:sar_speckle.");
    meta["workflowHints"].append("Multitemporal filtering needs co-registered, same-grid "
                                 "scenes listed in companionScenes.");
    meta["limitations"].append("The multitemporal gate rejects companion pixels deviating "
                               "from the reference by more than k · localStd "
                               "(deviationK); gated-out pixels fall back to the temporal "
                               "mean of the accepted scenes.");
    meta["limitations"].append("All filters assume intensity (power) data, not amplitude "
                               "or dB.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarSpeckleOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    // cleaned halo copy + core output tile per streaming step
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarSpeckleOperator::run(const Json::Value& params,
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
    const std::string methodStr = getEnum(params, "method", s_methods, "lee");
    bool methodOk = false;
    const sicnu::sar::SpeckleMethod method =
        sicnu::sar::speckleMethodFromString(QString::fromStdString(methodStr), &methodOk);
    if (!methodOk) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Unknown speckle method: " + methodStr);
    }
    const int kernelSize = getInt(params, "kernelSize", 3);
    if (kernelSize < 3 || kernelSize > 15 || kernelSize % 2 == 0) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "kernelSize must be an odd integer in [3, 15]");
    }
    const double noiseVariance = getDouble(params, "noiseVariance", 0.25);
    const double dampingFactor = getDouble(params, "dampingFactor", 1.0);
    const int looks = getInt(params, "looks", 1);
    const double deviationK = getDouble(params, "deviationK", 1.0);
    const QString polarizations =
        QString::fromStdString( getString( params, "polarizations", "" ) );
    const QString sensor = QString::fromStdString( getString( params, "sensor", "" ) );

    sicnu::sar::SpeckleParams speckleParams;
    speckleParams.method = method;
    speckleParams.kernelSize = kernelSize;
    speckleParams.noiseVariance = noiseVariance;
    speckleParams.dampingFactor = dampingFactor;
    speckleParams.looks = looks;
    speckleParams.deviationK = deviationK;

    QStringList companionPaths;
    if (method == sicnu::sar::SpeckleMethod::Multitemporal) {
        const std::vector<std::string> companions = getStringArray(params, "companionScenes");
        if (companions.empty()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "method 'multitemporal' requires at least one "
                                  "companionScenes entry");
        }
        for (const auto& path : companions) {
            companionPaths << QString::fromStdString(path);
        }
    }

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

    context.throwIfCancelled();
    context.reportProgress(0.05, "Filtering SAR speckle");
    // band=0 filters every band independently (legacy dialog behavior);
    // band>0 filters that single band.
    GdalStreamingOutput dst(QString::fromStdString(outputPath), src.width(), src.height(),
                            bandCount, GDT_Float32, src.geoTransform(), src.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create output raster");
    }
    dst.setNoDataValue(std::numeric_limits<float>::quiet_NaN());

    // Companion scenes are opened and grid-validated inside the kernel.
    for (int b = 0; b < bandCount; ++b) {
        context.throwIfCancelled();
        if (!sicnu::sar::speckleRaster(src, firstBand + b, speckleParams, nodata,
                                       companionPaths, dst, 256, b + 1,
                                       polarizations, sensor)) {
            dst.abandon();
            throw RSOperatorError(ErrorCode::GdalError,
                                  "SAR speckle filtering failed while streaming");
        }
        context.reportProgress(0.1 + 0.85 * (b + 1) / bandCount, "Filtered band");
    }

    QString error;
    if (!dst.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to finalize output: " +
                                                        error.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = methodStr;
    result["kernelSize"] = kernelSize;
    result["bands"] = bandCount;
    context.reportProgress(1.0, "SAR speckle filtering complete");
    return result;
}

} // namespace sicnu::operators::rs
