/***************************************************************************
 * rs_mnf_operator.cpp  —  Minimum Noise Fraction RSOperator (streaming,
 * with an invertible transform model)
 *
 * Hyperspectral Platform 10.0: replaces the full-raster forward-only kernel
 * with the MnfTransform chain — two statistics passes + one application
 * pass, O(row·bands + bands²) memory — and can emit the transform model as
 * a typed artifact (exp-rs:mnf-transform) that rs:mnf_inverse consumes.
 ***************************************************************************/
#include "rs_mnf_operator.h"

#include "rs_partial_output_guard.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/mnf_transform.h"
#include "processing/algorithms/spectral_wavelength.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <gdal.h>

#include <cmath>
#include <limits>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsMnfOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input multi-band raster");
    props["output"] = makeOutputParam("output", "Output MNF components raster", "tif");
    props["numComponents"] = makeIntegerParam("numComponents",
                                              "Number of components (0 = all bands)",
                                              0);
    props["transformOut"] = makeOutputParam(
        "transformOut",
        "Optional path for the MNF transform model artifact "
        "(exp-rs:mnf-transform) consumed by rs:mnf_inverse",
        "json");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "MNF GeoTIFF", "tif");
    outputs["numComponents"] = makeIntegerParam("numComponents", "Components written", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsMnfOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("mnf");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("dimensionality-reduction");
    meta["purpose"] = "Reduce hyperspectral dimensionality by signal-to-noise "
                      "ordered components (PCA of noise-whitened data), with an "
                      "invertible transform model.";
    meta["prerequisites"].append("Input should be calibrated reflectance/radiance; "
                                 "wavelength metadata is not required for MNF itself.");
    meta["workflowHints"].append("Set transformOut to enable the inverse chain: "
                                 "rs:mnf_inverse consumes the artifact to reconstruct "
                                 "band space from selected components (and to convert "
                                 "MNF-space endmembers back via spectrumRef).");
    meta["workflowHints"].append("Memory is O(row x bands + bands^2): statistics stream "
                                 "row by row, never materializing the full cube.");
    meta["limitations"].append("A numerically singular noise covariance (e.g. constant "
                               "bands, too few valid pixels) is a typed refusal, not a "
                               "degraded fit.");
    return meta;
}

Json::Value RsMnfOperator::executionEstimate() const {
    // Streaming: one row buffer (width x bands float) + ~10 B x B double
    // matrices for statistics and eigensolver temporaries. At the accepted
    // band cap (1024) the matrix term alone is ~90 MiB; the nominal number
    // below covers a 256-band cube with a 2k-wide row.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;          // row-streaming: tiling not applicable
    est["tileHeight"] = 1;
    est["estimatedRamBytes"] = 188743680; // ~180 MiB nominal (256 bands, wide row)
    return est;
}

namespace {

/// Read one image row as BIP and build the validity mask (declared NoData or
/// non-finite in ANY band invalidates the pixel, matching the PPI/PCA
/// listwise convention).
bool readValidRow(GdalDatasetWrapper &ds, const std::vector<int> &bandList, int y,
                  int width, const std::vector<bool> &hasNoData,
                  const std::vector<float> &noDataValues, std::vector<float> &bipRow,
                  std::vector<uint8_t> &validMask)
{
    const int bands = static_cast<int>(bandList.size());
    if (!ds.readWindowBip(bandList, 0, y, width, 1, bipRow.data()))
        return false;
    for (int p = 0; p < width; ++p)
    {
        const float *spectrum = bipRow.data() + static_cast<size_t>(p) * bands;
        uint8_t valid = 1;
        for (int b = 0; b < bands; ++b)
        {
            const float v = spectrum[b];
            if (!std::isfinite(v)
                || (hasNoData[static_cast<size_t>(b)]
                    && v == noDataValues[static_cast<size_t>(b)]))
            {
                valid = 0;
                break;
            }
        }
        validMask[static_cast<size_t>(p)] = valid;
    }
    return true;
}

} // namespace

Json::Value RsMnfOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    int numComponents = getInt(params, "numComponents", 0);
    const std::string transformOut = getString(params, "transformOut", "");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input raster not found: " + inputPath);
    }

    ensureGdalInit();

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input: " + inputPath);
    }

    const int bandCount = src.bandCount();
    if (bandCount < 2) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "MNF requires at least 2 bands, got " + std::to_string(bandCount));
    }
    if (bandCount > MnfTransform::kMaxBands) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "MNF supports up to " + std::to_string(MnfTransform::kMaxBands) +
                                  " bands, got " + std::to_string(bandCount));
    }

    const int width = src.width();
    const int height = src.height();
    if (numComponents <= 0 || numComponents > bandCount) {
        numComponents = bandCount;
    }

    std::vector<bool> hasNoData(static_cast<size_t>(bandCount), false);
    std::vector<float> noDataValues(static_cast<size_t>(bandCount), 0.0f);
    for (int b = 0; b < bandCount; ++b)
    {
        bool hasNd = false;
        const double nd = src.bandNoDataValue(b + 1, &hasNd);
        if (hasNd)
        {
            hasNoData[static_cast<size_t>(b)] = true;
            noDataValues[static_cast<size_t>(b)] = static_cast<float>(nd);
        }
    }

    std::vector<int> bandList(static_cast<size_t>(bandCount));
    for (int b = 0; b < bandCount; ++b)
        bandList[static_cast<size_t>(b)] = b + 1;

    std::vector<float> bipRow(static_cast<size_t>(width) * bandCount, 0.0f);
    std::vector<uint8_t> validMask(static_cast<size_t>(width), 0);

    // Pass 1: band means.
    context.reportProgress(0.05, "MNF pass 1/3: band means");
    MnfTransform::RowFeeder feeder(bandCount);
    for (int y = 0; y < height; ++y)
    {
        context.throwIfCancelled();
        if (!readValidRow(src, bandList, y, width, hasNoData, noDataValues, bipRow, validMask))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read row " + std::to_string(y));
        feeder.addRow(bipRow.data(), width, validMask.data());
        if (y % 64 == 0)
            context.reportProgress(0.05 + 0.25 * static_cast<double>(y) / height,
                                   "MNF pass 1/3: band means");
    }
    feeder.finalizeMean();

    // Pass 2: signal + noise covariance statistics.
    context.reportProgress(0.32, "MNF pass 2/3: covariance statistics");
    for (int y = 0; y < height; ++y)
    {
        context.throwIfCancelled();
        if (!readValidRow(src, bandList, y, width, hasNoData, noDataValues, bipRow, validMask))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read row " + std::to_string(y));
        feeder.addRow(bipRow.data(), width, validMask.data());
        if (y % 64 == 0)
            context.reportProgress(0.32 + 0.28 * static_cast<double>(y) / height,
                                   "MNF pass 2/3: covariance statistics");
    }
    feeder.finalizeCovariances();

    context.reportProgress(0.62, "MNF eigendecomposition");
    context.throwIfCancelled();
    MnfTransform::Model model;
    model.bandCount = bandCount;
    QString fitError;
    if (!MnfTransform::fit(feeder, &model, &fitError))
        throw RSOperatorError(ErrorCode::InvalidInputData, fitError.toStdString());

    // Optional transform artifact (before the application pass: failing to
    // write the model must not leave a half-published chain behind).
    if (!transformOut.empty())
    {
        model.wavelengthsNm.clear();
        std::vector<std::pair<double, std::string>> wavelengths;
        bool allWavelengths = true;
        for (int b = 0; b < bandCount && allWavelengths; ++b)
        {
            const QString wl = src.bandMetadataItem(b + 1, "WAVELENGTH");
            double wlValue = 0.0;
            if (wl.isEmpty() || !(wlValue = wl.toDouble(nullptr)))
            {
                allWavelengths = false;
                break;
            }
            wavelengths.emplace_back(
                wlValue, src.bandMetadataItem(b + 1, "WAVELENGTH_UNITS").toStdString());
        }
        if (allWavelengths)
        {
            SpectralWavelength::Grid grid;
            if (SpectralWavelength::gridFromBandValues(wavelengths, {}, &grid)
                == SpectralWavelength::Status::Ok)
                model.wavelengthsNm = grid.centersNm;
        }

        QJsonObject modelJson;
        Json::Value paramLog(Json::objectValue);
        paramLog["numComponents"] = numComponents;
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        MnfTransform::modelToJson(model, QString::fromStdString(inputPath),
                                  QString::fromStdString(Json::writeString(builder, paramLog)),
                                  QDateTime::currentMSecsSinceEpoch(), modelJson);
        QFile artifactFile(QString::fromStdString(transformOut));
        if (!artifactFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Cannot write transform artifact: " + transformOut);
        const QByteArray artifactBytes = QJsonDocument(modelJson).toJson(QJsonDocument::Compact);
        if (artifactFile.write(artifactBytes) != artifactBytes.size())
        {
            artifactFile.close();
            QFile::remove(QString::fromStdString(transformOut));
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Short write to transform artifact: " + transformOut);
        }
        artifactFile.close();
    }

    // Pass 3: apply the forward transform, writing the top-k components.
    // Any failure from here on removes the partial output AND the transform
    // artifact, so no half-published chain survives (ADR 0148 pair rule).
    context.reportProgress(0.70, "MNF pass 3/3: forward transform");
    GdalDatasetWrapper outDataset;
    if (!outDataset.create(QString::fromStdString(outputPath), width, height, numComponents,
                           GDT_Float32, src.geoTransform(), src.projection()))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create MNF output: " + outputPath);
    QStringList guardedPaths{ QString::fromStdString(outputPath) };
    if (!transformOut.empty())
        guardedPaths.append(QString::fromStdString(transformOut));
    PartialOutputGuard partialGuard(guardedPaths);
    for (int c = 0; c < numComponents; ++c)
        outDataset.setBandNoDataValue(c + 1, std::numeric_limits<float>::quiet_NaN());

    std::vector<double> yBuffer(static_cast<size_t>(bandCount), 0.0);
    std::vector<std::vector<float>> componentRows(
        static_cast<size_t>(numComponents),
        std::vector<float>(static_cast<size_t>(width),
                           std::numeric_limits<float>::quiet_NaN()));
    for (int y = 0; y < height; ++y)
    {
        context.throwIfCancelled();
        if (!readValidRow(src, bandList, y, width, hasNoData, noDataValues, bipRow, validMask))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read row " + std::to_string(y));
        for (int c = 0; c < numComponents; ++c)
            std::fill(componentRows[static_cast<size_t>(c)].begin(),
                      componentRows[static_cast<size_t>(c)].end(),
                      std::numeric_limits<float>::quiet_NaN());
        for (int p = 0; p < width; ++p)
        {
            if (!validMask[static_cast<size_t>(p)])
                continue;
            MnfTransform::forward(model, bipRow.data() + static_cast<size_t>(p) * bandCount,
                                  yBuffer.data(), numComponents);
            for (int c = 0; c < numComponents; ++c)
                componentRows[static_cast<size_t>(c)][static_cast<size_t>(p)] =
                    static_cast<float>(yBuffer[static_cast<size_t>(c)]);
        }
        for (int c = 0; c < numComponents; ++c)
        {
            if (!outDataset.writeBandWindow(c + 1, 0, y, width, 1,
                                            componentRows[static_cast<size_t>(c)].data()))
                throw RSOperatorError(ErrorCode::FileNotWritable,
                                      "Failed to write MNF component " + std::to_string(c + 1));
        }
        if (y % 64 == 0)
            context.reportProgress(0.70 + 0.28 * static_cast<double>(y) / height,
                                   "MNF pass 3/3: forward transform");
    }

    src.close();
    QString closeError;
    if (!outDataset.closeWithError(&closeError))
        throw RSOperatorError(ErrorCode::GdalError,
                              closeError.isEmpty()
                                  ? "Failed to finalize MNF output"
                                  : closeError.toStdString());
    partialGuard.disarm();
    context.reportProgress(1.0, "MNF complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["numComponents"] = numComponents;
    result["width"] = width;
    result["height"] = height;
    Json::Value snrArray(Json::arrayValue);
    for (double v : model.snr)
        snrArray.append(v);
    result["signalToNoise"] = snrArray;
    if (!transformOut.empty())
        result["transformArtifact"] = transformOut;
    return result;
}

} // namespace sicnu::operators::rs
