/***************************************************************************
 * rs_mnf_inverse_operator.cpp — inverse MNF RSOperator (Hyperspectral 10.0)
 ***************************************************************************/
#include "rs_mnf_inverse_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/mnf_transform.h"
#include "processing/algorithms/spectral_table.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

/// Load and digest-verify the transform model artifact.
MnfTransform::Model loadModel(const std::string &path)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Cannot open transform artifact: " + path);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Transform artifact is not valid JSON: " + path + " (" +
                                  parseError.errorString().toStdString() + ")");
    MnfTransform::Model model;
    QString error;
    if (!MnfTransform::modelFromJson(doc.object(), &model, &error))
        throw RSOperatorError(ErrorCode::InvalidParameter, error.toStdString());
    return model;
}

/// Parse + validate the component selection (empty = default selection).
std::vector<int> parseComponents(const Json::Value &params, int inputBands, int modelBands)
{
    std::vector<int> components;
    if (!params.isMember("components") || params["components"].isNull())
    {
        components.resize(static_cast<size_t>(inputBands));
        for (int i = 0; i < inputBands; ++i)
            components[static_cast<size_t>(i)] = i;
        return components;
    }
    const Json::Value &arr = params["components"];
    if (!arr.isArray() || arr.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "'components' must be a non-empty array of 0-based "
                              "component indices");
    std::set<int> unique;
    for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
    {
        if (!arr[i].isIntegral())
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "'components' entries must be integers");
        const int c = arr[i].asInt();
        if (c < 0 || c >= modelBands)
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Component index " + std::to_string(c) +
                                      " outside [0, " + std::to_string(modelBands) + ")");
        if (!unique.insert(c).second)
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Duplicate component index " + std::to_string(c));
        components.push_back(c);
    }
    std::sort(components.begin(), components.end());
    return components;
}

Json::Value runRasterMode(const Json::Value &params, RSOperatorContext &context,
                          const MnfTransform::Model &model)
{
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input raster not found: " + inputPath);
    const std::string errorOut = getString(params, "errorOut", "");

    ensureGdalInit();

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input: " + inputPath);

    const int width = src.width();
    const int height = src.height();
    const int inputBands = src.bandCount();
    if (inputBands < 1 || inputBands > model.bandCount)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Input component count (" + std::to_string(inputBands) +
                                  ") outside [1, model bandCount " +
                                  std::to_string(model.bandCount) + "]");

    const std::vector<int> components = parseComponents(params, inputBands, model.bandCount);

    // Input validity: declared NoData / non-finite anywhere in the component
    // vector invalidates the pixel (its reconstruction would be garbage).
    bool hasNoData = false;
    double noData = src.bandNoDataValue(1, &hasNoData);

    GdalDatasetWrapper outDataset;
    if (!outDataset.create(QString::fromStdString(outputPath), width, height,
                           model.bandCount, GDT_Float32, src.geoTransform(),
                           src.projection()))
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create reconstructed raster: " + outputPath);
    outDataset.setBandNoDataValue(1, std::numeric_limits<float>::quiet_NaN());
    GDALDataset *outHandle = static_cast<GDALDataset *>( outDataset.dataset() );
    for (int b = 0; b < model.bandCount; ++b)
    {
        if (static_cast<int>(model.wavelengthsNm.size()) == model.bandCount && outHandle)
        {
            const QByteArray wl =
                QString::number(static_cast<double>(model.wavelengthsNm[static_cast<size_t>(b)]),
                                'f', 6)
                    .toUtf8();
            GDALRasterBand *band = outHandle->GetRasterBand(b + 1);
            band->SetMetadataItem("WAVELENGTH", wl.constData());
            band->SetMetadataItem("WAVELENGTH_UNITS", "nm");
        }
    }

    GdalDatasetWrapper errorDataset;
    if (!errorOut.empty())
    {
        if (!errorDataset.create(QString::fromStdString(errorOut), width, height, 1,
                                 GDT_Float32, src.geoTransform(), src.projection()))
            throw RSOperatorError(ErrorCode::FileNotWritable,
                                  "Failed to create reconstruction-error raster: " + errorOut);
        errorDataset.setBandNoDataValue(1, std::numeric_limits<float>::quiet_NaN());
    }

    const std::vector<int> bandList = [&inputBands] {
        std::vector<int> bands(static_cast<size_t>(inputBands));
        for (int b = 0; b < inputBands; ++b)
            bands[static_cast<size_t>(b)] = b + 1;
        return bands;
    }();

    std::vector<float> bipRow(static_cast<size_t>(width) * inputBands, 0.0f);
    std::vector<double> yBuffer(static_cast<size_t>(model.bandCount), 0.0);
    std::vector<double> spectrumBuffer(static_cast<size_t>(model.bandCount), 0.0);
    std::vector<std::vector<float>> bandRows(
        static_cast<size_t>(model.bandCount),
        std::vector<float>(static_cast<size_t>(width),
                           std::numeric_limits<float>::quiet_NaN()));
    std::vector<float> errorRow(static_cast<size_t>(width),
                                std::numeric_limits<float>::quiet_NaN());

    const int totalTiles = (height + 255) / 256;
    int tilesSeen = 0;
    for (int y = 0; y < height; ++y)
    {
        context.throwIfCancelled();
        if (!src.readWindowBip(bandList, 0, y, width, 1, bipRow.data()))
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read row " + std::to_string(y));
        for (auto &row : bandRows)
            std::fill(row.begin(), row.end(), std::numeric_limits<float>::quiet_NaN());
        std::fill(errorRow.begin(), errorRow.end(), std::numeric_limits<float>::quiet_NaN());

        for (int p = 0; p < width; ++p)
        {
            const float *coeffs = bipRow.data() + static_cast<size_t>(p) * inputBands;
            bool valid = true;
            for (int c = 0; c < inputBands && valid; ++c)
            {
                if (!std::isfinite(coeffs[c])
                    || (hasNoData && coeffs[c] == static_cast<float>(noData)))
                    valid = false;
            }
            if (!valid)
                continue;
            std::fill(yBuffer.begin(), yBuffer.end(), 0.0);
            for (int c = 0; c < inputBands; ++c)
                yBuffer[static_cast<size_t>(c)] = static_cast<double>(coeffs[c]);
            MnfTransform::inverse(model, yBuffer.data(), components, spectrumBuffer.data());
            for (int b = 0; b < model.bandCount; ++b)
                bandRows[static_cast<size_t>(b)][static_cast<size_t>(p)] =
                    static_cast<float>(spectrumBuffer[static_cast<size_t>(b)]);
            if (!errorOut.empty())
                errorRow[static_cast<size_t>(p)] = static_cast<float>(
                    MnfTransform::reconstructionRmse(model, yBuffer.data(), components));
        }

        for (int b = 0; b < model.bandCount; ++b)
        {
            if (!outDataset.writeBandWindow(b + 1, 0, y, width, 1,
                                            bandRows[static_cast<size_t>(b)].data()))
                throw RSOperatorError(ErrorCode::FileNotWritable,
                                      "Failed to write reconstructed band " +
                                          std::to_string(b + 1));
        }
        if (!errorOut.empty())
        {
            if (!errorDataset.writeBandWindow(1, 0, y, width, 1, errorRow.data()))
                throw RSOperatorError(ErrorCode::FileNotWritable,
                                      "Failed to write reconstruction-error row");
        }
        if (++tilesSeen % 8 == 0 || y == height - 1)
            context.reportProgress(static_cast<double>(y + 1) / height,
                                   "Inverse MNF");
    }

    src.close();
    outDataset.close();
    if (!errorOut.empty())
        errorDataset.close();
    context.reportProgress(1.0, "Inverse MNF complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["bands"] = model.bandCount;
    result["components"] = static_cast<int>(components.size());
    result["width"] = width;
    result["height"] = height;
    if (!errorOut.empty())
        result["errorOut"] = errorOut;
    return result;
}

Json::Value runSpectrumMode(const Json::Value &params, RSOperatorContext &context,
                            const MnfTransform::Model &model)
{
    Q_UNUSED( context );
    const std::string spectrumRef = requireString(params, "spectrumRef");
    const std::string spectrumOut = requireString(params, "spectrumOut");

    SpectralTable::Table table;
    QString error;
    if (!SpectralTable::loadValidated(QString::fromStdString(spectrumRef), &table, &error))
        throw RSOperatorError(ErrorCode::InvalidParameter, error.toStdString());
    if (table.count() != 1)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "spectrumRef must hold exactly one spectrum, got " +
                                  std::to_string(table.count()));
    if (table.bandCount > model.bandCount)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Spectrum width (" + std::to_string(table.bandCount) +
                                  ") exceeds the model band count (" +
                                  std::to_string(model.bandCount) + ")");

    const std::vector<int> components =
        parseComponents(params, table.bandCount, model.bandCount);

    std::vector<double> yBuffer(static_cast<size_t>(model.bandCount), 0.0);
    for (int c = 0; c < table.bandCount; ++c)
        yBuffer[static_cast<size_t>(c)] =
            static_cast<double>(table.spectra[0][static_cast<size_t>(c)]);
    std::vector<double> spectrumBuffer(static_cast<size_t>(model.bandCount), 0.0);
    MnfTransform::inverse(model, yBuffer.data(), components, spectrumBuffer.data());
    const double rmse = MnfTransform::reconstructionRmse(model, yBuffer.data(), components);

    SpectralTable::Table converted;
    converted.id = QStringLiteral("mnf-converted");
    converted.bandCount = model.bandCount;
    converted.spectra.push_back(spectrumBuffer);
    if (static_cast<int>(model.wavelengthsNm.size()) == model.bandCount)
        converted.wavelengthsNm = model.wavelengthsNm;
    converted.labels.append(
        table.labels.isEmpty() ? QStringLiteral("converted") : table.labels.first());
    converted.provenance.sourceOperator = QStringLiteral("rs:mnf_inverse");
    converted.provenance.sourceInput = QString::fromStdString(spectrumRef);
    converted.provenance.synthetic = table.provenance.synthetic;
    converted.provenance.derived = true;
    converted.license = table.license;
    converted.citation = table.citation;

    QStringList validationErrors;
    if (!SpectralTable::validate(converted, &validationErrors))
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Converted spectrum failed validation: " +
                                  validationErrors.join("; ").toStdString());
    QString saveError;
    if (!SpectralTable::save(converted, QString::fromStdString(spectrumOut), &saveError))
        throw RSOperatorError(ErrorCode::FileNotWritable, saveError.toStdString());

    Json::Value result(Json::objectValue);
    result["spectrumOut"] = spectrumOut;
    result["bands"] = model.bandCount;
    result["reconstructionError"] = rmse;
    return result;
}

} // namespace

Json::Value RsMnfInverseOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["transform"] = makeOutputParam(
        "transform", "MNF transform model artifact (exp-rs:mnf-transform) from "
                     "rs:mnf transformOut",
        "json");
    props["input"] = makeRasterParam("input", "MNF components raster (raster mode)");
    props["output"] = makeOutputParam("output", "Reconstructed band-space raster", "tif");
    props["components"] = [] {
        Json::Value arr(Json::objectValue);
        arr["type"] = "array";
        arr["description"] = "0-based component subset for the inverse (default: all "
                             "bands of the input raster)";
        arr["items"]["type"] = "integer";
        return arr;
    }();
    props["errorOut"] = makeOutputParam("errorOut", "Optional reconstruction-error raster "
                                                    "(RMSE of the dropped components)", "tif");
    props["spectrumRef"] = makeOutputParam(
        "spectrumRef", "Spectral-table artifact with one MNF-space spectrum "
                       "(spectrum mode; converts it back to band space)",
        "json");
    props["spectrumOut"] = makeOutputParam("spectrumOut", "Converted spectrum table path "
                                                          "(spectrum mode)",
                                           "json");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Reconstructed raster path");
    outputs["spectrumOut"] = makeOutputParam("spectrumOut", "Converted spectrum path", "json");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"transform"});
    return root;
}

Json::Value RsMnfInverseOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("mnf");
    meta["tags"].append("hyperspectral");
    meta["tags"].append("inverse-transform");
    meta["purpose"] = "Complete the MNF chain: reconstruct band space from MNF "
                      "components, or convert MNF-space endmembers back to "
                      "reflectance space for SAM/SID/unmixing.";
    meta["prerequisites"].append("Requires the transform artifact written by rs:mnf "
                                 "(transformOut); the model is digest-verified on load.");
    meta["workflowHints"].append("Classic MNF->PPI chain: rs:mnf (transformOut) -> "
                                 "rs:mnf -> rs:endmember_extraction on components -> "
                                 "rs:mnf_inverse (spectrumRef) -> rs:sam_classify with "
                                 "refsRef in reflectance space.");
    meta["limitations"].append("Component subsets are a documented approximation: the "
                               "dropped components' contribution is reported via "
                               "errorOut / reconstructionError, never silently ignored.");
    return meta;
}

Json::Value RsMnfInverseOperator::executionEstimate() const {
    // Streaming: one input row + model.bandCount output rows + B x B matrices.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 1;
    est["estimatedRamBytes"] = 12582912; // 12 MiB nominal
    return est;
}

Json::Value RsMnfInverseOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string transformPath = requireString(params, "transform");
    const MnfTransform::Model model = loadModel(transformPath);

    const bool spectrumMode =
        params.isMember("spectrumRef") && params["spectrumRef"].isString()
        && !params["spectrumRef"].asString().empty();

    if (spectrumMode)
        return runSpectrumMode(params, context, model);
    return runRasterMode(params, context, model);
}

} // namespace sicnu::operators::rs
