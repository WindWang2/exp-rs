// src/processing/algorithms/band_math.cpp — Band math expression engine
#include "band_math.h"
#include "band_math_ast.h"
#include "band_math_simd.h"
#include "core/sicnu_logging.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace BandMath
{

static constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

bool evaluate(const QString &expression, const BandData &bands, float *out, size_t count)
{
    if (!out) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "BandMath::evaluate: null output pointer");
        return false;
    }
    if (expression.trimmed().isEmpty() || count == 0) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms, "BandMath::evaluate: invalid arguments");
        return false;
    }

    QString err;
    int errCol = 0;
    std::vector<int> requiredBands;
    auto bytecode = BandMathBytecode::compile(expression, err, errCol, &requiredBands);
    if (!bytecode) {
        SICNU_LOG_ERROR(SicnuLogTags::Algorithms,
                        QString("BandMath: parse error in expression: %1 (line 1, col %2): %3")
                            .arg(expression).arg(errCol).arg(err));
        return false;
    }

    // Validate that all referenced bands exist in the data and have sufficient elements
    std::map<int, const float*> bandPtrs;
    for (int ref : requiredBands) {
        auto it = bands.find(ref);
        if (it == bands.end() || it->second.size() < count) {
            SICNU_LOG_ERROR(SicnuLogTags::Algorithms, QString("BandMath: band b%1 not found in input data").arg(ref));
            return false;
        }
        bandPtrs[ref] = it->second.data();
    }

    SICNU_LOG_INFO(SicnuLogTags::Algorithms, QString("BandMath: evaluating '%1' on %2 pixels, %3 bands")
                   .arg(expression).arg(count).arg(bands.size()));

    bytecode->evaluateSimd(bandPtrs, out, count);
    return true;
}

bool evaluateScalar(const QString &expression, const BandData &bands, float *out, size_t count)
{
    if (!out || expression.trimmed().isEmpty() || count == 0) {
        return false;
    }

    QString err;
    int errCol = 0;
    std::vector<int> requiredBands;
    auto bytecode = BandMathBytecode::compile(expression, err, errCol, &requiredBands);
    if (!bytecode) {
        return false;
    }

    std::map<int, const float*> bandPtrs;
    for (int ref : requiredBands) {
        auto it = bands.find(ref);
        if (it == bands.end() || it->second.size() < count) {
            return false;
        }
        bandPtrs[ref] = it->second.data();
    }

    bytecode->evaluateScalar(bandPtrs, out, count);
    return true;
}

std::vector<int> referencedBands(const QString &expression)
{
    QString err;
    int errCol = 0;
    std::vector<int> refs;
    if (BandMathBytecode::compile(expression, err, errCol, &refs)) {
        return refs;
    }
    return {};
}

bool processFile(const QString &sourcePath, const QString &outputPath,
                 const QString &expression, QString *errorMessage)
{
    GdalDatasetWrapper srcDataset;
    if (!srcDataset.open(sourcePath)) {
        if (errorMessage)
            *errorMessage = srcDataset.lastError();
        return false;
    }

    const int width = srcDataset.width();
    const int height = srcDataset.height();
    const int bandCount = srcDataset.bandCount();

    QString compileError;
    int errorCol = 0;
    std::vector<int> requiredBands;
    auto bytecode = BandMathBytecode::compile(expression, compileError, errorCol, &requiredBands);
    if (!bytecode) {
        if (errorMessage)
            *errorMessage = QString("Failed to parse expression: %1 (col %2)").arg(compileError).arg(errorCol);
        return false;
    }

    for (int b : requiredBands) {
        if (b < 1 || b > bandCount) {
            if (errorMessage)
                *errorMessage = QString("Band index %1 out of range (1..%2)").arg(b).arg(bandCount);
            return false;
        }
    }

    // If expression references no bands (constant expression like "42"), stream band 1 for tile geometry
    std::vector<int> streamBands = requiredBands.empty() ? std::vector<int>{1} : requiredBands;

    // Collect per-band NoData sentinel information from source dataset
    struct BandInfo
    {
        int bandNum = 1;
        bool hasNoData = false;
        float noDataVal = 0.0f;
    };
    std::vector<BandInfo> bandInfos(streamBands.size());
    for (size_t i = 0; i < streamBands.size(); i++) {
        bandInfos[i].bandNum = streamBands[i];
        double nd = srcDataset.bandNoDataValue(streamBands[i], &bandInfos[i].hasNoData);
        if (bandInfos[i].hasNoData && std::isfinite(nd)) {
            bandInfos[i].noDataVal = static_cast<float>(nd);
        } else {
            bandInfos[i].hasNoData = false;
        }
    }

    // Initialize streaming output GeoTIFF with bounded memory footprint
    GdalStreamingOutput streamingOutput(outputPath, width, height, 1, GDT_Float32,
                                         srcDataset.geoTransform(), srcDataset.projection());
    if (!streamingOutput.isOpen()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to create streaming output raster");
        return false;
    }
    streamingOutput.setNoDataValue(std::numeric_limits<double>::quiet_NaN());

    SICNU_LOG_INFO(SicnuLogTags::Algorithms, QString("BandMath::processFile: streaming '%1' on %2x%3 raster with %4 band(s)")
                   .arg(expression).arg(width).arg(height).arg(streamBands.size()));

    GdalMultibandBlockStream stream(srcDataset, streamBands, 256, 256);
    const size_t numStreamBands = streamBands.size();

    // Reusable planar tile buffers (<64MB RAM guarantee)
    std::vector<std::vector<float>> planarBuffers(numStreamBands);
    std::vector<float> tileOutput;

    bool streamOk = stream.forEach([&](const GdalMultibandBlockStream::Tile &tile, const float *pixelsBip) -> bool {
        const size_t tilePixels = static_cast<size_t>(tile.width) * static_cast<size_t>(tile.height);
        for (size_t b = 0; b < numStreamBands; b++) {
            planarBuffers[b].resize(tilePixels);
            const bool hasNd = bandInfos[b].hasNoData;
            const float nd = bandInfos[b].noDataVal;
            float *dst = planarBuffers[b].data();
            for (size_t p = 0; p < tilePixels; p++) {
                float v = pixelsBip[p * numStreamBands + b];
                if (!std::isfinite(v) || (hasNd && v == nd)) {
                    dst[p] = kNaN;
                } else {
                    dst[p] = v;
                }
            }
        }

        std::map<int, const float*> tileBandPtrs;
        for (size_t b = 0; b < numStreamBands; b++) {
            tileBandPtrs[streamBands[b]] = planarBuffers[b].data();
        }

        tileOutput.resize(tilePixels);
        bytecode->evaluateSimd(tileBandPtrs, tileOutput.data(), tilePixels);

        if (!streamingOutput.writeTile(1, tile, tileOutput.data())) {
            return false;
        }
        return true;
    });

    if (!streamOk) {
        if (errorMessage && errorMessage->isEmpty())
            *errorMessage = QStringLiteral("Failed while streaming tiles during band-math evaluation");
        return false;
    }

    if (!streamingOutput.closeWithError(errorMessage)) {
        return false;
    }

    return true;
}

} // namespace BandMath
