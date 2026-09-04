/***************************************************************************
 * rs_feature_stack_operator.cpp — Multimodal feature cube builder (Platform 3.0)
 ***************************************************************************/
#include "rs_feature_stack_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/features/feature_cube.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_window_read.h"

#include <gdal.h>

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_modalities = { "optical", "sar", "dem",
                                                "auxiliary", "model_derived" };
constexpr int kTileSize = 256;
constexpr double kGeoTransformEpsilon = 1e-9;

/// One 'features[]' entry after parameter resolution.
struct FeatureSource
{
    std::string input;
    int band = 1;
    std::string id;
    std::string role;
    std::string unit;
    std::string modality;
    std::string timeStart;
    std::string timeEnd;
    double scale = 1.0;
    double offset = 0.0;
};

} // anonymous namespace

Json::Value RsFeatureStackOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);

    Json::Value featuresParam(Json::objectValue);
    featuresParam["type"] = "array";
    featuresParam["description"] =
        "Feature sources; each entry: {input: raster path (required), band: 1-based "
        "(default 1), id (default '<basename>_b<band>'), role: semantic role id, unit, "
        "modality (optical|sar|dem|auxiliary|model_derived), time_start/time_end: ISO "
        "instants, scale (default 1), offset (default 0)}. Declared physical scaling is "
        "applied at stack time (stored = source·scale + offset; declared NoData "
        "sentinels are copied unchanged).";
    Json::Value items(Json::objectValue);
    items["type"] = "object";
    featuresParam["items"] = items;
    props["features"] = featuresParam;

    props["reference"] = makeRasterParam("reference",
                                         "Optional reference grid: every input must match "
                                         "its dimensions and CRS exactly. When omitted, all "
                                         "inputs must share the grid of features[0].",
                                         false);
    props["output"] = makeOutputParam("output",
                                      "Output feature cube raster (Float32, one band per feature)",
                                      "tif");
    props["feature_id"] = makeStringParam("feature_id",
                                          "Cube identity recorded in the feature contract",
                                          "feature_cube");
    props["generator"] = makeStringParam("generator",
                                         "Generator id recorded in the feature contract",
                                         "rs:feature_stack");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Feature cube raster path");
    Json::Value featuresOut(Json::objectValue);
    featuresOut["type"] = "array";
    featuresOut["description"] = "Per-band feature identities: [{id, role, band}]";
    outputs["features"] = featuresOut;
    outputs["bands"] = makeIntegerParam("bands", "Number of cube bands");
    Json::Value modalitiesOut(Json::objectValue);
    modalitiesOut["type"] = "array";
    modalitiesOut["description"] = "Distinct modalities declared across the features";
    outputs["modalities"] = modalitiesOut;

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"features", "output"});
    return root;
}

Json::Value RsFeatureStackOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("features");
    meta["tags"].append("feature cube");
    meta["tags"].append("multimodal");
    meta["tags"].append("stack");
    meta["purpose"] = "Build a self-describing multimodal feature cube: every band carries a "
                      "declared identity (id, semantic role, unit, modality, time) so models "
                      "and agents match inputs by contract instead of convention.";
    meta["prerequisites"].append("All inputs co-registered on a common grid (identical "
                                 "dimensions and CRS); align against the reference with "
                                 "gdal:reproject first. Grid mismatches are errors, not "
                                 "resampled.");
    meta["workflowHints"].append("Feed rs:infer models by role matching: declare semantic roles "
                                 "here, then rs:feature_select keeps exactly a model manifest's "
                                 "input.band_roles before inference.");
    meta["workflowHints"].append("Run rs:feature_normalize on the cube before ML training or "
                                 "inference; the fit statistics ride the cube contract.");
    meta["limitations"].append("No hidden resampling: dimension or CRS mismatches fail the run.");
    meta["limitations"].append("The cube contract is stored as dataset metadata; oversized "
                               "contracts spill to a '<file>.features.json' sidecar.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "multimodal";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsFeatureStackOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = kTileSize;
    est["tileHeight"] = kTileSize;
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * kTileSize * kTileSize * 4ULL );
    return est;
}

Json::Value RsFeatureStackOperator::run(const Json::Value& params,
                                        RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string outputPath = requireString(params, "output");
    if (!params.isMember("features") || !params["features"].isArray()
        || params["features"].empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "'features' must be a non-empty array of "
                              "{input, band, id, role, ...} entries");
    }

    // --- Resolve the feature entries -------------------------------------
    std::vector<FeatureSource> sources;
    const Json::Value& featuresJson = params["features"];
    sources.reserve(featuresJson.size());
    for (Json::ArrayIndex i = 0; i < featuresJson.size(); ++i) {
        const Json::Value& entry = featuresJson[i];
        const std::string where = "features[" + std::to_string(i) + "]";
        if (!entry.isObject()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  where + " must be an object");
        }
        FeatureSource fs;
        if (!entry.isMember("input") || !entry["input"].isString()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  where + " needs a string 'input' raster path");
        }
        fs.input = entry["input"].asString();
        fs.band = getInt(entry, "band", 1);
        if (fs.band < 1) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  where + " band must be >= 1 (1-based)");
        }
        fs.id = getString(entry, "id", "");
        if (fs.id.empty()) {
            const QString base = QFileInfo(QString::fromStdString(fs.input)).completeBaseName();
            fs.id = (base.isEmpty() ? QString::fromStdString(fs.input) : base).toStdString()
                    + "_b" + std::to_string(fs.band);
        }
        fs.role = getString(entry, "role", "");
        fs.unit = getString(entry, "unit", "");
        fs.modality = getString(entry, "modality", "");
        if (!fs.modality.empty()) {
            fs.modality = getEnum(entry, "modality", s_modalities);
        }
        fs.timeStart = getString(entry, "time_start", "");
        fs.timeEnd = getString(entry, "time_end", "");
        if (!fs.timeEnd.empty() && fs.timeStart.empty()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  where + " has 'time_end' without 'time_start'");
        }
        fs.scale = getDouble(entry, "scale", 1.0);
        fs.offset = getDouble(entry, "offset", 0.0);
        sources.push_back(std::move(fs));
    }

    const int bandCount = static_cast<int>(sources.size());
    // Duplicate ids would corrupt the cube contract (contract parsing rejects them).
    std::set<std::string> ids;
    for (int i = 0; i < bandCount; ++i) {
        if (!ids.insert(sources[i].id).second) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "duplicate feature id '" + sources[i].id + "' (features[" +
                                      std::to_string(i) + "]); pass explicit 'id's");
        }
    }

    // --- Open the inputs ---------------------------------------------------
    std::vector<GdalDatasetWrapper> srcs(sources.size());
    for (int i = 0; i < bandCount; ++i) {
        if (!fileExists(sources[i].input)) {
            throw RSOperatorError(ErrorCode::FileNotFound,
                                  "Input raster not found: " + sources[i].input);
        }
        if (!srcs[i].open(QString::fromStdString(sources[i].input))) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Cannot open input raster: " + sources[i].input);
        }
        if (sources[i].band > srcs[i].bandCount()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "features[" + std::to_string(i) + "] band " +
                                      std::to_string(sources[i].band) + " out of range (" +
                                      sources[i].input + " has " +
                                      std::to_string(srcs[i].bandCount()) + " bands)");
        }
    }

    // --- Grid check ---------------------------------------------------------
    const std::string referencePath = getString(params, "reference", "");
    GdalDatasetWrapper referenceDs;
    if (!referencePath.empty()) {
        if (!fileExists(referencePath)) {
            throw RSOperatorError(ErrorCode::FileNotFound,
                                  "Reference raster not found: " + referencePath);
        }
        if (!referenceDs.open(QString::fromStdString(referencePath))) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Cannot open reference raster: " + referencePath);
        }
    }
    const GdalDatasetWrapper& grid = referencePath.empty() ? srcs[0] : referenceDs;
    const int width = grid.width();
    const int height = grid.height();
    const QString refProjection = grid.projection();
    const std::array<double, 6> refTransform = grid.geoTransform();
    const std::string gridName = referencePath.empty() ? "features[0] grid" : "reference grid";
    if (width < 1 || height < 1) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "The " + gridName + " is empty");
    }

    for (int i = 0; i < bandCount; ++i) {
        const GdalDatasetWrapper& src = srcs[i];
        if (src.width() != width || src.height() != height) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "features[" + std::to_string(i) + "] '" + sources[i].input +
                                      "' is " + std::to_string(src.width()) + "x" +
                                      std::to_string(src.height()) + " but the " + gridName +
                                      " is " + std::to_string(width) + "x" +
                                      std::to_string(height) + " — align inputs first "
                                      "(gdal:reproject); rs:feature_stack does not resample");
        }
        if (src.projection() != refProjection) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "features[" + std::to_string(i) + "] '" + sources[i].input +
                                      "' has a CRS that does not match the " + gridName +
                                      " CRS — align inputs first (gdal:reproject)");
        }
        if (refProjection.isEmpty()) {
            context.logWarning("Inputs carry no CRS; co-registration cannot be verified");
        }
        const std::array<double, 6> gt = src.geoTransform();
        for (int k = 0; k < 6; ++k) {
            if (std::fabs(gt[k] - refTransform[k]) > kGeoTransformEpsilon) {
                context.logWarning("features[" + std::to_string(i) + "] geotransform differs "
                                   "from the " + gridName + " (dimensions and CRS agree); "
                                   "continuing without resampling");
                break;
            }
        }
    }

    // --- Per-band NoData sentinels (declared on the source, else NaN) ------
    std::vector<double> nodataRaw(bandCount, 0.0);
    std::vector<bool> hasNodata(bandCount, false);
    for (int b = 0; b < bandCount; ++b) {
        bool has = false;
        nodataRaw[b] = srcs[b].bandNoDataValue(sources[b].band, &has);
        hasNodata[b] = has;
    }

    // --- Create the output cube --------------------------------------------
    context.reportProgress(0.05, "Creating feature cube output");
    GdalDatasetWrapper out;
    QString outErr;
    if (!out.create(QString::fromStdString(outputPath), width, height, bandCount,
                    static_cast<int>(GDT_Float32), refTransform, refProjection, &outErr)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "failed to create output: " + outErr.toStdString());
    }
    for (int b = 0; b < bandCount; ++b) {
        out.setBandNoDataValue(b + 1, hasNodata[b] ? nodataRaw[b]
                                                   : std::numeric_limits<double>::quiet_NaN());
    }

    // --- Stream each source band into the cube ------------------------------
    std::vector<float> tileBuffer(static_cast<size_t>(kTileSize) * kTileSize);
    for (int b = 0; b < bandCount; ++b) {
        context.throwIfCancelled();
        const GdalDatasetWrapper& src = srcs[b];
        // Straight copy of the source band; the declared physical scaling is
        // baked in (stored = source·scale + offset) so the contract's
        // scale/offset stay 1/0 (values are already physical).
        const bool applyLinear = sources[b].scale != 1.0 || sources[b].offset != 0.0;
        const float nodataFloat = hasNodata[b] ? static_cast<float>(nodataRaw[b])
                                               : std::numeric_limits<float>::quiet_NaN();
        for (int y = 0; y < height; y += kTileSize) {
            const int h = std::min(kTileSize, height - y);
            for (int x = 0; x < width; x += kTileSize) {
                const int w = std::min(kTileSize, width - x);
                if (!sicnu::processing::readClampedWindow(src, sources[b].band, x, y, w, h,
                                                          0, tileBuffer)) {
                    out.close();
                    QFile::remove(QString::fromStdString(outputPath));
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "failed reading band " +
                                              std::to_string(sources[b].band) + " from " +
                                              sources[b].input);
                }
                if (applyLinear) {
                    const size_t pixels = static_cast<size_t>(w) * h;
                    // Declared NoData sentinels and non-finite values are copied
                    // unchanged so masking semantics survive the scaling.
                    for (size_t p = 0; p < pixels; ++p) {
                        const float v = tileBuffer[p];
                        if (std::isfinite(v) && v != nodataFloat) {
                            tileBuffer[p] = static_cast<float>(v * sources[b].scale
                                                               + sources[b].offset);
                        }
                    }
                }
                if (!out.writeBandWindow(b + 1, x, y, w, h, tileBuffer.data())) {
                    out.close();
                    QFile::remove(QString::fromStdString(outputPath));
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "failed writing cube band " +
                                              std::to_string(b + 1));
                }
            }
        }
        context.reportProgress(0.05 + 0.9 * (b + 1) / bandCount,
                               "Stacked feature band " + std::to_string(b + 1) + "/" +
                                   std::to_string(bandCount));
    }

    // --- Feature cube contract ----------------------------------------------
    sicnu::features::FeatureCubeContract contract;
    contract.featureId = QString::fromStdString(getString(params, "feature_id", "feature_cube"));
    contract.generator = QString::fromStdString(getString(params, "generator", "rs:feature_stack"));
    for (int b = 0; b < bandCount; ++b) {
        sicnu::features::FeatureBand fb;
        fb.id = QString::fromStdString(sources[b].id);
        fb.semanticRole = QString::fromStdString(sources[b].role);
        fb.unit = QString::fromStdString(sources[b].unit);
        fb.band = b + 1;
        fb.sourcePath = QString::fromStdString(sources[b].input);
        fb.modality = QString::fromStdString(sources[b].modality);
        if (!sources[b].timeStart.empty() && !sources[b].timeEnd.empty()) {
            fb.time.kind = QStringLiteral("range");
            fb.time.startIso = QString::fromStdString(sources[b].timeStart);
            fb.time.endIso = QString::fromStdString(sources[b].timeEnd);
        } else if (!sources[b].timeStart.empty()) {
            fb.time.kind = QStringLiteral("single");
            fb.time.startIso = QString::fromStdString(sources[b].timeStart);
        }
        // Scaling was applied at stack time, so stored values are physical.
        fb.scale = 1.0;
        fb.offset = 0.0;
        fb.nodata = hasNodata[b] ? nodataRaw[b]
                                 : std::numeric_limits<double>::quiet_NaN();
        contract.bands.push_back(fb);
    }

    // Dataset-level modality tag: "multimodal" when the cube mixes modalities.
    std::vector<std::string> modalities;
    for (const FeatureSource& fs : sources) {
        if (!fs.modality.empty()
            && std::find(modalities.begin(), modalities.end(), fs.modality) == modalities.end()) {
            modalities.push_back(fs.modality);
        }
    }
    if (!modalities.empty()) {
        const QString modalityTag = modalities.size() > 1
                                        ? QStringLiteral("multimodal")
                                        : QString::fromStdString(modalities.front());
        GDALSetMetadataItem(static_cast<GDALDatasetH>(out.dataset()), "SICNU_MODALITY",
                            modalityTag.toUtf8().constData(), nullptr);
    }

    // The contract must ride on the open dataset (before close); oversized
    // contracts spill to the conventional sidecar next to the output.
    const QString sidecar = QString::fromStdString(outputPath) + QStringLiteral(".features.json");
    if (!sicnu::features::writeFeatureCubeMetadata(out.dataset(), contract, sidecar)) {
        out.close();
        QFile::remove(QString::fromStdString(outputPath));
        QFile::remove(sidecar);
        throw RSOperatorError(ErrorCode::GdalError,
                              "failed to write the feature cube contract onto " + outputPath);
    }

    QString closeErr;
    if (!out.closeWithError(&closeErr)) {
        QFile::remove(QString::fromStdString(outputPath));
        throw RSOperatorError(ErrorCode::GdalError,
                              "output flush failed (disk full?): " + closeErr.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    Json::Value featuresOut(Json::arrayValue);
    for (int b = 0; b < bandCount; ++b) {
        Json::Value f(Json::objectValue);
        f["id"] = sources[b].id;
        f["role"] = sources[b].role;
        f["band"] = b + 1;
        featuresOut.append(f);
    }
    result["features"] = featuresOut;
    result["bands"] = bandCount;
    Json::Value modalitiesOut(Json::arrayValue);
    for (const std::string& m : modalities) {
        modalitiesOut.append(m);
    }
    result["modalities"] = modalitiesOut;
    context.reportProgress(1.0, "Feature cube complete");
    return result;
}

} // namespace sicnu::operators::rs
