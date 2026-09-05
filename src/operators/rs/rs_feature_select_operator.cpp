/***************************************************************************
 * rs_feature_select_operator.cpp — Feature cube band subsetting (Platform 3.0)
 ***************************************************************************/
#include "rs_feature_select_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/features/feature_cube.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <QFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kTileSize = 256;

/// Plain-raster fallback contract: bands without a cube contract get
/// synthetic ids "band_1..N" and no semantic roles.
sicnu::features::FeatureCubeContract plainContract( int bandCount )
{
    sicnu::features::FeatureCubeContract c;
    c.featureId = QStringLiteral( "band_raster" );
    c.bands.reserve( bandCount );
    for ( int b = 1; b <= bandCount; ++b ) {
        sicnu::features::FeatureBand fb;
        fb.id = QStringLiteral( "band_%1" ).arg( b );
        fb.band = b;
        c.bands.push_back( fb );
    }
    return c;
}

} // anonymous namespace

Json::Value RsFeatureSelectOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input",
                                     "Input feature cube (a plain raster works; its bands "
                                     "expose synthetic ids band_1..N)");

    Json::Value idsParam(Json::objectValue);
    idsParam["type"] = "array";
    idsParam["items"] = Json::Value(Json::objectValue);
    idsParam["items"]["type"] = "string";
    idsParam["description"] = "Feature ids to keep (matched against the cube contract)";
    props["ids"] = idsParam;

    Json::Value rolesParam(Json::objectValue);
    rolesParam["type"] = "array";
    rolesParam["items"] = Json::Value(Json::objectValue);
    rolesParam["items"]["type"] = "string";
    rolesParam["description"] = "Semantic roles to keep (e.g. a model manifest's "
                                "input.band_roles)";
    props["roles"] = rolesParam;

    Json::Value indicesParam(Json::objectValue);
    indicesParam["type"] = "array";
    indicesParam["items"] = Json::Value(Json::objectValue);
    indicesParam["items"]["type"] = "integer";
    indicesParam["items"]["minimum"] = 1;
    indicesParam["description"] = "1-based band indices to keep";
    props["indices"] = indicesParam;

    props["complement"] = makeBooleanParam("complement",
                                           "Drop the selected bands instead of keeping them",
                                           false);
    props["output"] = makeOutputParam("output", "Output feature cube subset (Float32)", "tif");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Feature cube subset path");
    Json::Value selectedOut(Json::objectValue);
    selectedOut["type"] = "array";
    selectedOut["description"] = "Ids of the selected bands, in output band order";
    outputs["selected"] = selectedOut;
    outputs["bands"] = makeIntegerParam("bands", "Number of selected bands");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsFeatureSelectOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("features");
    meta["tags"].append("selection");
    meta["tags"].append("multimodal");
    meta["tags"].append("model-input");
    meta["purpose"] = "Subset a feature cube's bands by id, semantic role or index while keeping "
                      "the self-describing contract intact — the model-input preparation step: "
                      "match a model manifest's input.band_roles, then feed rs:infer.";
    meta["prerequisites"].append("Input raster; ids and roles resolve against the cube contract "
                                 "(plain rasters expose synthetic ids band_1..N).");
    meta["workflowHints"].append("Match a model manifest's input.band_roles: rs:feature_select "
                                 "with roles=[...] renumbers the kept bands 1..k, then run "
                                 "rs:infer on the subset.");
    meta["workflowHints"].append("Drop null/degenerate bands before inference; the normalization "
                                 "stats are dropped with the subset, so re-run "
                                 "rs:feature_normalize on the selected cube afterwards.");
    meta["limitations"].append("Band selection only — pixels are copied without resampling, "
                               "reprojection or value changes.");
    meta["limitations"].append("The normalization section of the contract is dropped (stored "
                               "stats no longer match the subset).");
    return meta;
}

Json::Value RsFeatureSelectOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = kTileSize;
    est["tileHeight"] = kTileSize;
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * kTileSize * kTileSize * 4ULL );
    return est;
}

Json::Value RsFeatureSelectOperator::run(const Json::Value& params,
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
    const std::vector<std::string> ids = getStringArray(params, "ids");
    const std::vector<std::string> roles = getStringArray(params, "roles");
    std::vector<int> indices;
    if (params.isMember("indices")) {
        if (!params["indices"].isArray()) {
            throw RSOperatorError(ErrorCode::TypeMismatch,
                                  "Parameter 'indices' must be an array of 1-based "
                                  "band indices");
        }
        for (const Json::Value& item : params["indices"]) {
            if (!item.isNumeric()) {
                throw RSOperatorError(ErrorCode::TypeMismatch,
                                      "Parameter 'indices' must be an array of 1-based "
                                      "band indices");
            }
            indices.push_back(item.asInt());
        }
    }
    const bool complement = getBool(params, "complement", false);
    if (ids.empty() && roles.empty() && indices.empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Provide at least one of 'ids', 'roles' or 'indices'");
    }

    // Contract: cubes carry their band identity; plain rasters get a
    // synthesized one (ids "band_1..N", no roles).
    sicnu::features::FeatureCubeContract contract;
    QString contractError;
    if (!sicnu::features::readFeatureCubeMetadata(QString::fromStdString(inputPath),
                                                  &contract, &contractError)) {
        context.logInfo("No feature cube contract on '" + inputPath +
                        "'; synthesizing band identities band_1..N");
    }

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }
    const int bandCount = src.bandCount();
    if (contract.bands.isEmpty()
        || static_cast<int>(contract.bands.size()) != bandCount) {
        if (!contract.bands.isEmpty()) {
            context.logWarning("Feature cube contract lists " +
                               std::to_string(contract.bands.size()) +
                               " bands but the raster has " + std::to_string(bandCount) +
                               "; rebuilding a plain band identity");
        }
        contract = plainContract(bandCount);
    }

    // --- Resolve the selection: union of ids + roles + indices, cube order --
    std::vector<bool> selected(bandCount, false);
    for (const std::string& id : ids) {
        const QString qid = QString::fromStdString(id);
        bool matched = false;
        for (int b = 0; b < bandCount; ++b) {
            if (contract.bands[b].id == qid) {
                selected[b] = true;
                matched = true;
            }
        }
        if (!matched) {
            context.logWarning("Feature id '" + id + "' matched no band in the cube");
        }
    }
    for (const std::string& role : roles) {
        const QString qrole = QString::fromStdString(role);
        bool matched = false;
        for (int b = 0; b < bandCount; ++b) {
            if (!contract.bands[b].semanticRole.isEmpty()
                && contract.bands[b].semanticRole == qrole) {
                selected[b] = true;
                matched = true;
            }
        }
        if (!matched) {
            context.logWarning("Semantic role '" + role + "' matched no band in the cube");
        }
    }
    for (const int index : indices) {
        if (index < 1 || index > bandCount) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Band index " + std::to_string(index) +
                                      " out of range (1-" + std::to_string(bandCount) + ")");
        }
        selected[index - 1] = true;
    }
    if (complement) {
        selected.flip();
    }

    std::vector<int> selection; // 0-based cube band indices, cube order
    for (int b = 0; b < bandCount; ++b) {
        if (selected[b]) {
            selection.push_back(b);
        }
    }
    if (selection.empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "The feature selection is empty (nothing "
                              + std::string(complement ? "kept" : "selected")
                              + " after resolving ids/roles/indices)");
    }
    const int selectedCount = static_cast<int>(selection.size());

    // --- Create the output cube ---------------------------------------------
    context.reportProgress(0.05, "Creating feature cube subset");
    GdalDatasetWrapper out;
    QString outErr;
    if (!out.create(QString::fromStdString(outputPath), src.width(), src.height(),
                    selectedCount, static_cast<int>(GDT_Float32), src.geoTransform(),
                    src.projection(), &outErr)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "failed to create output: " + outErr.toStdString());
    }
    // RAII partial-output guard: an unexpected unwind (cancellation between
    // bands, GDAL exception) must not leave a truncated success-looking .tif.
    struct OutputCleanup {
        GdalDatasetWrapper &out;
        const std::string &path;
        bool armed = true;
        ~OutputCleanup() {
            if ( armed ) {
                out.closeWithError( nullptr );
                QFile::remove( QString::fromStdString( path ) );
            }
        }
    } outputCleanup{out, outputPath};


    // --- Subset contract ------------------------------------------------------
    if (contract.normalization.isObject() && !contract.normalization.empty()) {
        context.logWarning("Dropping the contract's normalization stats — they no longer "
                           "match the band subset; re-run rs:feature_normalize on the "
                           "selected cube");
    }
    sicnu::features::FeatureCubeContract outContract;
    outContract.featureId = contract.featureId;
    outContract.generator = QStringLiteral("rs:feature_select");
    outContract.bands.reserve(selectedCount);
    for (int k = 0; k < selectedCount; ++k) {
        const int srcBand = selection[k];
        sicnu::features::FeatureBand fb = contract.bands[srcBand];
        // Effective NoData: contract value, falling back to the raster's
        // declared sentinel, else NaN (undeclared).
        bool has = false;
        double nodata = src.bandNoDataValue(srcBand + 1, &has);
        if (!has && !std::isnan(fb.nodata)) {
            nodata = fb.nodata;
            has = true;
        }
        fb.band = k + 1;
        fb.nodata = has ? nodata : std::numeric_limits<double>::quiet_NaN();
        out.setBandNoDataValue(k + 1, fb.nodata);
        outContract.bands.push_back(fb);
    }

    // --- Stream-copy the selected bands --------------------------------------
    std::vector<float> tile(static_cast<size_t>(kTileSize) * kTileSize);
    for (int k = 0; k < selectedCount; ++k) {
        context.throwIfCancelled();
        const int srcBand = selection[k] + 1;
        for (int y = 0; y < src.height(); y += kTileSize) {
            const int h = std::min(kTileSize, src.height() - y);
            for (int x = 0; x < src.width(); x += kTileSize) {
                const int w = std::min(kTileSize, src.width() - x);
                if (!src.readBandWindow(srcBand, x, y, w, h, tile.data())) {
                    out.close();
                    QFile::remove(QString::fromStdString(outputPath));
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "failed reading band " + std::to_string(srcBand) +
                                              " from " + inputPath);
                }
                if (!out.writeBandWindow(k + 1, x, y, w, h, tile.data())) {
                    out.close();
                    QFile::remove(QString::fromStdString(outputPath));
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "failed writing subset band " +
                                              std::to_string(k + 1));
                }
            }
        }
        context.reportProgress(0.05 + 0.9 * (k + 1) / selectedCount,
                               "Copied band " + std::to_string(k + 1) + "/" +
                                   std::to_string(selectedCount));
    }

    // Keep the dataset-level modality tag truthful for the subset.
    QStringList modalities;
    for (const sicnu::features::FeatureBand& fb : outContract.bands) {
        if (!fb.modality.isEmpty() && !modalities.contains(fb.modality)) {
            modalities.append(fb.modality);
        }
    }
    if (!modalities.isEmpty()) {
        const QString modalityTag = modalities.size() > 1
                                        ? QStringLiteral("multimodal")
                                        : modalities.front();
        GDALSetMetadataItem(static_cast<GDALDatasetH>(out.dataset()), "SICNU_MODALITY",
                            modalityTag.toUtf8().constData(), nullptr);
    }

    // The contract must ride on the open dataset (before close); oversized
    // contracts spill to the conventional sidecar next to the output.
    const QString sidecar = QString::fromStdString(outputPath) + QStringLiteral(".features.json");
    if (!sicnu::features::writeFeatureCubeMetadata(out.dataset(), outContract, sidecar)) {
        out.close();
        QFile::remove(QString::fromStdString(outputPath));
        QFile::remove(sidecar);
        throw RSOperatorError(ErrorCode::GdalError,
                              "failed to write the feature cube contract onto " + outputPath);
    }

    outputCleanup.armed = false; // committed cleanly below

    QString closeErr;
    if (!out.closeWithError(&closeErr)) {
        QFile::remove(QString::fromStdString(outputPath));
        throw RSOperatorError(ErrorCode::GdalError,
                              "output flush failed (disk full?): " + closeErr.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    Json::Value selectedOut(Json::arrayValue);
    for (const sicnu::features::FeatureBand& fb : outContract.bands) {
        selectedOut.append(fb.id.toStdString());
    }
    result["selected"] = selectedOut;
    result["bands"] = selectedCount;
    context.reportProgress(1.0, "Feature selection complete");
    return result;
}

} // namespace sicnu::operators::rs
