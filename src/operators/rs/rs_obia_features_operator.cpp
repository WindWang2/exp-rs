/***************************************************************************
 * rs_obia_features_operator.cpp  —  RsSegmentFeatures behind the operator seam
 *
 * Issue #663 — the OBIA GUI's feature-extraction step (previously a direct
 * RsSegmentFeatures::extract call inside a TaskCenter lambda) now runs here.
 * The analysis kernel is unchanged; the operator adds the boundary contract
 * (validation, cancel/progress plumbing, CSV interchange, result summary).
 ***************************************************************************/
#include "rs_obia_features_operator.h"

#include "analysis/segmentation/rs_segment_features.h"
#include "analysis/segmentation/rs_segment_map.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QString>
#include <QTextStream>

#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsObiaFeaturesOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Source raster (spectral bands)");
    props["labels"] = makeRasterParam("labels", "Segment label raster (UInt32, 0 = nodata)");
    props["output"] = makeOutputParam("output", "Per-segment feature CSV", "csv");

    Json::Value bands = makeStringParam("bands", "Optional 1-based band indices", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "integer";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Feature CSV", "csv");
    outputs["segments"] = makeIntegerParam("segments", "Segments (rows) written", 0);
    outputs["bands"] = makeIntegerParam("bands", "Bands used", 0);
    outputs["features"] = makeIntegerParam("features", "Feature columns per row", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "labels", "output"});
    return root;
}

Json::Value RsObiaFeaturesOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("obia");
    meta["tags"].append("features");
    meta["tags"].append("texture");
    meta["purpose"] = "Full per-object statistics (spectral + GLCM + shape) as a CSV table";
    meta["limitations"] = "Labels grid must match the source raster; label band is read as-is (UInt32 labels expected)";
    Json::Value useCases(Json::arrayValue);
    useCases.append("Object table for Excel/Python after rs:obia_segment");
    useCases.append("Interactive OBIA session feature step (GUI rehydrates the CSV)");
    meta["useCases"] = useCases;
    Json::Value hints(Json::arrayValue);
    hints.append("labels = the output raster of rs:obia_segment (engine simple/otb/auto)");
    hints.append("For a light mean+area table use rs:segment_stats; this operator emits the full stat set");
    meta["workflowHints"] = hints;
    return meta;
}

Json::Value RsObiaFeaturesOperator::executionEstimate() const
{
    // FullRaster (default policy): the any-band validity mask, the label map
    // and per-segment accumulators are resident; band data streams in
    // row blocks (#648). GLCM re-reads per segment bbox chunk.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 50331648; // mask + labels + accumulators (48 MiB)
    return est;
}

Json::Value RsObiaFeaturesOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string labelsPath = requireString(params, "labels");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    if (!fileExists(labelsPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Labels not found: " + labelsPath);

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath)))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input");
    const std::vector<int> bands = parseBands(params, ds.bandCount());
    if (bands.empty())
        throw RSOperatorError(ErrorCode::InvalidParameter, "No bands selected");

    // Grid alignment: the label raster must cover the same pixels as the
    // source (both dimensions equal; CRS/geotransform equality is implied by
    // the producing segmentation step).
    GdalDatasetWrapper labelsDs;
    if (!labelsDs.open(QString::fromStdString(labelsPath)))
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open labels");
    if (labelsDs.width() != ds.width() || labelsDs.height() != ds.height())
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Labels grid (" + std::to_string(labelsDs.width()) + "x" +
                                  std::to_string(labelsDs.height()) +
                              ") does not match the source raster (" +
                              std::to_string(ds.width()) + "x" + std::to_string(ds.height()) + ")");
    labelsDs.close();

    context.reportProgress(0.02, "Loading label map");
    RsSegmentMap segMap = RsSegmentMap::fromGeoTIFF(QString::fromStdString(labelsPath));
    if (segMap.isEmpty())
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Label raster is empty or unreadable as a UInt32 label map");
    if (segMap.segmentCount() < 1)
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Label raster contains no segments (all pixels are 0)");

    QVector<int> bandIndices;
    bandIndices.reserve(static_cast<int>(bands.size()));
    for (int b : bands)
        bandIndices.append(b);

    context.reportProgress(0.05, "Extracting features");
    const QMap<quint32, RsSegmentFeatures::SegmentStat> stats = RsSegmentFeatures::extract(
        QString::fromStdString(inputPath), segMap, bandIndices,
        [&context]() { return context.isCancelled(); },
        [&context](float f) { context.reportProgress(0.05 + 0.9 * f, "Extracting features"); });
    context.throwIfCancelled();
    if (stats.isEmpty())
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Feature extraction produced no segment statistics");

    context.reportProgress(0.97, "Writing CSV");
    QFile csv(QString::fromStdString(outputPath));
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text))
        throw RSOperatorError(ErrorCode::FileNotWritable, "Cannot write " + outputPath);
    QTextStream out(&csv);

    const int nBands = static_cast<int>(bands.size());
    out << "segment_id,area,perimeter,shape_index,compactness,rectangularity,aspect_ratio";
    for (int b = 1; b <= nBands; ++b)
        out << ",mean_b" << b << ",stddev_b" << b << ",min_b" << b << ",max_b" << b
            << ",glcm_contrast_b" << b << ",glcm_correlation_b" << b
            << ",glcm_energy_b" << b << ",glcm_homogeneity_b" << b;
    out << '\n';

    // 17 significant digits: exact round-trip of the doubles the GUI adapter
    // rehydrates into its session stats map.
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
        const auto &s = it.value();
        out << it.key()
            << ',' << QString::number(s.area, 'g', 17)
            << ',' << QString::number(s.perimeter, 'g', 17)
            << ',' << QString::number(s.shapeIndex, 'g', 17)
            << ',' << QString::number(s.compactness, 'g', 17)
            << ',' << QString::number(s.rectangularity, 'g', 17)
            << ',' << QString::number(s.aspectRatio, 'g', 17);
        for (int b = 0; b < nBands; ++b) {
            out << ',' << QString::number(s.mean.value(b), 'g', 17)
                << ',' << QString::number(s.stddev.value(b), 'g', 17)
                << ',' << QString::number(s.min.value(b), 'g', 17)
                << ',' << QString::number(s.max.value(b), 'g', 17)
                << ',' << QString::number(s.glcmContrast.value(b), 'g', 17)
                << ',' << QString::number(s.glcmCorrelation.value(b), 'g', 17)
                << ',' << QString::number(s.glcmEnergy.value(b), 'g', 17)
                << ',' << QString::number(s.glcmHomogeneity.value(b), 'g', 17);
        }
        out << '\n';
    }
    out.flush();
    if (csv.error() != QFile::NoError)
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed writing " + outputPath + ": " + csv.errorString().toStdString());
    csv.close();

    context.reportProgress(1.0, "Feature extraction complete");

    // 6 shape columns + 8 per-band columns.
    const int featureColumns = 6 + 8 * nBands;

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["segments"] = static_cast<int>(stats.size());
    result["bands"] = nBands;
    result["features"] = featureColumns;
    result["width"] = segMap.width();
    result["height"] = segMap.height();
    return result;
}

} // namespace sicnu::operators::rs
