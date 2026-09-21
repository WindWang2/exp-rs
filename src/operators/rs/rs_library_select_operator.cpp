/***************************************************************************
 * rs_library_select_operator.cpp — spectral library selection / projection
 ***************************************************************************/
#include "rs_library_select_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/framework/runtime_paths.h"
#include "processing/algorithms/spectral_classification.h"
#include "processing/algorithms/spectral_library.h"

#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxPairScanEntries = 256;

/// Near-duplicate pairs among selected entries by SAM angle (reported QA,
/// never a removal).
Json::Value findNearDuplicates(const QVector<SpectralLibrary::Entry> &entries,
                               float nodata, double thresholdDeg)
{
    Json::Value pairs(Json::arrayValue);
    if (entries.size() < 2 || entries.size() > kMaxPairScanEntries)
        return pairs;
    for (qsizetype i = 0; i + 1 < entries.size(); ++i)
    {
        for (qsizetype j = i + 1; j < entries.size(); ++j)
        {
            const auto &a = entries[i].spectrum;
            const auto &b = entries[j].spectrum;
            if (a.size() != b.size() || a.empty())
                continue;
            // Shared SAM kernel (single authority for similarity math after the
            // library consolidation): NaN for non-finite, nodata-equal or
            // zero-norm pairs — exactly the pairs this QA skips.
            const double angleRad = SpectralClassification::spectralAngle(
                a.data(), b.data(), a.size(), nodata);
            if (std::isnan(angleRad))
                continue;
            const double angleDeg = angleRad * 180.0 / kPi;
            if (angleDeg <= thresholdDeg)
            {
                Json::Value pair(Json::objectValue);
                pair["a"] = entries[i].name.toStdString();
                pair["b"] = entries[j].name.toStdString();
                pair["angleDeg"] = angleDeg;
                pairs.append(pair);
            }
        }
    }
    return pairs;
}

} // namespace

Json::Value RsLibrarySelectOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["libraryPath"] = makeOutputParam(
        "libraryPath", "Validated spectral library JSON (data/spectral format)", "json");
    props["output"] = makeOutputParam("output", "Subset library JSON", "json");
    props["materials"] = [] {
        Json::Value arr(Json::objectValue);
        arr["type"] = "array";
        arr["description"] = "Material labels to keep (empty = all)";
        arr["items"]["type"] = "string";
        return arr;
    }();
    props["wavelengthMin"] = makeNumberParam("wavelengthMin", "Inclusive window start (nm); "
                                            "entries without wavelength metadata are "
                                            "dropped in this mode", 0.0);
    props["wavelengthMax"] = makeNumberParam("wavelengthMax", "Inclusive window end (nm)", 0.0);
    props["sensor"] = makeStringParam("sensor", "Sensor id (data/spectral/sensors.json) to "
                                      "project the selection onto");
    props["nearDuplicateAngleDeg"] = makeNumberParam("nearDuplicateAngleDeg",
                                                     "QA threshold for near-duplicate "
                                                     "reporting (SAM degrees)", 0.5);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Subset library path", "json");
    outputs["entries"] = makeIntegerParam("entries", "Selected entry count", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"libraryPath", "output"});
    return root;
}

Json::Value RsLibrarySelectOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("spectral");
    meta["tags"].append("library");
    meta["purpose"] = "Compose library-driven spectral workflows: pick materials, "
                      "select entries by wavelength window, project onto a sensor "
                      "grid, keep license control.";
    meta["prerequisites"].append("The source library must pass strict validation "
                                 "(loadValidated); sensor projection needs entries with "
                                 "wavelength grids.");
    meta["workflowHints"].append("Output feeds rs:sam_classify / rs:spectral_unmixing "
                                 "through libraryPath, or rs:matched_filter via "
                                 "targetRef when the selection holds one entry.");
    meta["limitations"].append("Near-duplicate detection reports pairs below the SAM "
                               "threshold; it never removes entries by itself.");
    return meta;
}

Json::Value RsLibrarySelectOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216; // library-resident (curated libs are small)
    return est;
}

Json::Value RsLibrarySelectOperator::run(const Json::Value& params,
                                         RSOperatorContext& context) {
    const std::string libraryPath = requireString(params, "libraryPath");
    const std::string outputPath = requireString(params, "output");
    const QString sensorId = QString::fromStdString(getString(params, "sensor", ""));
    const double duplicateThreshold = getDouble(params, "nearDuplicateAngleDeg", 0.5);


    SpectralLibrary::Library library;
    QString error;
    if (!SpectralLibrary::Library::loadValidated(QString::fromStdString(libraryPath),
                                                 &library, &error))
        throw RSOperatorError(ErrorCode::InvalidParameter, error.toStdString());

    // 1. Material subset (in library order).
    std::vector<std::string> materialFilter = getStringArray(params, "materials");
    QVector<SpectralLibrary::Entry> selected = library.entries;
    if (!materialFilter.empty())
    {
        QStringList wantedList;
        for (const auto &m : materialFilter)
            wantedList.append(QString::fromStdString(m));
        const std::set<QString> wanted(wantedList.cbegin(), wantedList.cend());
        QVector<SpectralLibrary::Entry> filtered;
        for (const auto &entry : selected)
            if (wanted.count(entry.material))
                filtered.append(entry);
        if (filtered.isEmpty())
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "materials filter matched no entries; requested [" +
                                      wantedList.join(", ").toStdString() +
                                      "] but the library offers [" +
                                      library.materials().join(", ").toStdString() + "]");
        selected = filtered;
    }

    // 2. Wavelength window clip.
    const bool hasWindow = params.isMember("wavelengthMin") && params.isMember("wavelengthMax")
                           && params["wavelengthMin"].isNumeric()
                           && params["wavelengthMax"].isNumeric();
    if (hasWindow)
    {
        const double minNm = params["wavelengthMin"].asDouble();
        const double maxNm = params["wavelengthMax"].asDouble();
        if (!(maxNm > minNm))
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "wavelengthMax must be greater than wavelengthMin");
        QVector<SpectralLibrary::Entry> clipped;
        for (const auto &entry : selected)
        {
            if (entry.wavelengths.empty())
                continue; // documented: entries without a grid drop in this mode
            bool inside = false;
            for (float w : entry.wavelengths)
                if (w >= static_cast<float>(minNm) && w <= static_cast<float>(maxNm))
                {
                    inside = true;
                    break;
                }
            if (inside)
                clipped.append(entry);
        }
        if (clipped.isEmpty())
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "No entry's wavelength coverage intersects [" +
                                      std::to_string(minNm) + ", " +
                                      std::to_string(maxNm) + "] nm");
        selected = clipped;
    }

    // 3. Optional sensor projection (Gaussian SRF of ADR 0079).
    if (!sensorId.isEmpty())
    {
        const QString sensorsPath =
            sicnu::processing::resolveRuntimeDataPath(QStringLiteral("spectral/sensors.json"));
        SpectralLibrary::SensorProfile sensor;
        if (!SpectralLibrary::SensorProfile::loadSensor(sensorsPath, sensorId, &sensor, &error))
            throw RSOperatorError(ErrorCode::InvalidParameter, error.toStdString());
        SpectralLibrary::Library projected;
        projected.id = library.id + QStringLiteral("@") + sensorId;
        projected.entries = selected;
        if (!projected.resampleTo(sensor, &projected, &error))
            throw RSOperatorError(ErrorCode::InvalidInputData, error.toStdString());
        selected = projected.entries;
    }

    if (selected.isEmpty())
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Library selection is empty; nothing to write");

    // 4. Near-duplicate QA (SAM angle pairs below the threshold).
    const float nodata = SpectralClassification::kNoDataSentinel;
    const Json::Value duplicatePairs = findNearDuplicates(selected, nodata, duplicateThreshold);

    // 5. Write the subset as library-format JSON (a subset of a validated
    //    library re-validates: the same rules apply on write).
    SpectralLibrary::Library subset;
    subset.id = library.id.isEmpty()
                    ? QStringLiteral("library-selection")
                    : library.id + QStringLiteral("-selection");
    subset.entries = selected;
    QStringList validationErrors;
    if (!SpectralLibrary::validateLibrary(subset, &validationErrors))
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Selection failed library validation: " +
                                  validationErrors.join("; ").toStdString());
    if (!subset.save(QString::fromStdString(outputPath), &error))
        throw RSOperatorError(ErrorCode::FileNotWritable, error.toStdString());

    context.reportProgress(1.0, "Library selection complete");

    // QA payload.
    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["entries"] = static_cast<int>(selected.size());
    Json::Value materialList(Json::arrayValue);
    for (const QString &m : subset.materials())
        materialList.append(m.toStdString());
    result["materials"] = materialList;
    bool allSynthetic = true;
    bool anyMeasured = false;
    QSet<QString> licenses;
    for (const auto &entry : selected)
    {
        allSynthetic = allSynthetic && entry.synthetic;
        anyMeasured = anyMeasured || !entry.synthetic;
        if (!entry.license.isEmpty())
            licenses.insert(entry.license);
    }
    result["synthetic"] = allSynthetic && !anyMeasured;
    result["measured"] = anyMeasured;
    result["license"] =
        licenses.size() == 1
            ? (*licenses.begin()).toStdString()
            : (licenses.isEmpty() ? std::string() : std::string("mixed"));
    result["nearDuplicatePairs"] = duplicatePairs;
    if (!sensorId.isEmpty())
        result["sensorProjection"] = sensorId.toStdString();
    if (selected.size() > kMaxPairScanEntries)
        result["nearDuplicateScanSkipped"] = true;
    return result;
}

} // namespace sicnu::operators::rs
