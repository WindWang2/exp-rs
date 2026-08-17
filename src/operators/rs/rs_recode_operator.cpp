/***************************************************************************
 * rs_recode_operator.cpp  —  Class recode RSOperator
 ***************************************************************************/
#include "rs_recode_operator.h"

#include "rs_post_process.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <gdal_priv.h>
#include <QFileInfo>
#include <QDir>
#include <QMap>
#include <QString>
#include <QVector>
#include <QRgb>

#include <algorithm>
#include <limits>
#include <sstream>

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

void remappedColorTable( QVector<QRgb> &table, const QMap<int, int> &map )
{
  if ( table.isEmpty() || map.isEmpty() )
    return;

  QVector<QRgb> out = table;
  int maxNew = table.size() - 1;
  for ( auto it = map.constBegin(); it != map.constEnd(); ++it )
    maxNew = std::max( maxNew, it.value() );
  if ( maxNew >= out.size() )
    out.resize( maxNew + 1 );

  for ( auto it = map.constBegin(); it != map.constEnd(); ++it )
  {
    const int oldId = it.key();
    const int newId = it.value();
    if ( oldId >= 0 && oldId < table.size() && newId >= 0 )
      out[newId] = table[oldId];
  }
  table = std::move( out );
}

QMap<int, int> parseRecodeMap( const Json::Value &params )
{
  QMap<int, int> map;
  Json::Value mapValue;

  if ( params.isMember( "recode_map" ) )
    mapValue = params["recode_map"];
  else if ( params.isMember( "map" ) )
    mapValue = params["map"];
  else if ( params.isMember( "recode" ) )
    mapValue = params["recode"];

  if ( mapValue.isString() )
  {
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream iss( mapValue.asString() );
    Json::Value parsed;
    if ( Json::parseFromStream( builder, iss, &parsed, &errs ) && parsed.isObject() )
    {
      mapValue = parsed;
    }
  }

  if ( mapValue.isObject() )
  {
    for ( const auto &key : mapValue.getMemberNames() )
    {
      try
      {
        int srcId = std::stoi( key );
        int dstId = mapValue[key].asInt();
        map[srcId] = dstId;
      }
      catch ( ... )
      {
        // Skip invalid keys
      }
    }
  }

  return map;
}

} // namespace

Json::Value RsRecodeOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input single-band classification label raster");
    props["output"] = makeOutputParam("output", "Output recoded raster path", "tif");
    props["recode_map"] = makeStringParam("recode_map", "Recode mapping dictionary {from_class_id: to_class_id}");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input"});
    return root;
}

Json::Value RsRecodeOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("classification");
    meta["tags"].append("postprocess");
    meta["tags"].append("recode");
    meta["purpose"] = "Remap integer class labels according to a recode mapping table.";
    meta["prerequisites"].append("Input raster must be a single-band integer classification raster.");
    return meta;
}

Json::Value RsRecodeOperator::executionEstimate() const {
    // FullRaster (base policy): the label raster is loaded into a CV_32S
    // cv::Mat and recode writes a second full CV_32S output, i.e.
    // 2 x 1024 x 1024 x 4 B for a typical 1024x1024 input.
    Json::Value estimate(Json::objectValue);
    estimate["tileWidth"] = 0;         // full-raster processing: tiling not applicable
    estimate["tileHeight"] = 0;
    estimate["estimatedRamBytes"] = 2 * 1024 * 1024 * 4; // ~8 MiB
    return estimate;
}

Json::Value RsRecodeOperator::run(const Json::Value& params, RSOperatorContext& context) {
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
        outputPath = fi.dir().filePath(fi.completeBaseName() + QStringLiteral("_recode.tif")).toStdString();
    }

    QMap<int, int> recodeMap = parseRecodeMap(params);
    if (recodeMap.isEmpty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "recode_map must contain at least one valid {from_class: to_class} mapping");
    }

    context.logInfo("Running class recode on " + inputPath);
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
    context.reportProgress(0.3, "Recoding class labels");

    cv::Mat outLabels;
    if (!RsPostProcess::recode(labels, outLabels, recodeMap, &err)) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Recode failed: " + err.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(0.8, "Saving recoded raster");

    QVector<QRgb> colorTable;
    loadRasterColorTable(QString::fromStdString(inputPath), colorTable);
    if (!recodeMap.isEmpty()) {
        remappedColorTable(colorTable, recodeMap);
    }

    QStringList creationOptions{
        QStringLiteral("TILED=YES"),
        QStringLiteral("COMPRESS=DEFLATE"),
        QStringLiteral("PREDICTOR=2")
    };

    if (!RsPostProcess::saveLabelRaster(QString::fromStdString(outputPath), outLabels, gt, wkt,
                                        colorTable, creationOptions,
                                        std::numeric_limits<double>::quiet_NaN(), &err)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to save recoded raster: " + err.toStdString());
    }

    // Update and preserve sidecar JSON metadata if present
    QHash<int, RsClassDef> classDefs;
    if (RsPostProcess::loadClassMetaData(QString::fromStdString(inputPath), classDefs) && !classDefs.isEmpty()) {
        if (!recodeMap.isEmpty()) {
            QHash<int, RsClassDef> remappedDefs;
            for (auto it = classDefs.constBegin(); it != classDefs.constEnd(); ++it) {
                int oldId = it.key();
                int newId = recodeMap.value(oldId, oldId);
                RsClassDef oldDef = it.value();
                RsClassDef newDef( newId, oldDef.name(), oldDef.color() );
                if (!remappedDefs.contains(newId)) {
                    remappedDefs[newId] = newDef;
                }
            }
            RsPostProcess::saveClassMetaData(QString::fromStdString(outputPath), remappedDefs);
        } else {
            RsPostProcess::saveClassMetaData(QString::fromStdString(outputPath), classDefs);
        }
    }

    context.reportProgress(1.0, "Recode complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    return result;
}

} // namespace sicnu::operators::rs
