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
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

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

    // 1-based band number carrying @a role, or 0 when the input has no such
    // role (plain rasters without product metadata return 0).
    auto bandWithRole = [&](sicnu::data::BandRole role) {
        const QByteArray roleId = sicnu::data::bandRoleToString(role).toLatin1();
        for (int b = 1; b <= bandCount; ++b) {
            if (ds.bandMetadataItem(b, "SICNU_BAND_ROLE") == QLatin1String(roleId))
                return b;
        }
        return 0;
    };

    int nirBand = nirExplicit;
    int redBand = redExplicit;
    int greenBand = greenExplicit;
    int blueBand = blueExplicit;
    int swirBand = swirExplicit;
    int swir2Band = swir2Explicit;
    int redEdgeBand = redEdgeExplicit;
    bool anyHardcodedFallback = false;

    if (!hasNir) {
        nirBand = bandWithRole(sicnu::data::BandRole::NIR);
        if (nirBand <= 0) {
            nirBand = 4;
            anyHardcodedFallback = true;
        }
    }
    if (!hasRed) {
        redBand = bandWithRole(sicnu::data::BandRole::Red);
        if (redBand <= 0) {
            redBand = 3;
            anyHardcodedFallback = true;
        }
    }
    if (!hasGreen) {
        greenBand = bandWithRole(sicnu::data::BandRole::Green);
        if (greenBand <= 0) {
            greenBand = 2;
            anyHardcodedFallback = true;
        }
    }
    if (!hasBlue) {
        blueBand = bandWithRole(sicnu::data::BandRole::Blue);
        if (blueBand <= 0) {
            blueBand = 1;
            anyHardcodedFallback = true;
        }
    }
    if (!hasSwir) {
        swirBand = bandWithRole(sicnu::data::BandRole::SWIR1);
        if (swirBand <= 0)
            swirBand = bandWithRole(sicnu::data::BandRole::SWIR2);
        if (swirBand <= 0) {
            swirBand = 5;
            anyHardcodedFallback = true;
        }
    }
    if (!hasSwir2) {
        swir2Band = bandWithRole(sicnu::data::BandRole::SWIR2);
        if (swir2Band <= 0)
            swir2Band = bandWithRole(sicnu::data::BandRole::SWIR1);
        if (swir2Band <= 0) {
            swir2Band = std::min(6, bandCount);
            anyHardcodedFallback = true;
        }
    }
    if (!hasRedEdge) {
        redEdgeBand = bandWithRole(sicnu::data::BandRole::RedEdge);
        if (redEdgeBand <= 0) {
            redEdgeBand = std::min(5, bandCount);
            anyHardcodedFallback = true;
        }
    }

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

    // Streaming execution (#664, ADR 0124 grade bit-exact): the raster is
    // processed in horizontal row-blocks so only O(blockRows*width) of each
    // participating band is resident, instead of the former full-raster
    // buffers. Every index kernel is strictly element-wise, so block-wise
    // invocation is bit-identical to the previous full-raster path.
    const int blockRows = std::max(1, std::min(256, height));
    const size_t blockSize = static_cast<size_t>(width) * blockRows;

    // A (dataset, band) pair with the band's finite NoData sentinel resolved
    // once; blocks read through it apply the same normalization the former
    // full-raster path used (sentinel / non-finite -> NaN).
    struct BandSource
    {
        GdalDatasetWrapper *ds = nullptr;
        int band = 0;
        bool hasNodata = false;
        double nodata = 0.0;
    };
    auto makeSource = [&](GdalDatasetWrapper &d, int bandNum) {
        BandSource src;
        src.ds = &d;
        src.band = bandNum;
        src.nodata = d.bandNoDataValue(bandNum, &src.hasNodata);
        if (src.hasNodata && !std::isfinite(src.nodata))
            src.hasNodata = false;
        return src;
    };
    auto readBlock = [&](const BandSource &src, int y0, int rows, float *buf) {
        if (!src.ds->readBandWindow(src.band, 0, y0, width, rows, buf)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(src.band));
        }
        if (src.hasNodata) {
            const float nodataF = static_cast<float>(src.nodata);
            const size_t count = static_cast<size_t>(width) * rows;
            for (size_t i = 0; i < count; ++i) {
                if (buf[i] == nodataF || !std::isfinite(buf[i]))
                    buf[i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    };

    GdalStreamingOutput output(QString::fromStdString(outputPath), width, height, 1,
                               GDT_Float32, ds.geoTransform(), ds.projection());
    if (!output.isOpen()) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output raster: " + outputPath);
    }
    output.setNoDataValue(std::numeric_limits<double>::quiet_NaN());

    // Generic row-block driver: reads each participating band's block, runs
    // the branch kernel (strictly element-wise), and writes the result tile.
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

    // Any failure/cancel after this point must not leave a partial raster at
    // the output path (#647 streaming-output contract).
    try {
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
        ok = streamBlocks({makeSource(ds, nirBand), makeSource(ds, redBand), makeSource(ds, blueBand)},
                          [](const float *const *in, float *outBlk, size_t n) {
                              return SpectralIndices::evi(in[0], in[1], in[2], outBlk, n);
                          });
    } else if (indexName == "SAVI") {
        validateBand(nirBand, "NIR");
        validateBand(redBand, "Red");
        ok = streamBlocks({makeSource(ds, nirBand), makeSource(ds, redBand)},
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
        // dNBR = NBR_pre - NBR_post
        GdalDatasetWrapper postDs;
        int postNirBand = 0;
        int postSwir2Band = 0;
        if (params.isMember("postfire") && params["postfire"].isString()
                && !params["postfire"].asString().empty()) {
            const std::string postfirePath = params["postfire"].asString();
            if (!fileExists(postfirePath)) {
                throw RSOperatorError(ErrorCode::FileNotFound, "Post-fire raster not found: " + postfirePath);
            }
            if (!postDs.open(QString::fromStdString(postfirePath))) {
                throw RSOperatorError(ErrorCode::GdalError, "Failed to open post-fire raster: " + postfirePath);
            }
            if (postDs.width() != width || postDs.height() != height) {
                throw RSOperatorError(ErrorCode::InvalidInputData, "Pre-fire and post-fire rasters must have identical dimensions");
            }
            postNirBand = params.isMember("postNir") ? getInt(params, "postNir", nirBand) : nirBand;
            postSwir2Band = params.isMember("postSwir2") ? getInt(params, "postSwir2", swir2Band) : swir2Band;
        } else {
            // Single raster pre/post bands
            postNirBand = getInt(params, "postNir", 0);
            postSwir2Band = getInt(params, "postSwir2", 0);
            if (postNirBand < 1 || postSwir2Band < 1) {
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "dNBR requires either 'postfire' raster path or 'postNir'/'postSwir2' band numbers");
            }
        }
        if (postDs.isValid()) {
            validateBand(nirBand, "NIR (pre-fire)");
            validateBand(swir2Band, "SWIR2 (pre-fire)");
            if (postNirBand < 1 || postNirBand > postDs.bandCount()
                    || postSwir2Band < 1 || postSwir2Band > postDs.bandCount()) {
                throw RSOperatorError(ErrorCode::InvalidParameter, "Post-fire band numbers out of range");
            }
        } else {
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
                                  const float sv = swir[i];
                                  const float rv = red[i];
                                  const float nv = nirB[i];
                                  const float bv = blue[i];
                                  if (!std::isfinite(sv) || !std::isfinite(rv) || !std::isfinite(nv) || !std::isfinite(bv)) {
                                      outBlk[i] = nan;
                                      continue;
                                  }
                                  const float num = (sv + rv) - (nv + bv);
                                  const float denom = (sv + rv) + (nv + bv);
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
    } catch (...) {
        output.abandon();
        throw;
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
