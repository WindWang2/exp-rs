/***************************************************************************
 * rs_obia_label_operator.cpp  —  RsRoiLabeler behind the operator seam
 *
 * Issue #663 — the OBIA GUI's Import ROI step re-implemented majority
 * labeling (windowed GDAL MEM rasterize + GEOS fallback + a hand-written
 * votes loop mirroring the operator tie-break). The canonical kernel
 * (RsRoiLabeler, ADR 0060) is now the only labeling path, and the GUI
 * imports the CSV this operator writes.
 ***************************************************************************/
#include "rs_obia_label_operator.h"

#include "analysis/segmentation/rs_roi_labeler.h"
#include "analysis/segmentation/rs_segment_map.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <QFile>
#include <QTextStream>

namespace sicnu::operators::rs {

using namespace params;

Json::Value RsObiaLabelOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Source raster (georeferences the polygons)");
    props["labels"] = makeRasterParam("labels", "Segment label raster (UInt32)");
    props["training"] = makeVectorParam("training", "Training polygons with an integer class field");
    props["output"] = makeOutputParam("output", "Labeled segments CSV (segment_id,class_id)", "csv");
    props["classField"] = makeStringParam("classField", "Integer class field (fallback: class, id)", "class_id");
    props["minLabelPixels"] = makeIntegerParam(
        "minLabelPixels", "Min ROI pixels before a segment is labeled", 3);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Label CSV", "csv");
    outputs["labeled"] = makeIntegerParam("labeled", "Segments labeled", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "labels", "training", "output"});
    return root;
}

Json::Value RsObiaLabelOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("obia");
    meta["tags"].append("training");
    meta["purpose"] = "Bulk-label objects from training polygons by pixel majority";
    meta["limitations"] = "Fail-closed on unresolvable CRS/class-field/geometry; labels grid must match the source raster";
    Json::Value useCases(Json::arrayValue);
    useCases.append("Bulk training-label import for interactive OBIA sessions");
    useCases.append("Agent-side object labeling before rs:obia_classify with segmentClasses");
    meta["useCases"] = useCases;
    return meta;
}

Json::Value RsObiaLabelOperator::executionEstimate() const
{
    // FullRaster (default policy): the label map is resident; polygons stream
    // through the windowed rasterizer per feature.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 16777216; // label map + rasterizer window (16 MiB)
    return est;
}

Json::Value RsObiaLabelOperator::run(const Json::Value& params, RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string labelsPath = requireString(params, "labels");
    const std::string trainingPath = requireString(params, "training");
    const std::string outputPath = requireString(params, "output");
    const std::string classField = getString(params, "classField", "class_id");
    const int minLabelPixels = getInt(params, "minLabelPixels", 3);

    if (minLabelPixels <= 0)
        throw RSOperatorError(ErrorCode::InvalidParameter, "minLabelPixels must be > 0");
    if (!fileExists(inputPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    if (!fileExists(labelsPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Labels not found: " + labelsPath);
    if (!fileExists(trainingPath))
        throw RSOperatorError(ErrorCode::FileNotFound, "Training not found: " + trainingPath);

    context.reportProgress(0.05, "Loading label map");
    RsSegmentMap segMap = RsSegmentMap::fromGeoTIFF(QString::fromStdString(labelsPath));
    if (segMap.isEmpty())
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Label raster is empty or unreadable as a UInt32 label map");

    context.reportProgress(0.1, "Labeling segments from training polygons");
    QString roiErr;
    const QMap<quint32, int> segLabels = RsRoiLabeler::labelByMajority(
        segMap, QString::fromStdString(inputPath), QString::fromStdString(trainingPath),
        QString::fromStdString(classField), minLabelPixels, &roiErr,
        [&context]() { return context.isCancelled(); });
    context.throwIfCancelled();
    if (segLabels.isEmpty() && !roiErr.isEmpty())
        throw RSOperatorError(ErrorCode::GdalError, roiErr.toStdString());

    context.reportProgress(0.9, "Writing label CSV");
    QFile csv(QString::fromStdString(outputPath));
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text))
        throw RSOperatorError(ErrorCode::FileNotWritable, "Cannot write " + outputPath);
    QTextStream out(&csv);
    out << "segment_id,class_id\n";
    for (auto it = segLabels.constBegin(); it != segLabels.constEnd(); ++it)
        out << it.key() << ',' << it.value() << '\n';
    out.flush();
    if (csv.error() != QFile::NoError)
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed writing " + outputPath + ": " + csv.errorString().toStdString());
    csv.close();

    context.reportProgress(1.0, "ROI labeling complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["labeled"] = static_cast<int>(segLabels.size());
    return result;
}

} // namespace sicnu::operators::rs
