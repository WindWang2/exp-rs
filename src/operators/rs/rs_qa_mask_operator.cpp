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
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput + Tile

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string>& qaSources() {
    static const std::vector<std::string> s = {
        "auto", "landsat_qa_pixel", "sentinel2_scl", "generic_bitmask"
    };
    return s;
}

const std::vector<std::string>& qaMasks() {
    static const std::vector<std::string> s = {
        "cloud_and_shadow", "cloud", "cloud_shadow", "snow", "water", "all"
    };
    return s;
}

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
    props["source"] = makeEnumParam("source", "QA source interpretation", qaSources(), "auto");
    props["mask"] = makeEnumParam("mask", "Classes to mask out", qaMasks(), "cloud_and_shadow");
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
        meta["task"] = "quality-masking";
    meta["gpu"] = false;
    meta["purpose"] = "Derive cloud / cloud-shadow / snow masks from product QA bands.";
    meta["prerequisites"].append("Input must carry a QA band (Landsat QA_PIXEL or Sentinel-2 SCL) or an explicit qa_band.");
    meta["workflowHints"].append("Apply the mask to exclude obscured pixels before computing indices or change detection.");
    meta["limitations"].append("Sentinel-2 cloud shadow interpretation uses the SCL class only (no probability thresholds).");
    return meta;
}

Json::Value RsQaMaskOperator::executionEstimate() const {
    // Streaming (#665): 256x256 row-blocks; one native-dtype read buffer
    // (<= 8 B/px) + uint16 conversion + UInt8 mask + UInt8 SCL block — about
    // 12 * 256 * 256 bytes = ~0.75 MiB, rounded up to 2 MiB with overhead.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 2097152;
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
    const std::string sourceRequested = getEnum(params, "source", qaSources(), "auto");
    const std::string maskSelection = getEnum(params, "mask", qaMasks(), "cloud_and_shadow");
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

    // Streaming execution (#665, ADR 0124 grade bit-exact): the QA band is
    // processed in horizontal row-blocks — O(blockRows*width) resident —
    // instead of full-raster QA + mask buffers. The #699 native-dtype read
    // and the per-pixel mask kernels are unchanged, so results are
    // bit-identical to the full-raster path.
    const int blockRows = std::max(1, std::min(256, height));
    const size_t blockSize = static_cast<size_t>(width) * blockRows;

    // #699: read the QA band in its NATIVE data type. The previous path read
    // the band as Float32 and static_cast<uint16_t>'d every sample: casting
    // NaN / negative sentinels to uint16 is undefined behaviour (in practice
    // yielding 0 = "clear" by accident) and values >= 65536 silently
    // truncated. Conversion now applies explicit guards and counts the
    // irregular samples.
    const int qaType = ds.bandDataType(qaBand);
    bool hasNodata = false;
    const double nodataVal = ds.bandNoDataValue(qaBand, &hasNodata);

    std::vector<uint16_t> values(blockSize, 0);
    std::vector<uint8_t> mask(blockSize);
    std::vector<uint8_t> scl(blockSize);
    size_t irregular = 0;
    auto convertSample = [&](double v) -> uint16_t {
        if (hasNodata && std::isfinite(nodataVal) && std::abs(v - nodataVal) < 1e-9) {
            ++irregular;
            return 0; // declared NoData -> not a QA word; leave unmasked
        }
        if (!std::isfinite(v)) {
            ++irregular;
            return 0; // NaN/Inf -> keep the historical "clear" outcome, no UB
        }
        if (v < 0.0) {
            ++irregular;
            return 0; // negative sentinel -> clear
        }
        if (v > 65535.0) {
            ++irregular;
            return 65535; // clamp instead of silently truncating high bits
        }
        return static_cast<uint16_t>(v);
    };

    // UInt8 output: 1 = masked, 0 = clear. Tiles are written as they are
    // computed (#647 contract: failures/cancel abandon() the partial file
    // instead of leaving it at the output path).
    GdalStreamingOutput output(QString::fromStdString(outputPath), width, height, 1,
                               static_cast<int>(GDT_Byte),
                               ds.geoTransform(), ds.projection());
    if (!output.isOpen()) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create mask raster: " + outputPath);
    }
    output.setMetadataItem(QStringLiteral("SICNU_QA_MASK_SOURCE"),
                           QString::fromStdString(source));
    output.setMetadataItem(QStringLiteral("SICNU_QA_MASK_SELECTION"),
                           QString::fromStdString(maskSelection));

    const int totalBlocks = (height + blockRows - 1) / blockRows;
    size_t masked = 0;
    int blockIndex = 0;
    // Any failure/cancel must not leave a partial raster at the output path.
    try {
    for (int y0 = 0; y0 < height; y0 += blockRows, ++blockIndex) {
        context.throwIfCancelled();
        const int rows = std::min(blockRows, height - y0);
        const size_t n = static_cast<size_t>(width) * rows;

        // Read the QA block in its native dtype and convert to uint16 with
        // the #699 guards (per-block buffers; lazily sized on first use).
        bool readOk = true;
        switch (qaType) {
        case GDT_Byte: {
            static thread_local std::vector<uint8_t> raw;
            raw.resize(blockSize);
            readOk = ds.readBandWindowNative(qaBand, 0, y0, width, rows, raw.data());
            for (size_t i = 0; readOk && i < n; ++i)
                values[i] = convertSample(raw[i]);
            break;
        }
        case GDT_UInt16: {
            readOk = ds.readBandWindowNative(qaBand, 0, y0, width, rows, values.data());
            if (readOk && hasNodata && std::isfinite(nodataVal) && nodataVal >= 0.0
                && nodataVal <= 65535.0) {
                const auto nd = static_cast<uint16_t>(nodataVal);
                for (size_t i = 0; i < n; ++i) {
                    if (values[i] == nd) {
                        values[i] = 0;
                        ++irregular;
                    }
                }
            }
            break;
        }
        case GDT_Int16: {
            static thread_local std::vector<int16_t> raw;
            raw.resize(blockSize);
            readOk = ds.readBandWindowNative(qaBand, 0, y0, width, rows, raw.data());
            for (size_t i = 0; readOk && i < n; ++i)
                values[i] = convertSample(raw[i]);
            break;
        }
        case GDT_UInt32: {
            static thread_local std::vector<uint32_t> raw;
            raw.resize(blockSize);
            readOk = ds.readBandWindowNative(qaBand, 0, y0, width, rows, raw.data());
            for (size_t i = 0; readOk && i < n; ++i)
                values[i] = convertSample(raw[i]);
            break;
        }
        case GDT_Int32: {
            static thread_local std::vector<int32_t> raw;
            raw.resize(blockSize);
            readOk = ds.readBandWindowNative(qaBand, 0, y0, width, rows, raw.data());
            for (size_t i = 0; readOk && i < n; ++i)
                values[i] = convertSample(raw[i]);
            break;
        }
        case GDT_Float32: {
            std::vector<float> raw(blockSize);
            readOk = ds.readBandWindow(qaBand, 0, y0, width, rows, raw.data());
            for (size_t i = 0; readOk && i < n; ++i)
                values[i] = convertSample(raw[i]);
            break;
        }
        case GDT_Float64: {
            static thread_local std::vector<double> raw;
            raw.resize(blockSize);
            readOk = ds.readBandWindowNative(qaBand, 0, y0, width, rows, raw.data());
            for (size_t i = 0; readOk && i < n; ++i)
                values[i] = convertSample(raw[i]);
            break;
        }
        default: {
            // Unknown/complex types: fall back to the Float32 read; the same
            // finite/range guards apply during conversion.
            std::vector<float> raw(blockSize);
            readOk = ds.readBandWindow(qaBand, 0, y0, width, rows, raw.data());
            for (size_t i = 0; readOk && i < n; ++i)
                values[i] = convertSample(raw[i]);
            break;
        }
        }
        if (!readOk) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read QA band " + std::to_string(qaBand));
        }

        if (source == "sentinel2_scl") {
            for (size_t i = 0; i < n; ++i)
                scl[i] = static_cast<uint8_t>(values[i]);
            QaMask::sclMask(scl.data(), mask.data(), n, sclClasses);
        } else if (source == "generic_bitmask") {
            QaMask::genericBitmaskMask(values.data(), mask.data(), n, genericBits);
        } else {
            QaMask::landsatQaMask(values.data(), mask.data(), n, landsatFlags);
        }
        for (size_t i = 0; i < n; ++i)
            masked += (mask[i] != 0) ? 1 : 0;

        const GdalBlockStream::Tile tile{0, y0, width, rows, 0, width, rows,
                                         blockIndex, totalBlocks};
        if (!output.writeTileRaw(1, tile, mask.data(), GDT_Byte)) {
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to write mask raster: " + outputPath);
        }
        context.reportProgress(0.1 + 0.6 * (static_cast<double>(blockIndex + 1) / totalBlocks),
                               "Computing QA mask");
    }

    if (irregular > 0) {
        context.logWarning(
            "QA band contained " + std::to_string(irregular)
            + " non-finite / out-of-range / declared-NoData sample(s); treated as clear (unmasked)");
    }

    context.reportProgress(0.7, "Writing mask raster");

    QString closeError;
    if (!output.closeWithError(&closeError)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write mask raster: " + closeError.toStdString());
    }
    } catch (...) {
        output.abandon();
        throw;
    }

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
