/***************************************************************************
 * rs_majority_filter_operator.cpp  —  3x3 Majority filter RSOperator
 ***************************************************************************/
#include "rs_majority_filter_operator.h"

#include "rs_post_process.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <gdal_priv.h>
#include <QFileInfo>
#include <QDir>
#include <QString>
#include <QVector>
#include <QRgb>

#include <algorithm>
#include <limits>

namespace sicnu::operators::rs {

using namespace params;

namespace {

void loadRasterColorTable( const QString &path, QVector<QRgb> &table )
{
  table.clear();
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return;
  GDALRasterBand *band = ds->GetRasterBand( 1 );
  GDALColorTable *ct = band ? band->GetColorTable() : nullptr;
  if ( ct )
  {
    const int n = ct->GetColorEntryCount();
    table.resize( std::max( 0, n ) );
    for ( int i = 0; i < n; ++i )
    {
      const GDALColorEntry *e = ct->GetColorEntry( i );
      if ( e )
        table[i] = qRgba( e->c1, e->c2, e->c3, e->c4 );
      else
        table[i] = qRgba( 0, 0, 0, 0 );
    }
  }
  GDALClose( ds );
}

} // namespace

Json::Value RsMajorityFilterOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input single-band classification label raster");
    props["output"] = makeOutputParam("output", "Output majority-filtered raster path", "tif");
    props["kernel"] = makeIntegerParam("kernel", "Sliding window kernel size (odd >= 3)", 3);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input"});
    return root;
}

Json::Value RsMajorityFilterOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("classification");
    meta["tags"].append("postprocess");
    meta["tags"].append("majority_filter");
    meta["purpose"] = "Reduce salt-and-pepper classification noise using a 3x3 majority mode filter.";
    meta["prerequisites"].append("Input raster must be a single-band integer classification raster.");
    return meta;
}

Json::Value RsMajorityFilterOperator::executionEstimate() const {
    // FullRaster (base policy): the label raster is loaded into a CV_32S
    // cv::Mat and the majority filter materializes a second full CV_32S
    // output, i.e. 2 x 1024 x 1024 x 4 B for a typical 1024x1024 input.
    Json::Value estimate(Json::objectValue);
    estimate["tileWidth"] = 0;         // full-raster processing: tiling not applicable
    estimate["tileHeight"] = 0;
    estimate["estimatedRamBytes"] = 2 * 1024 * 1024 * 4; // ~8 MiB
    return estimate;
}

Json::Value RsMajorityFilterOperator::run(const Json::Value& params, RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    std::string outputPath = getString(params, "output", "");
    if (outputPath.empty()) {
        QFileInfo fi(QString::fromStdString(inputPath));
        outputPath = fi.dir().filePath(fi.completeBaseName() + QStringLiteral("_maj.tif")).toStdString();
    }

    int kernel = getInt(params, "kernel", 3);
    if (kernel < 3 || kernel % 2 == 0) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "kernel must be an odd integer >= 3 (e.g. 3, 5, 7)");
    }

    context.logInfo("Running 3x3 majority filter on " + inputPath);
    context.reportProgress(0.1, "Loading classification label raster");

    cv::Mat labels;
    double gt[6] = { 0, 1, 0, 0, 0, -1 };
    QString wkt;
    QString err;
    if (!RsPostProcess::loadLabelRaster(QString::fromStdString(inputPath), labels, gt, wkt, &err)) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to load label raster: " + err.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(0.3, "Filtering labels");

    cv::Mat outLabels;
    if (!RsPostProcess::majorityFilter(labels, outLabels, kernel, &err, [&context]() { return context.isCancelled(); })) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Majority filter failed: " + err.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(0.8, "Saving majority-filtered raster");

    QVector<QRgb> colorTable;
    loadRasterColorTable(QString::fromStdString(inputPath), colorTable);

    QStringList creationOptions{
        QStringLiteral("TILED=YES"),
        QStringLiteral("COMPRESS=DEFLATE"),
        QStringLiteral("PREDICTOR=2")
    };

    if (!RsPostProcess::saveLabelRaster(QString::fromStdString(outputPath), outLabels, gt, wkt,
                                        colorTable, creationOptions,
                                        std::numeric_limits<double>::quiet_NaN(), &err)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to save majority-filtered raster: " + err.toStdString());
    }

    // Preserve sidecar JSON metadata if present
    QHash<int, RsClassDef> classDefs;
    if (RsPostProcess::loadClassMetaData(QString::fromStdString(inputPath), classDefs) && !classDefs.isEmpty()) {
        RsPostProcess::saveClassMetaData(QString::fromStdString(outputPath), classDefs);
    }

    context.reportProgress(1.0, "Majority filter complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    return result;
}

} // namespace sicnu::operators::rs
