/***************************************************************************
 * rs_qa_mask_operator.cpp  —  Quality / cloud / shadow / snow mask RSOperator
 ***************************************************************************/
#include "rs_qa_mask_operator.h"

#include "data/band_role.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/qa_mask.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <gdal.h>

#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_sources = {
    "auto", "landsat_qa_pixel", "sentinel2_scl", "generic_bitmask"
};

const std::vector<std::string> s_masks = {
    "cloud_and_shadow", "cloud", "cloud_shadow", "snow", "water", "all"
};

/// Resolves the QA band number: explicit `qa_band` wins; otherwise the first
/// band whose semantic role is scene_classification or qa. Returns 0 when no
/// QA band can be identified.
int resolveQaBand(GdalDatasetWrapper& ds, bool hasExplicit, int explicitBand,
                  int bandCount)
{
    if (hasExplicit)
        return explicitBand;
    for (int b = 1; b <= bandCount; ++b) {
        const QString roleId = ds.bandMetadataItem(b, "SICNU_BAND_ROLE");
        if (roleId == QLatin1String("scene_classification"))
            return b;
    }
    for (int b = 1; b <= bandCount; ++b) {
        const QString roleId = ds.bandMetadataItem(b, "SICNU_BAND_ROLE");
        if (roleId == QLatin1String("qa"))
            return b;
    }
    return 0;
}

/// Resolves the QA source from the resolved band's semantic role (or name
/// fallback) when `source` is "auto".
std::string resolveSource(const std::string& requested, const QString& bandName,
                          const QString& roleId)
{
    if (requested != "auto")
        return requested;
    if (roleId == QLatin1String("scene_classification"))
        return "sentinel2_scl";
    if (roleId == QLatin1String("qa"))
        return "landsat_qa_pixel";
    const QString name = bandName.toUpper();
    if (name.contains(QStringLiteral("SCL")))
        return "sentinel2_scl";
    if (name.contains(QStringLiteral("QA")))
        return "landsat_qa_pixel";
    return "generic_bitmask";
}

/// Builds the mask rule for the requested mask selection and source.
/// Returns false for a combination the source does not support.
bool buildMaskRule(const std::string& source, const std::string& maskSelection,
                   uint32_t* landsatFlags, bool sclClasses[16])
{
    for (int i = 0; i < 16; ++i)
        sclClasses[i] = false;

    if (source == "landsat_qa_pixel") {
        const uint32_t cloud = QaMask::LandsatMaskCloud
                               | QaMask::LandsatMaskDilatedCloud
                               | QaMask::LandsatMaskCirrus;
        if (maskSelection == "cloud")
            *landsatFlags = cloud;
        else if (maskSelection == "cloud_shadow")
            *landsatFlags = QaMask::LandsatMaskCloudShadow;
        else if (maskSelection == "snow")
            *landsatFlags = QaMask::LandsatMaskSnow;
        else if (maskSelection == "water")
            *landsatFlags = QaMask::LandsatMaskWater;
        else if (maskSelection == "cloud_and_shadow")
            *landsatFlags = cloud | QaMask::LandsatMaskCloudShadow;
        else if (maskSelection == "all")
            *landsatFlags = QaMask::LandsatMaskFill | cloud
                            | QaMask::LandsatMaskCloudShadow
                            | QaMask::LandsatMaskSnow
                            | QaMask::LandsatMaskWater;
        else
            return false;
        return true;
    }

    if (source == "sentinel2_scl") {
        auto select = [&](std::initializer_list<int> classes) {
            for (int c : classes)
                sclClasses[c] = true;
        };
        if (maskSelection == "cloud")
            select({QaMask::SclCloudMediumProbability, QaMask::SclCloudHighProbability,
                    QaMask::SclThinCirrus});
        else if (maskSelection == "cloud_shadow")
            select({QaMask::SclCloudShadow});
        else if (maskSelection == "snow")
            select({QaMask::SclSnow});
        else if (maskSelection == "water")
            select({QaMask::SclWater});
        else if (maskSelection == "cloud_and_shadow")
            select({QaMask::SclCloudShadow, QaMask::SclCloudMediumProbability,
                    QaMask::SclCloudHighProbability, QaMask::SclThinCirrus});
        else if (maskSelection == "all")
            select({QaMask::SclSaturated, QaMask::SclDarkFeatures,
                    QaMask::SclCloudShadow, QaMask::SclCloudMediumProbability,
                    QaMask::SclCloudHighProbability, QaMask::SclThinCirrus,
                    QaMask::SclSnow});
        else
            return false;
        return true;
    }

    return source == "generic_bitmask";
}

} // anonymous namespace

Json::Value RsQaMaskOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Raster containing the QA band");
    props["output"] = makeOutputParam("output", "Output mask raster (UInt8, 1 = masked)", "tif");
    props["qa_band"] = makeIntegerParam("qa_band", "1-based QA band (optional; when omitted, resolved from the input's product band roles)", 0);
    props["source"] = makeEnumParam("source", "QA source interpretation", s_sources, "auto");
    props["mask"] = makeEnumParam("mask", "Classes to mask out", s_masks, "cloud_and_shadow");
    props["bits"] = makeIntegerParam("bits", "Bit flags for generic_bitmask source", 0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output mask raster path");
    outputs["source"] = makeStringParam("source", "Resolved QA source", "");
    outputs["maskClasses"] = makeStringParam("maskClasses", "Applied mask selection", "");
    outputs["maskedPixels"] = makeIntegerParam("maskedPixels", "Masked pixel count", 0);
    outputs["totalPixels"] = makeIntegerParam("totalPixels", "Evaluated pixel count", 0);
    outputs["maskedPercent"] = makeNumberParam("maskedPercent", "Masked pixel percentage", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsQaMaskOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("qa");
    meta["tags"].append("cloud");
    meta["tags"].append("mask");
    meta["purpose"] = "Derive cloud / cloud-shadow / snow masks from product QA bands.";
    meta["prerequisites"].append("Input must carry a QA band (Landsat QA_PIXEL or Sentinel-2 SCL) or an explicit qa_band.");
    meta["workflowHints"].append("Apply the mask to exclude obscured pixels before computing indices or change detection.");
    meta["limitations"].append("Sentinel-2 cloud shadow interpretation uses the SCL class only (no probability thresholds).");
    return meta;
}

Json::Value RsQaMaskOperator::executionEstimate() const {
    // FullRaster (base default): no preferred tile. run() loads the whole QA
    // band as float32 plus a UInt8 mask and a uint16 conversion buffer (7
    // bytes/pixel in flight); typical 1024x1024 input -> ~7 MiB.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 7340032;
    return est;
}

Json::Value RsQaMaskOperator::run(const Json::Value& params,
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

    const bool hasQaBand = params.isMember("qa_band");
    const int qaBandExplicit = getInt(params, "qa_band", 0);
    const std::string sourceRequested = getEnum(params, "source", s_sources, "auto");
    const std::string maskSelection = getEnum(params, "mask", s_masks, "cloud_and_shadow");
    const uint16_t genericBits = static_cast<uint16_t>(getInt(params, "bits", 0));

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);
    }

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    const size_t pixelCount = static_cast<size_t>(width) * height;

    const int qaBand = resolveQaBand(ds, hasQaBand, qaBandExplicit, bandCount);
    if (qaBand < 1 || qaBand > bandCount) {
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "No QA band found: pass qa_band explicitly or use a raster whose "
            "bands carry SICNU_BAND_ROLE=scene_classification/qa product metadata");
    }

    const std::string source = resolveSource(sourceRequested,
                                             ds.bandDescription(qaBand),
                                             ds.bandMetadataItem(qaBand, "SICNU_BAND_ROLE"));
    if (source == "generic_bitmask" && genericBits == 0) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "generic_bitmask source requires a non-zero `bits` value");
    }

    uint32_t landsatFlags = 0;
    bool sclClasses[16] = {};
    if (!buildMaskRule(source, maskSelection, &landsatFlags, sclClasses)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Mask selection '" + maskSelection +
                                  "' is not supported for source '" + source + "'");
    }

    context.logInfo("Computing QA mask (source=" + source + ", mask=" + maskSelection + ")");

    std::vector<float> qa(pixelCount);
    if (!ds.readBandData(qaBand, qa.data(), width, height)) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to read QA band " + std::to_string(qaBand));
    }

    std::vector<uint8_t> mask(pixelCount);
    if (source == "sentinel2_scl") {
        std::vector<uint8_t> scl(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
            scl[i] = static_cast<uint8_t>(qa[i]);
        QaMask::sclMask(scl.data(), mask.data(), pixelCount, sclClasses);
    } else if (source == "generic_bitmask") {
        std::vector<uint16_t> values(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
            values[i] = static_cast<uint16_t>(qa[i]);
        QaMask::genericBitmaskMask(values.data(), mask.data(), pixelCount, genericBits);
    } else {
        std::vector<uint16_t> values(pixelCount);
        for (size_t i = 0; i < pixelCount; ++i)
            values[i] = static_cast<uint16_t>(qa[i]);
        QaMask::landsatQaMask(values.data(), mask.data(), pixelCount, landsatFlags);
    }

    context.throwIfCancelled();
    context.reportProgress(0.7, "Writing mask raster");

    // UInt8 output: 1 = masked, 0 = clear.
    QString errorMessage;
    GDALDatasetH outDs = createOutputTiff(QString::fromStdString(outputPath), width, height,
                                          1, static_cast<int>(GDT_Byte),
                                          ds.geoTransform(), ds.projection(), &errorMessage);
    if (!outDs) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create mask raster: " + errorMessage.toStdString());
    }
    GDALRasterBandH outBand = GDALGetRasterBand(outDs, 1);
    const CPLErr writeErr = GDALRasterIO(outBand, GF_Write, 0, 0, width, height,
                                         mask.data(), width, height, GDT_Byte, 0, 0);
    GDALSetMetadataItem(outDs, "SICNU_QA_MASK_SOURCE", source.c_str(), nullptr);
    GDALSetMetadataItem(outDs, "SICNU_QA_MASK_SELECTION", maskSelection.c_str(), nullptr);
    GDALClose(outDs);
    if (writeErr != CE_None) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write mask raster: " + outputPath);
    }

    size_t masked = 0;
    for (uint8_t v : mask)
        masked += (v != 0) ? 1 : 0;

    context.reportProgress(1.0, "QA mask complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["source"] = source;
    result["maskClasses"] = maskSelection;
    result["maskedPixels"] = static_cast<Json::UInt64>(masked);
    result["totalPixels"] = static_cast<Json::UInt64>(pixelCount);
    result["maskedPercent"] = pixelCount == 0
        ? 0.0
        : 100.0 * static_cast<double>(masked) / static_cast<double>(pixelCount);
    return result;
}

} // namespace sicnu::operators::rs
