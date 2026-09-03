/***************************************************************************
 * rs_spectral_index_operator.cpp  —  Spectral index RSOperator
 ***************************************************************************/
#include "rs_spectral_index_operator.h"

#include "data/band_role.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/algorithms/math_utils.h"
#include "processing/algorithms/temporal/temporal_band_roles.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h" // GdalStreamingOutput + Tile

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <functional>
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
        meta["task"] = "index-computation";
        meta["notes"] = "Role-resolved spectral indices (NDVI, EVI, SAVI, NDWI, NDBI, MNDWI). NIR/Red resolve by SICNU_BAND_ROLE when present, otherwise explicit band numbers. First analysis step after preprocessing in most workflows.";
    meta["gpu"] = false;
    meta["purpose"] = "Derive vegetation, water, soil, snow, fire, or built-up indices from multispectral imagery.";
    meta["prerequisites"].append("Input raster must have sufficient bands for the selected index.");
    meta["workflowHints"].append("Apply atmospheric correction before computing indices for best results.");
    meta["limitations"].append("Band numbers are 1-based and must exist in the input raster. When a band "
                               "parameter is omitted, it is resolved from the input's SICNU_BAND_ROLE "
                               "product metadata (semantic band roles) instead of the positional default.");
    meta["limitations"].append("EVI and SAVI constants assume unit reflectance [0,1]. When the input "
                               "carries SICNU_NUMERIC_SCALE (stamped by rs:landsat_import / "
                               "rs:sentinel2_import for verbatim DN-scale Level-2 stacks), the "
                               "participating bands are divided by it for the computation; ratio "
                               "indices are scale-invariant and inputs are never rescaled on disk.");
    meta["facadeOf"] = "rs:ndvi,rs:evi,rs:ndwi,rs:savi,rs:ndbi,rs:mndwi,rs:nbr,rs:dnbr,rs:bsi,rs:ndre,rs:ci,rs:ndsi,rs:ndti";
    return meta;
}

Json::Value RsSpectralIndexOperator::executionEstimate() const {
    // Streaming (#664): 256x256 row-blocks; the worst-case index (EVI/BSI,
    // dNBR) keeps 4 input block buffers + 1 output block in flight — about
    // 5 * 256 * 256 * 4 bytes = 1.25 MiB, rounded up to 2 MiB with overhead.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 2097152;
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

    // #680: EVI/SAVI carry additive constants (+1.0, +0.5, ·1.5) that assume
    // unit reflectance in [0,1]. Reflectance products stacked at import keep
    // their stored DN-scale pixels (back-compat) and are stamped with
    // SICNU_NUMERIC_SCALE instead; when present and != 1, the participating
    // bands are divided by it for the index computation. Ratio-based indices
    // are scale-invariant and read bands verbatim; stored input pixels are
    // never rescaled, and outputs stay in the index's native [-1, ~1] range.
    double numericScale = 1.0;
    if (void *datasetHandle = ds.dataset()) {
        if (const char *rawScale = GDALGetMetadataItem(
                static_cast<GDALDatasetH>(datasetHandle),
                SatelliteProducts::kNumericScaleKey, nullptr)) {
            bool ok = false;
            const double v = QString::fromUtf8(rawScale).toDouble(&ok);
            if (ok && std::isfinite(v) && v > 0.0)
                numericScale = v;
        }
    }
    const bool applyNumericScale = std::abs(numericScale - 1.0) > 1e-9;

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
    if (applyNumericScale && (indexName == "EVI" || indexName == "SAVI")) {
        context.logInfo("Input carries " + std::string(SatelliteProducts::kNumericScaleKey)
                        + "=" + std::to_string(numericScale)
                        + "; dividing the participating bands by it for " + indexName);
    }

    // Streaming execution (#664, ADR 0124 grade bit-exact): the raster is
    // processed in horizontal row-blocks so only O(blockRows*width) of each
    // participating band is resident, instead of full-raster buffers. Every
    // index kernel is strictly element-wise, so block-wise invocation is
    // bit-identical to a full-raster pass.
    const int blockRows = std::max(1, std::min(256, height));
    const size_t blockSize = static_cast<size_t>(width) * blockRows;

    // A (dataset, band) pair with the band's finite NoData sentinel resolved
    // once. Blocks read through it apply the same normalization the
    // full-raster path used (sentinel / non-finite -> NaN) and, for
    // scale-sensitive indices (#680), the numeric-scale normalization
    // (finite values divided by the stamped scale).
    struct BandSource
    {
        GdalDatasetWrapper *ds = nullptr;
        int band = 0;
        bool hasNodata = false;
        double nodata = 0.0;
        float invScale = 1.0f; // 1/SICNU_NUMERIC_SCALE when #680 applies, else 1
    };
    auto makeSource = [&](GdalDatasetWrapper &dataset, int bandNum,
                          bool applyScale = false) {
        BandSource src;
        src.ds = &dataset;
        src.band = bandNum;
        src.nodata = dataset.bandNoDataValue(bandNum, &src.hasNodata);
        if (src.hasNodata && !std::isfinite(src.nodata))
            src.hasNodata = false;
        if (applyScale && applyNumericScale)
            src.invScale = static_cast<float>(1.0 / numericScale);
        return src;
    };
    auto readBlock = [&](const BandSource &src, int y0, int rows, float *buf) {
        if (!src.ds->readBandWindow(src.band, 0, y0, width, rows, buf)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(src.band));
        }
        const size_t count = static_cast<size_t>(width) * rows;
        if (src.hasNodata) {
            const float nodataF = static_cast<float>(src.nodata);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i] == nodataF || !std::isfinite(buf[i]))
                    buf[i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
        if (src.invScale != 1.0f) {
            // #680: normalize to unit reflectance; NaN'd nodata passes through.
            for (size_t i = 0; i < count; ++i) {
                if (std::isfinite(buf[i]))
                    buf[i] *= src.invScale;
            }
        }
    };

    // Streaming output: tiles are written as they are computed so no
    // full-raster buffer is ever resident (#647 contract: failures/cancel
    // abandon() the partial file instead of leaving it at the output path).
    GdalStreamingOutput output(QString::fromStdString(outputPath), width, height, 1,
                               GDT_Float32, ds.geoTransform(), ds.projection());
    if (!output.isOpen()) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output raster: " + outputPath);
    }
    output.setNoDataValue(std::numeric_limits<double>::quiet_NaN());

    // Generic row-block driver: reads each participating band's block, runs
    // the element-wise branch kernel, and writes the result tile.
    const auto streamBlocks = [&](const std::vector<BandSource> &sources,
                                  const std::function<bool(const float *const *, float *, size_t)> &kernel) {
        const size_t k = sources.size();
        std::vector<std::vector<float>> in(k);
        for (auto &buf : in)
            buf.resize(blockSize);
        std::vector<float> outBlk(blockSize);
        std::vector<const float *> inPtr(k);
        const int totalBlocks = (height + blockRows - 1) / blockRows;
        int blockIndex = 0;
        for (int y0 = 0; y0 < height; y0 += blockRows, ++blockIndex) {
            context.throwIfCancelled();
            const int rows = std::min(blockRows, height - y0);
            const size_t n = static_cast<size_t>(width) * rows;
            for (size_t s = 0; s < k; ++s)
                readBlock(sources[s], y0, rows, in[s].data());
            for (size_t s = 0; s < k; ++s)
                inPtr[s] = in[s].data();
            if (!kernel(inPtr.data(), outBlk.data(), n))
                return false;
            const GdalBlockStream::Tile tile{0, y0, width, rows, 0, width, rows,
                                             blockIndex, totalBlocks};
            if (!output.writeTile(1, tile, outBlk.data()))
                return false;
            context.reportProgress(0.1 + 0.7 * (static_cast<double>(blockIndex + 1) / totalBlocks),
                                   "Computing " + indexName);
        }
        return true;
    };

    context.reportProgress(0.1, "Reading input bands");

    bool ok = false;

    if (indexName == "NDVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        ok = streamBlocks({makeSource(ds, nirBand), makeSource(ds, redBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return MathUtils::normalizedDifference(in[0], in[1], outBlk, n);
                          });
    } else if (indexName == "EVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        validateBand(blueBand, "Blue");
        // Scale-sensitive constants: normalise to unit reflectance first (#680).
        ok = streamBlocks({makeSource(ds, nirBand, true), makeSource(ds, redBand, true),
                           makeSource(ds, blueBand, true)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return SpectralIndices::evi(in[0], in[1], in[2], outBlk, n);
                          });
    } else if (indexName == "SAVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        ok = streamBlocks({makeSource(ds, nirBand, true), makeSource(ds, redBand, true)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return SpectralIndices::savi(in[0], in[1], outBlk, n);
                          });
    } else if (indexName == "NDWI") {
        validateBand(greenBand, "Green");
        validateBand(nirBand, "NIR");
        ok = streamBlocks({makeSource(ds, greenBand), makeSource(ds, nirBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return MathUtils::normalizedDifference(in[0], in[1], outBlk, n);
                          });
    } else if (indexName == "NDBI") {
        validateBand(swirBand, "SWIR");
        validateBand(nirBand, "NIR");
        ok = streamBlocks({makeSource(ds, swirBand), makeSource(ds, nirBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return MathUtils::normalizedDifference(in[0], in[1], outBlk, n);
                          });
    } else if (indexName == "MNDWI") {
        validateBand(greenBand, "Green");
        validateBand(swirBand, "SWIR");
        ok = streamBlocks({makeSource(ds, greenBand), makeSource(ds, swirBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return MathUtils::normalizedDifference(in[0], in[1], outBlk, n);
                          });
    } else if (indexName == "NBR") {
        validateBand(nirBand, "NIR");
        validateBand(swir2Band, "SWIR2");
        ok = streamBlocks({makeSource(ds, nirBand), makeSource(ds, swir2Band)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return MathUtils::normalizedDifference(in[0], in[1], outBlk, n);
                          });
    } else if (indexName == "dNBR") {
        // dNBR = NBR_pre - NBR_post, streamed across both rasters' blocks.
        std::string postfirePath;
        if (params.isMember("postfire") && params["postfire"].isString())
            postfirePath = params["postfire"].asString();
        else if (params.isMember("after") && params["after"].isString())
            postfirePath = params["after"].asString();

        GdalDatasetWrapper postDs;
        int postNirBand = 0;
        int postSwir2Band = 0;
        if (!postfirePath.empty()) {
            if (!fileExists(postfirePath)) {
                throw RSOperatorError(ErrorCode::FileNotFound, "Post-fire raster not found: " + postfirePath);
            }
            if (!postDs.open(QString::fromStdString(postfirePath))) {
                throw RSOperatorError(ErrorCode::GdalError, "Failed to open post-fire raster: " + postfirePath);
            }
            if (postDs.width() != width || postDs.height() != height) {
                throw RSOperatorError(ErrorCode::InvalidInputData, "Pre-fire and post-fire rasters must have identical dimensions");
            }
            validateBand(nirBand, "NIR (pre-fire)");
            validateBand(swir2Band, "SWIR2 (pre-fire)");
            postNirBand = params.isMember("postNir") ? getInt(params, "postNir", nirBand) : nirBand;
            postSwir2Band = params.isMember("postSwir2") ? getInt(params, "postSwir2", swir2Band) : swir2Band;
            if (postNirBand < 1 || postNirBand > postDs.bandCount() || postSwir2Band < 1 || postSwir2Band > postDs.bandCount()) {
                throw RSOperatorError(ErrorCode::InvalidParameter, "Post-fire band numbers out of range");
            }
        } else {
            // Single raster pre/post bands
            postNirBand = getInt(params, "postNir", 0);
            postSwir2Band = getInt(params, "postSwir2", 0);
            if (postNirBand < 1 || postSwir2Band < 1) {
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "dNBR requires either 'postfire' raster path or 'postNir'/'postSwir2' band numbers");
            }
            validateBand(nirBand, "NIR (pre)");
            validateBand(swir2Band, "SWIR2 (pre)");
            validateBand(postNirBand, "NIR (post)");
            validateBand(postSwir2Band, "SWIR2 (post)");
        }
        GdalDatasetWrapper &postRef = postDs.isValid() ? postDs : ds;
        std::vector<float> nbrPre, nbrPost;
        ok = streamBlocks({makeSource(ds, nirBand), makeSource(ds, swir2Band),
                           makeSource(postRef, postNirBand), makeSource(postRef, postSwir2Band)},
                          [&](const float *const *in, float *outBlk, size_t n) {
                              nbrPre.resize(n);
                              nbrPost.resize(n);
                              MathUtils::normalizedDifference(in[0], in[1], nbrPre.data(), n);
                              MathUtils::normalizedDifference(in[2], in[3], nbrPost.data(), n);
                              const float nan = std::numeric_limits<float>::quiet_NaN();
                              for (size_t i = 0; i < n; ++i) {
                                  outBlk[i] = (std::isfinite(nbrPre[i]) && std::isfinite(nbrPost[i]))
                                                  ? (nbrPre[i] - nbrPost[i])
                                                  : nan;
                              }
                              return true;
                          });
    } else if (indexName == "BSI") {
        // BSI = ((SWIR + Red) - (NIR + Blue)) / ((SWIR + Red) + (NIR + Blue))
        validateBand(swirBand, "SWIR");
        validateBand(redBand, "Red");
        validateBand(nirBand, "NIR");
        validateBand(blueBand, "Blue");
        ok = streamBlocks({makeSource(ds, swirBand), makeSource(ds, redBand),
                           makeSource(ds, nirBand), makeSource(ds, blueBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              const float nan = std::numeric_limits<float>::quiet_NaN();
                              const float *swir = in[0];
                              const float *red = in[1];
                              const float *nirB = in[2];
                              const float *blue = in[3];
                              for (size_t i = 0; i < n; ++i) {
                                  const float s = swir[i];
                                  const float r = red[i];
                                  const float nv = nirB[i];
                                  const float b = blue[i];
                                  if (!std::isfinite(s) || !std::isfinite(r)
                                      || !std::isfinite(nv) || !std::isfinite(b)) {
                                      outBlk[i] = nan;
                                      continue;
                                  }
                                  const float num = (s + r) - (nv + b);
                                  const float denom = (s + r) + (nv + b);
                                  outBlk[i] = MathUtils::safeDiv(num, denom);
                              }
                              return true;
                          });
    } else if (indexName == "NDRE") {
        // NDRE = (NIR - RedEdge) / (NIR + RedEdge)
        validateBand(nirBand, "NIR");
        validateBand(redEdgeBand, "RedEdge");
        ok = streamBlocks({makeSource(ds, nirBand), makeSource(ds, redEdgeBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return MathUtils::normalizedDifference(in[0], in[1], outBlk, n);
                          });
    } else if (indexName == "CI") {
        // CI = (NIR / RedEdge) - 1.0 = (NIR - RedEdge) / RedEdge
        validateBand(nirBand, "NIR");
        validateBand(redEdgeBand, "RedEdge");
        ok = streamBlocks({makeSource(ds, nirBand), makeSource(ds, redEdgeBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              const float nan = std::numeric_limits<float>::quiet_NaN();
                              const float *nirB = in[0];
                              const float *re = in[1];
                              for (size_t i = 0; i < n; ++i) {
                                  if (!std::isfinite(nirB[i]) || !std::isfinite(re[i]) || re[i] == 0.0f)
                                      outBlk[i] = nan;
                                  else
                                      outBlk[i] = (nirB[i] / re[i]) - 1.0f;
                              }
                              return true;
                          });
    } else if (indexName == "NDSI") {
        // NDSI = (Green - SWIR) / (Green + SWIR)
        validateBand(greenBand, "Green");
        validateBand(swirBand, "SWIR");
        ok = streamBlocks({makeSource(ds, greenBand), makeSource(ds, swirBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return MathUtils::normalizedDifference(in[0], in[1], outBlk, n);
                          });
    } else if (indexName == "NDTI") {
        // NDTI = (SWIR1 - SWIR2) / (SWIR1 + SWIR2)
        validateBand(swirBand, "SWIR1");
        validateBand(swir2Band, "SWIR2");
        ok = streamBlocks({makeSource(ds, swirBand), makeSource(ds, swir2Band)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return MathUtils::normalizedDifference(in[0], in[1], outBlk, n);
                          });
    }

    context.throwIfCancelled();
    if (!ok) {
        output.abandon();
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Spectral index computation failed");
    }
    QString closeError;
    if (!output.closeWithError(&closeError)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to write output raster: " + closeError.toStdString());
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
