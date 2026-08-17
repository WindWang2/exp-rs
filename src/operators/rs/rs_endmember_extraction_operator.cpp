/***************************************************************************
 * rs_endmember_extraction_operator.cpp  —  PPI endmember extraction RSOperator
 ***************************************************************************/
#include "rs_endmember_extraction_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/endmember_extraction.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsEndmemberExtractionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["nEndmembers"] = makeIntegerParam("nEndmembers", "Number of endmembers to extract", 0);
    props["projections"] = makeIntegerParam("projections", "Random projections (min 16)", 1000);

    Json::Value outputs(Json::objectValue);
    outputs["endmembers"] = Json::Value(Json::objectValue);
    outputs["endmembers"]["type"] = "array";
    outputs["endmembers"]["description"] = "Extracted endmember spectra";
    outputs["indices"] = Json::Value(Json::objectValue);
    outputs["indices"]["type"] = "array";
    outputs["indices"]["description"] = "Source pixel index per endmember";

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "nEndmembers"});
    return root;
}

Json::Value RsEndmemberExtractionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("endmember");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("ppi");
    meta["purpose"] = "Extract the purest spectra of a scene by Pixel Purity Index "
                     "for spectral unmixing / matching.";
    meta["workflowHints"].append("Feed the returned endmembers into rs:spectral_unmixing "
                                 "or rs:sam_classify.");
    meta["limitations"].append("PPI finds pixels at the data hull; it assumes endmembers "
                               "are present as pure pixels in the scene.");
    return meta;
}

Json::Value RsEndmemberExtractionOperator::executionEstimate() const {
    // MultiPassStreaming: 256x256 tile buffers (all bands BIP), the projection
    // directions (projections x bands) and per-pixel PPI counts (4 B/px) — no
    // full-scene pixel buffer. For a 1024x1024x4 float32 input: 4 MiB tile
    // buffers + 1000x4x8 B directions + 4 MiB counts ≈ 9 MiB regardless of
    // band-materialized pixel buffer.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 9437184; // ~9 MiB nominal
    return est;
}

Json::Value RsEndmemberExtractionOperator::run(const Json::Value& params,
                                               RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);

    const int nEndmembers = getInt(params, "nEndmembers", 0);
    if (nEndmembers < 1)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "nEndmembers must be at least 1");
    const int projections = getInt(params, "projections", 1000);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if (bandCount < 2)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Endmember extraction requires at least 2 bands, got "
                                  + std::to_string(bandCount));

    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (static_cast<size_t>(nEndmembers) > pixelCount)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "nEndmembers exceeds the pixel count");
    if (projections < 16)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "projections must be at least 16");

    // --- Streaming Pixel Purity Index (3 passes, memory-bounded). ----------
    // Reproduces EndmemberExtraction::pixelPurityIndex deterministically (same
    // RNG sequence, same extreme semantics) but keeps only O(tilePixels*bands
    // + projections*bands + pixelCount*sizeof(int)) in memory instead of a
    // full pixelCount*bands float buffer.
    constexpr int kTile = 256;
    const size_t maxTilePixels = static_cast<size_t>(kTile) * kTile;
    const size_t B = static_cast<size_t>(bandCount);
    std::vector<float> tileBip(maxTilePixels * B, 0.0f);
    std::vector<float> bandScratch(maxTilePixels);
    std::vector<int> bandList(bandCount);
    for (int b = 0; b < bandCount; ++b)
        bandList[b] = b + 1;

    auto streamTiles = [&](const auto &fn) {
        size_t cursor = 0;
        for (int y = 0; y < height; y += kTile)
        {
            const int h = std::min(kTile, height - y);
            for (int x = 0; x < width; x += kTile)
            {
                const int w = std::min(kTile, width - x);
                const size_t n = static_cast<size_t>(w) * h;
                context.throwIfCancelled();
                if (!ds.readWindowBip(bandList, x, y, w, h, tileBip.data()))
                {
                    for (int b = 0; b < bandCount; ++b)
                    {
                        if (!ds.readBandWindow(b + 1, x, y, w, h, bandScratch.data()))
                            throw RSOperatorError(ErrorCode::GdalError,
                                                  "Failed to read tile at (" +
                                                      std::to_string(x) + ", " + std::to_string(y) + ")");
                        for (size_t p = 0; p < n; ++p)
                            tileBip[p * B + static_cast<size_t>(b)] = bandScratch[p];
                    }
                }
                if (!fn(tileBip.data(), n, cursor))
                    return false;
                cursor += n;
            }
        }
        return true;
    };

    std::vector<float> noDataValues(bandCount, std::numeric_limits<float>::quiet_NaN());
    std::vector<bool> hasNoData(bandCount, false);
    for (int b = 0; b < bandCount; ++b)
    {
        bool hasNd = false;
        const double nd = ds.bandNoDataValue(b + 1, &hasNd);
        if (hasNd)
        {
            hasNoData[b] = true;
            noDataValues[b] = static_cast<float>(nd);
        }
    }

    auto isPixelValid = [&](const float *spec) {
        for (int b = 0; b < bandCount; ++b)
        {
            const float v = spec[b];
            if (!std::isfinite(v))
                return false;
            if (hasNoData[b] && std::abs(v - noDataValues[b]) < 1e-3f)
                return false;
        }
        return true;
    };

    context.reportProgress(0.1, "PPI pass 1/3: mean");
    std::vector<double> mean(B, 0.0);
    uint64_t validPixelCount = 0;
    streamTiles([&](const float *tile, size_t n, size_t) {
        for (size_t p = 0; p < n; ++p)
        {
            const float *spec = tile + p * B;
            if (!isPixelValid(spec))
                continue;
            for (size_t b = 0; b < B; ++b)
                mean[b] += spec[b];
            ++validPixelCount;
        }
        return true;
    });
    if (validPixelCount == 0)
        throw RSOperatorError(ErrorCode::InvalidInputData, "No valid pixels found in raster");
    for (size_t b = 0; b < B; ++b)
        mean[b] /= static_cast<double>(validPixelCount);

    // Random projection directions — identical sequence to the full-scene
    // kernel (mt19937(42), std::normal_distribution, per-projection normalize).
    std::mt19937 rng(42);
    std::normal_distribution<double> normal(0.0, 1.0);
    std::vector<std::vector<double>> dirs(static_cast<size_t>(projections),
                                          std::vector<double>(bandCount));
    std::vector<bool> dirValid(static_cast<size_t>(projections), true);
    for (int proj = 0; proj < projections; ++proj)
    {
        double norm = 0.0;
        for (int b = 0; b < bandCount; ++b)
        {
            dirs[static_cast<size_t>(proj)][static_cast<size_t>(b)] = normal(rng);
            norm += dirs[static_cast<size_t>(proj)][static_cast<size_t>(b)]
                    * dirs[static_cast<size_t>(proj)][static_cast<size_t>(b)];
        }
        norm = std::sqrt(norm);
        if (norm < 1e-12)
        {
            dirValid[static_cast<size_t>(proj)] = false;
            continue;
        }
        for (double &v : dirs[static_cast<size_t>(proj)])
            v /= norm;
    }

    context.reportProgress(0.45, "PPI pass 2/3: projection extremes");
    std::vector<size_t> minP(static_cast<size_t>(projections), 0);
    std::vector<size_t> maxP(static_cast<size_t>(projections), 0);
    std::vector<double> minV(static_cast<size_t>(projections), 0.0);
    std::vector<double> maxV(static_cast<size_t>(projections), 0.0);
    bool firstPixel = true;
    std::vector<double> centered( static_cast<size_t>( bandCount ) );
    streamTiles([&](const float *tile, size_t n, size_t base) {
        for (size_t p = 0; p < n; ++p)
        {
            const float *spectrum = tile + p * B;
            if (!isPixelValid(spectrum))
                continue;
            for (int b = 0; b < bandCount; ++b)
                centered[static_cast<size_t>(b)] = static_cast<double>(spectrum[b]) - mean[static_cast<size_t>(b)];

            const size_t globalPixel = base + p;
            if (firstPixel)
            {
                for (int proj = 0; proj < projections; ++proj)
                {
                    if (!dirValid[static_cast<size_t>(proj)])
                        continue;
                    const double *dir = dirs[static_cast<size_t>(proj)].data();
                    double v = 0.0;
                    for (int b = 0; b < bandCount; ++b)
                        v += dir[b] * centered[static_cast<size_t>(b)];
                    minV[static_cast<size_t>(proj)] = v;
                    maxV[static_cast<size_t>(proj)] = v;
                    minP[static_cast<size_t>(proj)] = globalPixel;
                    maxP[static_cast<size_t>(proj)] = globalPixel;
                }
                firstPixel = false;
            }
            else
            {
                for (int proj = 0; proj < projections; ++proj)
                {
                    if (!dirValid[static_cast<size_t>(proj)])
                        continue;
                    const double *dir = dirs[static_cast<size_t>(proj)].data();
                    double v = 0.0;
                    for (int b = 0; b < bandCount; ++b)
                        v += dir[b] * centered[static_cast<size_t>(b)];
                    if (v < minV[static_cast<size_t>(proj)])
                    {
                        minV[static_cast<size_t>(proj)] = v;
                        minP[static_cast<size_t>(proj)] = globalPixel;
                    }
                    if (v > maxV[static_cast<size_t>(proj)])
                    {
                        maxV[static_cast<size_t>(proj)] = v;
                        maxP[static_cast<size_t>(proj)] = globalPixel;
                    }
                }
            }
        }
        return true;
    });

    // Tally the extreme hits (the PPI counts array — O(pixels) ints).
    std::vector<int> counts(pixelCount, 0);
    for (int proj = 0; proj < projections; ++proj)
    {
        if (!dirValid[static_cast<size_t>(proj)])
            continue;
        ++counts[static_cast<size_t>(minP[static_cast<size_t>(proj)])];
        ++counts[static_cast<size_t>(maxP[static_cast<size_t>(proj)])];
    }

    // Rank by PPI count, tie-broken by index for determinism.
    std::vector<size_t> order(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (counts[a] != counts[b])
            return counts[a] > counts[b];
        return a < b;
    });

    context.reportProgress(0.8, "PPI pass 3/3: extracting endmembers");
    EndmemberExtraction::EndmemberResult result;
    result.endmembers.resize(static_cast<size_t>(nEndmembers) * bandCount);
    result.endmemberIndices.resize(nEndmembers);
    result.ppiCounts = std::move(counts);
    {
        std::unordered_set<int> chosen;
        for (int e = 0; e < nEndmembers; ++e)
        {
            result.endmemberIndices[static_cast<size_t>(e)] =
                static_cast<int>(order[static_cast<size_t>(e)]);
            chosen.insert(static_cast<int>(order[static_cast<size_t>(e)]));
        }
        streamTiles([&](const float *tile, size_t n, size_t base) {
            for (size_t p = 0; p < n; ++p)
            {
                const int idx = static_cast<int>(base + p);
                if (chosen.count(idx) == 0)
                    continue;
                const float *spectrum = tile + p * B;
                for (int e = 0; e < nEndmembers; ++e)
                {
                    if (result.endmemberIndices[static_cast<size_t>(e)] == idx)
                    {
                        for (int b = 0; b < bandCount; ++b)
                            result.endmembers[static_cast<size_t>(e) * bandCount + b] = spectrum[b];
                        break;
                    }
                }
            }
            return true;
        });
    }

    ds.close();
    context.reportProgress(1.0, "Endmember extraction complete");

    Json::Value json(Json::objectValue);
    Json::Value ends(Json::arrayValue);
    for (int e = 0; e < nEndmembers; ++e)
    {
        Json::Value spectrum(Json::arrayValue);
        for (int b = 0; b < bandCount; ++b)
            spectrum.append(result.endmembers[static_cast<size_t>(e) * bandCount + b]);
        ends.append(spectrum);
    }
    json["endmembers"] = ends;
    Json::Value indices(Json::arrayValue);
    for (int index : result.endmemberIndices)
        indices.append(index);
    json["indices"] = indices;
    constexpr size_t kMaxPpiCountsInJson = 65536;
    if (result.ppiCounts.size() <= kMaxPpiCountsInJson)
    {
        Json::Value countsJson(Json::arrayValue);
        for (int c : result.ppiCounts)
            countsJson.append(c);
        json["ppiCounts"] = countsJson;
    }
    else
    {
        json["ppiCountsTruncated"] = true;
    }
    return json;
}

} // namespace sicnu::operators::rs
