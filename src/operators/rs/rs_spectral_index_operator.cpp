/***************************************************************************
 * rs_spectral_index_operator.cpp  —  Spectral index RSOperator
 ***************************************************************************/
#include "rs_spectral_index_operator.h"

#include "data/band_role.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/algorithms/math_utils.h"
#include "processing/algorithms/temporal/temporal_band_roles.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <cmath>
#include <limits>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_indices = {
    "NDVI", "EVI", "SAVI", "NDWI", "NDBI", "MNDWI",
    "NBR", "dNBR", "BSI", "NDRE", "CI", "NDSI", "NDTI"
};

} // anonymous namespace

Json::Value RsSpectralIndexOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output single-band index raster", "tif");
    props["index"] = makeEnumParam("index", "Spectral index to compute", s_indices, "NDVI");
    props["nir"] = makeIntegerParam("nir", "1-based NIR band number (optional; when omitted, resolved from the input's product band roles)", 4);
    props["red"] = makeIntegerParam("red", "1-based Red band number (optional; when omitted, resolved from the input's product band roles)", 3);
    props["green"] = makeIntegerParam("green", "1-based Green band number (optional; when omitted, resolved from the input's product band roles)", 2);
    props["blue"] = makeIntegerParam("blue", "1-based Blue band number (optional; when omitted, resolved from the input's product band roles)", 1);
    props["swir"] = makeIntegerParam("swir", "1-based SWIR/SWIR1 band number (optional; when omitted, resolved from the input's product band roles)", 5);
    props["swir2"] = makeIntegerParam("swir2", "1-based SWIR2 band number (optional; when omitted, resolved from the input's product band roles)", 6);
    props["rededge"] = makeIntegerParam("rededge", "1-based RedEdge band number (optional; when omitted, resolved from the input's product band roles)", 5);
    props["postfire"] = makeRasterParam("postfire", "Optional post-fire raster path for dNBR computation", false);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["index"] = makeStringParam("index", "Computed index name", "");
    outputs["width"] = makeIntegerParam("width", "Output width", 0);
    outputs["height"] = makeIntegerParam("height", "Output height", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output", "index"});
    return root;
}

Json::Value RsSpectralIndexOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("spectral");
    meta["tags"].append("ndvi");
    meta["tags"].append("evi");
    meta["tags"].append("nbr");
    meta["tags"].append("dnbr");
    meta["tags"].append("bsi");
    meta["tags"].append("ndre");
    meta["tags"].append("ci");
    meta["tags"].append("ndsi");
    meta["tags"].append("ndti");
    meta["tags"].append("vegetation");
    meta["purpose"] = "Derive vegetation, water, soil, snow, fire, or built-up indices from multispectral imagery.";
    meta["prerequisites"].append("Input raster must have sufficient bands for the selected index.");
    meta["workflowHints"].append("Apply atmospheric correction before computing indices for best results.");
    meta["limitations"].append("Band numbers are 1-based and must exist in the input raster. When a band "
                               "parameter is omitted, it is resolved from the input's SICNU_BAND_ROLE "
                               "product metadata (semantic band roles) instead of the positional default.");
    meta["facadeOf"] = "rs:ndvi,rs:evi,rs:ndwi,rs:savi,rs:ndbi,rs:mndwi,rs:nbr,rs:dnbr,rs:bsi,rs:ndre,rs:ci,rs:ndsi,rs:ndti";
    return meta;
}

Json::Value RsSpectralIndexOperator::executionEstimate() const {
    // FullRaster (base default): no preferred tile; the whole input raster is
    // resident. Typical input 1024x1024x4 float32 (~4 MiB/band); the worst-case
    // index (EVI/BSI) keeps 4 input bands + 1 output buffer in flight at once.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 33554432;
    return est;
}

namespace spectral_index_detail {

Json::Value runSpectralIndexCore(const std::string& defaultIndex,
                                 const Json::Value& params,
                                 RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string indexName = params.isMember("index")
                                      ? getEnum(params, "index", s_indices, defaultIndex)
                                      : defaultIndex;

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    // Explicit band parameters win; when omitted, bands are resolved from the
    // input's semantic band roles (SICNU_BAND_ROLE product metadata).
    const bool hasNir = params.isMember("nir");
    const bool hasRed = params.isMember("red");
    const bool hasGreen = params.isMember("green");
    const bool hasBlue = params.isMember("blue");
    const bool hasSwir = params.isMember("swir");
    const bool hasSwir2 = params.isMember("swir2");
    const bool hasRedEdge = params.isMember("rededge") || params.isMember("red_edge");

    const int nirExplicit = getInt(params, "nir", 4);
    const int redExplicit = getInt(params, "red", 3);
    const int greenExplicit = getInt(params, "green", 2);
    const int blueExplicit = getInt(params, "blue", 1);
    const int swirExplicit = getInt(params, "swir", 5);
    const int swir2Explicit = getInt(params, "swir2", 6);
    const int redEdgeExplicit = params.isMember("rededge")
                                    ? getInt(params, "rededge", 5)
                                    : getInt(params, "red_edge", 5);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);
    }

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();

    // Band resolution is delegated to the shared temporal resolver
    // (explicit > SICNU_BAND_ROLE > SWIR1/SWIR2 cross-fallback > positional
    // defaults) so single-scene and temporal-series operators cannot drift.
    int nirBand, redBand, greenBand, blueBand, swirBand, swir2Band, redEdgeBand;
    bool anyHardcodedFallback = false;
    auto resolveWithFlag = [&](const char *roleId, int explicitBand, bool hasExplicit) {
        if (hasExplicit)
            return explicitBand;
        bool fallback = false;
        const int band = sicnu::temporal::resolveBand(ds, QLatin1String(roleId), 0, &fallback);
        anyHardcodedFallback = anyHardcodedFallback || fallback;
        return band;
    };
    nirBand = resolveWithFlag("nir", nirExplicit, hasNir);
    redBand = resolveWithFlag("red", redExplicit, hasRed);
    greenBand = resolveWithFlag("green", greenExplicit, hasGreen);
    blueBand = resolveWithFlag("blue", blueExplicit, hasBlue);
    swirBand = resolveWithFlag("swir1", swirExplicit, hasSwir);
    swir2Band = resolveWithFlag("swir2", swir2Explicit, hasSwir2);
    redEdgeBand = resolveWithFlag("red_edge", redEdgeExplicit, hasRedEdge);

    if (anyHardcodedFallback) {
        context.logWarning(
            "No semantic band roles (SICNU_BAND_ROLE) on the input and no "
            "explicit band parameters; assuming conventional band numbering. "
            "Verify with describe_dataset or pass band parameters explicitly.");
    }

    auto validateBand = [&](int bandNum, const std::string& label) {
        if (bandNum < 1 || bandNum > bandCount) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  label + " band " + std::to_string(bandNum) +
                                      " is out of range (1-" + std::to_string(bandCount) + ")");
        }
    };

    context.logInfo("Computing " + indexName + " from " + inputPath);

    std::vector<float> nir, red, green, blue, swir, swir2, redEdge, out;

    auto readBandFromDs = [&](GdalDatasetWrapper &dataset, int bandNum, std::vector<float>& buffer) {
        buffer.resize(static_cast<size_t>(dataset.width()) * dataset.height());
        if (!dataset.readBandData(bandNum, buffer.data(), dataset.width(), dataset.height())) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bandNum));
        }
        bool hasNodata = false;
        double nodataVal = dataset.bandNoDataValue(bandNum, &hasNodata);
        if (hasNodata && std::isfinite(nodataVal)) {
            const float nodataF = static_cast<float>(nodataVal);
            for (float &val : buffer) {
                if (val == nodataF || !std::isfinite(val)) {
                    val = std::numeric_limits<float>::quiet_NaN();
                }
            }
        }
    };

    auto readBand = [&](int bandNum, std::vector<float>& buffer) {
        readBandFromDs(ds, bandNum, buffer);
    };

    context.reportProgress(0.1, "Reading input bands");

    const size_t totalPixels = static_cast<size_t>(width) * height;
    out.resize(totalPixels);
    bool ok = false;

    if (indexName == "NDVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        readBand(nirBand, nir);
        readBand(redBand, red);
        ok = MathUtils::normalizedDifference(nir.data(), red.data(), out.data(), out.size());
    } else if (indexName == "EVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        validateBand(blueBand, "Blue");
        readBand(nirBand, nir);
        readBand(redBand, red);
        readBand(blueBand, blue);
        ok = SpectralIndices::evi(nir.data(), red.data(), blue.data(), out.data(), out.size());
    } else if (indexName == "SAVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        readBand(nirBand, nir);
        readBand(redBand, red);
        ok = SpectralIndices::savi(nir.data(), red.data(), out.data(), out.size());
    } else if (indexName == "NDWI") {
        validateBand(greenBand, "Green");
        validateBand(nirBand, "NIR");
        readBand(greenBand, green);
        readBand(nirBand, nir);
        ok = MathUtils::normalizedDifference(green.data(), nir.data(), out.data(), out.size());
    } else if (indexName == "NDBI") {
        validateBand(swirBand, "SWIR");
        validateBand(nirBand, "NIR");
        readBand(swirBand, swir);
        readBand(nirBand, nir);
        ok = MathUtils::normalizedDifference(swir.data(), nir.data(), out.data(), out.size());
    } else if (indexName == "MNDWI") {
        validateBand(greenBand, "Green");
        validateBand(swirBand, "SWIR");
        readBand(greenBand, green);
        readBand(swirBand, swir);
        ok = MathUtils::normalizedDifference(green.data(), swir.data(), out.data(), out.size());
    } else if (indexName == "NBR") {
        validateBand(nirBand, "NIR");
        validateBand(swir2Band, "SWIR2");
        readBand(nirBand, nir);
        readBand(swir2Band, swir2);
        ok = MathUtils::normalizedDifference(nir.data(), swir2.data(), out.data(), out.size());
    } else if (indexName == "dNBR") {
        // dNBR = NBR_pre - NBR_post
        std::string postfirePath;
        if (params.isMember("postfire") && params["postfire"].isString())
            postfirePath = params["postfire"].asString();
        else if (params.isMember("after") && params["after"].isString())
            postfirePath = params["after"].asString();

        if (!postfirePath.empty()) {
            if (!fileExists(postfirePath)) {
                throw RSOperatorError(ErrorCode::FileNotFound, "Post-fire raster not found: " + postfirePath);
            }
            GdalDatasetWrapper postDs;
            if (!postDs.open(QString::fromStdString(postfirePath))) {
                throw RSOperatorError(ErrorCode::GdalError, "Failed to open post-fire raster: " + postfirePath);
            }
            if (postDs.width() != width || postDs.height() != height) {
                throw RSOperatorError(ErrorCode::InvalidInputData, "Pre-fire and post-fire rasters must have identical dimensions");
            }
            validateBand(nirBand, "NIR (pre-fire)");
            validateBand(swir2Band, "SWIR2 (pre-fire)");
            readBand(nirBand, nir);
            readBand(swir2Band, swir2);

            std::vector<float> nbrPre(totalPixels), nbrPost(totalPixels), postNir, postSwir2;
            MathUtils::normalizedDifference(nir.data(), swir2.data(), nbrPre.data(), totalPixels);

            int postNirBand = params.isMember("postNir") ? getInt(params, "postNir", nirBand) : nirBand;
            int postSwir2Band = params.isMember("postSwir2") ? getInt(params, "postSwir2", swir2Band) : swir2Band;
            if (postNirBand < 1 || postNirBand > postDs.bandCount() || postSwir2Band < 1 || postSwir2Band > postDs.bandCount()) {
                throw RSOperatorError(ErrorCode::InvalidParameter, "Post-fire band numbers out of range");
            }
            readBandFromDs(postDs, postNirBand, postNir);
            readBandFromDs(postDs, postSwir2Band, postSwir2);
            MathUtils::normalizedDifference(postNir.data(), postSwir2.data(), nbrPost.data(), totalPixels);

            for (size_t i = 0; i < totalPixels; ++i) {
                out[i] = (std::isfinite(nbrPre[i]) && std::isfinite(nbrPost[i]))
                             ? (nbrPre[i] - nbrPost[i])
                             : std::numeric_limits<float>::quiet_NaN();
            }
            ok = true;
        } else {
            // Single raster pre/post bands
            const int postNirBand = getInt(params, "postNir", 0);
            const int postSwir2Band = getInt(params, "postSwir2", 0);
            if (postNirBand >= 1 && postSwir2Band >= 1) {
                validateBand(nirBand, "NIR (pre)");
                validateBand(swir2Band, "SWIR2 (pre)");
                validateBand(postNirBand, "NIR (post)");
                validateBand(postSwir2Band, "SWIR2 (post)");
                readBand(nirBand, nir);
                readBand(swir2Band, swir2);
                std::vector<float> postNir, postSwir2, nbrPre(totalPixels), nbrPost(totalPixels);
                readBand(postNirBand, postNir);
                readBand(postSwir2Band, postSwir2);
                MathUtils::normalizedDifference(nir.data(), swir2.data(), nbrPre.data(), totalPixels);
                MathUtils::normalizedDifference(postNir.data(), postSwir2.data(), nbrPost.data(), totalPixels);
                for (size_t i = 0; i < totalPixels; ++i) {
                    out[i] = (std::isfinite(nbrPre[i]) && std::isfinite(nbrPost[i]))
                                 ? (nbrPre[i] - nbrPost[i])
                                 : std::numeric_limits<float>::quiet_NaN();
                }
                ok = true;
            } else {
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "dNBR requires either 'postfire' raster path or 'postNir'/'postSwir2' band numbers");
            }
        }
    } else if (indexName == "BSI") {
        // BSI = ((SWIR + Red) - (NIR + Blue)) / ((SWIR + Red) + (NIR + Blue))
        validateBand(swirBand, "SWIR");
        validateBand(redBand, "Red");
        validateBand(nirBand, "NIR");
        validateBand(blueBand, "Blue");
        readBand(swirBand, swir);
        readBand(redBand, red);
        readBand(nirBand, nir);
        readBand(blueBand, blue);

        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (size_t i = 0; i < totalPixels; ++i) {
            const float s = swir[i];
            const float r = red[i];
            const float n = nir[i];
            const float b = blue[i];
            if (!std::isfinite(s) || !std::isfinite(r) || !std::isfinite(n) || !std::isfinite(b)) {
                out[i] = nan;
                continue;
            }
            const float num = (s + r) - (n + b);
            const float denom = (s + r) + (n + b);
            out[i] = MathUtils::safeDiv(num, denom);
        }
        ok = true;
    } else if (indexName == "NDRE") {
        // NDRE = (NIR - RedEdge) / (NIR + RedEdge)
        validateBand(nirBand, "NIR");
        validateBand(redEdgeBand, "RedEdge");
        readBand(nirBand, nir);
        readBand(redEdgeBand, redEdge);
        ok = MathUtils::normalizedDifference(nir.data(), redEdge.data(), out.data(), out.size());
    } else if (indexName == "CI") {
        // CI = (NIR / RedEdge) - 1.0 = (NIR - RedEdge) / RedEdge
        validateBand(nirBand, "NIR");
        validateBand(redEdgeBand, "RedEdge");
        readBand(nirBand, nir);
        readBand(redEdgeBand, redEdge);

        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (size_t i = 0; i < totalPixels; ++i) {
            const float n = nir[i];
            const float re = redEdge[i];
            if (!std::isfinite(n) || !std::isfinite(re) || re == 0.0f) {
                out[i] = nan;
            } else {
                out[i] = (n / re) - 1.0f;
            }
        }
        ok = true;
    } else if (indexName == "NDSI") {
        // NDSI = (Green - SWIR) / (Green + SWIR)
        validateBand(greenBand, "Green");
        validateBand(swirBand, "SWIR");
        readBand(greenBand, green);
        readBand(swirBand, swir);
        ok = MathUtils::normalizedDifference(green.data(), swir.data(), out.data(), out.size());
    } else if (indexName == "NDTI") {
        // NDTI = (SWIR1 - SWIR2) / (SWIR1 + SWIR2)
        validateBand(swirBand, "SWIR1");
        validateBand(swir2Band, "SWIR2");
        readBand(swirBand, swir);
        readBand(swir2Band, swir2);
        ok = MathUtils::normalizedDifference(swir.data(), swir2.data(), out.data(), out.size());
    }

    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Spectral index computation failed");
    }

    context.throwIfCancelled();
    context.reportProgress(0.7, "Writing output raster");

    std::vector<std::vector<float>> bands = {std::move(out)};
    QString errorMessage;
    if (!writeGdalOutput(QString::fromStdString(outputPath), width, height, bands,
                         ds.geoTransform(), ds.projection(), &errorMessage,
                         std::numeric_limits<double>::quiet_NaN())) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + errorMessage.toStdString());
    }

    ds.close();

    context.reportProgress(1.0, indexName + " complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["index"] = indexName;
    result["width"] = width;
    result["height"] = height;
    return result;
}

} // namespace spectral_index_detail

Json::Value RsSpectralIndexOperator::run(const Json::Value& params,
                                         RSOperatorContext& context) {
    return spectral_index_detail::runSpectralIndexCore("NDVI", params, context);
}

} // namespace sicnu::operators::rs
