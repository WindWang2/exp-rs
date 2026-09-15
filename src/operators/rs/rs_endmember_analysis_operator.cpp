/***************************************************************************
 * rs_endmember_analysis_operator.cpp  —  endmember set analysis
 ***************************************************************************/
#include "rs_endmember_analysis_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/endmember_analysis.h"
#include "processing/algorithms/spectral_library.h"
#include "processing/algorithms/spectral_table.h"
#include "processing/framework/runtime_paths.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <cmath>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {
// The angle matrix travels in the result JSON; keep that payload bounded.
constexpr int kMaxMatrixRows = 64;
} // namespace

Json::Value RsEndmemberAnalysisOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["endmembersRef"] = makeStringParam(
        "endmembersRef", "Input exp-rs:spectral-table artifact path (e.g. from rs:endmember_extraction)");
    props["output"] = makeOutputParam("output", "Output spectral table artifact", "json");
    props["mergeAngleDegrees"] = makeNumberParam("mergeAngleDegrees",
        "Average-link cluster merge threshold in degrees (0 keeps every endmember)", 2.0);
    props["ppiCounts"] = [] {
        Json::Value arr(Json::objectValue);
        arr["type"] = "array";
        arr["description"] = "Optional per-row purity ranking (e.g. PPI counts) aligned "
                             "with the input table rows; absent rows all tie";
        arr["items"]["type"] = "integer";
        return arr;
    }();
    props["angleMatrix"] = [] {
        Json::Value v(Json::objectValue);
        v["type"] = "boolean";
        v["description"] = "Embed the pairwise SAM angle matrix (radians) in the result "
                           "(input rows <= 64)";
        return v;
    }();
    props["sensor"] = makeStringParam("sensor",
        "Sensor id (data/spectral/sensors.json) to project the reduced set onto", "");
    props["requireFullCoverage"] = [] {
        Json::Value v(Json::objectValue);
        v["type"] = "boolean";
        v["description"] = "Refuse projection when any reduced endmember lacks source coverage";
        return v;
    }();

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Reduced table artifact path", "json");
    outputs["inputRows"] = makeIntegerParam("inputRows", "Input endmember count", 0);
    outputs["outputRows"] = makeIntegerParam("outputRows", "Reduced endmember count", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"endmembersRef", "output"});
    return root;
}

Json::Value RsEndmemberAnalysisOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["task"] = "endmember-analysis";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("spectral");
    meta["tags"].append("endmember");
    meta["tags"].append("library");
    meta["purpose"] = "Turn a raw extracted endmember set into an interpretable, "
                      "non-redundant dictionary with a SAM angle matrix and optional "
                      "sensor projection.";
    meta["prerequisites"].append("Input must be an exp-rs:spectral-table with finite, "
                                 "non-zero endmember rows.");
    meta["workflowHints"].append("Chain rs:endmember_extraction endmembersOut into this "
                                 "operator, then feed output to rs:spectral_unmixing via "
                                 "endmembersRef or rs:sparse_unmixing.");
    meta["limitations"].append("Reduction caps at 512 input rows; the angle matrix embed "
                               "caps at 64 rows (payload bound).");
    meta["limitations"].append("Projection reflects the reduced NATIVE-space set; angles "
                               "change under resampling by design.");
    return meta;
}

Json::Value RsEndmemberAnalysisOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216; // table-resident (curated sets are small)
    return est;
}

Json::Value RsEndmemberAnalysisOperator::run(const Json::Value& params,
                                             RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "endmembersRef");
    const std::string outputPath = requireString(params, "output");
    const double mergeAngleDegrees = getDouble(params, "mergeAngleDegrees", 2.0);
    const bool wantMatrix = params.isMember("angleMatrix") && params["angleMatrix"].asBool();
    const std::string sensorId = getString(params, "sensor", "");
    const bool requireFullCoverage =
        params.isMember("requireFullCoverage") && params["requireFullCoverage"].asBool();
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input endmember table not found: " + inputPath);

    SpectralTable::Table table;
    QString error;
    if (!SpectralTable::loadValidated(QString::fromStdString(inputPath), &table, &error))
        throw RSOperatorError(ErrorCode::InvalidInputData, error.toStdString());

    const int rows = table.count();
    const int bands = table.bandCount;
    if (rows > 512)
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Input table holds " + std::to_string(rows)
                                  + " rows; reduction caps at 512");

    // Optional PPI ranking aligned with the table rows.
    std::vector<int> ppiCounts;
    if (params.isMember("ppiCounts") && params["ppiCounts"].isArray()) {
        const Json::Value& counts = params["ppiCounts"];
        if (static_cast<int>(counts.size()) != rows)
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "ppiCounts size (" + std::to_string(counts.size())
                                      + ") does not match the input row count ("
                                      + std::to_string(rows) + ")");
        ppiCounts.reserve(counts.size());
        for (const auto& c : counts) {
            if (!c.isIntegral() || c.asInt() < 0)
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "ppiCounts entries must be non-negative integers");
            ppiCounts.push_back(c.asInt());
        }
    }

    context.reportProgress(0.2, "Analyzing endmember set");

    // Flatten the table rows (endmember-major) for the kernels.
    std::vector<float> endmembers;
    endmembers.reserve(static_cast<size_t>(rows) * bands);
    for (const auto& row : table.spectra)
        endmembers.insert(endmembers.end(), row.begin(), row.end());

    // 1. Reduction (average-link clustering on spectral angle).
    EndmemberAnalysis::ReduceResult reduceResult;
    QString kernelError;
    EndmemberAnalysis::ReduceConfig reduceConfig;
    reduceConfig.mergeAngleDegrees = mergeAngleDegrees;
    const std::vector<int>* ppiArg = ppiCounts.empty() ? nullptr : &ppiCounts;
    if (!EndmemberAnalysis::reduceEndmembers(endmembers.data(), rows, bands,
                                             reduceConfig, ppiArg,
                                             &reduceResult, &kernelError))
        throw RSOperatorError(ErrorCode::ComputationError,
                              kernelError.isEmpty() ? "Endmember reduction failed"
                                                    : kernelError.toStdString());
    const int reducedRows = static_cast<int>(reduceResult.representativeOf.size());
    context.reportProgress(0.5, "Reduction complete");

    // 2. Optional sensor projection of the reduced set.
    std::vector<float> projectedSpectra;
    std::vector<float> projectedWavelengths;
    std::vector<float> projectedFwhm;
    int projectedBands = 0;
    if (!sensorId.empty()) {
        const QString sensorsPath =
            sicnu::processing::resolveRuntimeDataPath(QStringLiteral("spectral/sensors.json"));
        SpectralLibrary::SensorProfile sensor;
        if (!SpectralLibrary::SensorProfile::loadSensor(sensorsPath,
                                                        QString::fromStdString(sensorId),
                                                        &sensor, &error))
            throw RSOperatorError(ErrorCode::InvalidParameter, error.toStdString());
        if (sensor.bands.isEmpty())
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "Sensor " + sensorId + " declares no bands");
        std::vector<float> sensorCenters;
        std::vector<float> sensorFwhm;
        bool allFwhm = true;
        for (const auto& band : sensor.bands) {
            sensorCenters.push_back(band.wavelengthNm);
            if (band.fwhmNm > 0.0f && std::isfinite(band.fwhmNm))
                sensorFwhm.push_back(band.fwhmNm);
            else
                allFwhm = false;
        }
        // A sensor without FWHM metadata projects linearly (documented).
        const float* fwhm = allFwhm ? sensorFwhm.data() : nullptr;
        EndmemberAnalysis::ProjectionResult projection;
        if (!EndmemberAnalysis::projectToSensor(
                reduceResult.endmembers.data(), reducedRows, bands,
                table.wavelengthsNm.empty() ? nullptr : table.wavelengthsNm.data(),
                sensorCenters.data(), fwhm, static_cast<int>(sensorCenters.size()),
                requireFullCoverage, &projection, &kernelError))
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  kernelError.isEmpty() ? "Sensor projection failed"
                                                        : kernelError.toStdString());
        projectedSpectra = std::move(projection.spectra);
        projectedWavelengths = std::move(projection.wavelengthsNm);
        projectedFwhm = std::move(projection.fwhmNm);
        projectedBands = static_cast<int>(sensorCenters.size());
    }
    context.reportProgress(0.7, "Projection complete");

    // 3. Optional pairwise angle matrix over the reduced NATIVE-space set.
    Json::Value matrixJson(Json::nullValue);
    if (wantMatrix) {
        if (reducedRows > kMaxMatrixRows)
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "angleMatrix requested for " + std::to_string(reducedRows)
                                      + " rows; cap is " + std::to_string(kMaxMatrixRows));
        std::vector<double> matrix;
        if (!EndmemberAnalysis::angleMatrix(reduceResult.endmembers.data(), reducedRows,
                                            bands, &matrix, &kernelError))
            throw RSOperatorError(ErrorCode::ComputationError,
                                  kernelError.isEmpty() ? "Angle matrix failed"
                                                        : kernelError.toStdString());
        matrixJson = Json::Value(Json::arrayValue);
        for (int r = 0; r < reducedRows; ++r) {
            Json::Value row(Json::arrayValue);
            for (int c = 0; c < reducedRows; ++c)
                row.append(matrix[static_cast<size_t>(r) * reducedRows + c]);
            matrixJson.append(row);
        }
    }

    // 4. Write the derived table. License/citation follow the source; the
    //    provenance records this operator as the producer.
    SpectralTable::Table out;
    out.id = table.id.isEmpty() ? QStringLiteral("endmember-analysis")
                                : table.id + QStringLiteral("-analysis");
    out.bandCount = projectedBands > 0 ? projectedBands : bands;
    if (projectedBands > 0) {
        out.wavelengthsNm = projectedWavelengths;
        if (!projectedFwhm.empty())
            out.fwhmNm = projectedFwhm;
    } else {
        out.wavelengthsNm = table.wavelengthsNm;
        out.fwhmNm = table.fwhmNm;
    }
    const int outSpectraBands = out.bandCount;
    const std::vector<float>& spectraSource =
        projectedBands > 0 ? projectedSpectra : reduceResult.endmembers;
    for (int r = 0; r < reducedRows; ++r) {
        out.spectra.emplace_back(
            spectraSource.begin() + static_cast<size_t>(r) * outSpectraBands,
            spectraSource.begin() + static_cast<size_t>(r + 1) * outSpectraBands);
        const int representative = reduceResult.representativeOf[static_cast<size_t>(r)];
        if (representative < table.labels.size())
            out.labels.append(table.labels.at(representative));
        if (representative < table.materials.size())
            out.materials.append(table.materials.at(representative));
    }
    out.provenance.sourceOperator = "rs:endmember_analysis";
    out.provenance.sourceInput = QString::fromStdString(inputPath);
    out.provenance.synthetic = table.provenance.synthetic;
    out.provenance.derived = true;
    out.provenance.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    {
        Json::Value paramLog(Json::objectValue);
        paramLog["mergeAngleDegrees"] = mergeAngleDegrees;
        if (!ppiCounts.empty())
            paramLog["ppiCounts"] = true;
        if (!sensorId.empty())
            paramLog["sensor"] = sensorId;
        paramLog["inputRows"] = rows;
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        out.provenance.parameters =
            QString::fromStdString(Json::writeString(builder, paramLog));
    }
    out.license = table.license;
    out.citation = table.citation;

    QStringList tableErrors;
    if (!SpectralTable::validate(out, &tableErrors))
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Output table failed validation: "
                                  + tableErrors.join("; ").toStdString());
    QString saveError;
    if (!SpectralTable::save(out, QString::fromStdString(outputPath), &saveError))
        throw RSOperatorError(ErrorCode::FileNotWritable, saveError.toStdString());

    context.reportProgress(1.0, "Endmember analysis complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["inputRows"] = rows;
    result["outputRows"] = reducedRows;
    Json::Value clusterOf(Json::arrayValue);
    for (int c : reduceResult.clusterOf)
        clusterOf.append(c);
    result["clusterOf"] = clusterOf;
    Json::Value representativeOf(Json::arrayValue);
    for (int r : reduceResult.representativeOf)
        representativeOf.append(r);
    result["representativeOf"] = representativeOf;
    Json::Value mergeAngles(Json::arrayValue);
    for (double a : reduceResult.mergeAngles)
        mergeAngles.append(a);
    result["mergeAngles"] = mergeAngles;
    if (!matrixJson.isNull())
        result["angleMatrix"] = matrixJson;
    if (!sensorId.empty())
        result["sensorProjection"] = sensorId;
    result["inputDigest"] = table.digestHex.toStdString();
    result["outputDigest"] = out.digestHex.toStdString();
    if (!table.license.isEmpty())
        result["license"] = table.license.toStdString();
    result["synthetic"] = table.provenance.synthetic;
    return result;
}

} // namespace sicnu::operators::rs
