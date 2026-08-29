/***************************************************************************
 * rs_recode_operator.cpp  —  Class recode RSOperator
 ***************************************************************************/
#include "rs_recode_operator.h"

#include "rs_post_process.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

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

    context.throwIfCancelled();
    context.reportProgress(0.3, "Recoding class labels");

    // Streaming execution (#665, ADR 0124 grade bit-exact): pass 1 sweeps the
    // raster in row-blocks to learn the recoded value range (the save-side
    // dtype policy is a function of the OUTPUT values), pass 2 streams the
    // mapping again and writes each block. Output values are bit-identical to
    // the former full-raster path; only the memory profile changed.
    GdalDatasetWrapper inDs;
    if (!inDs.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open label raster: " + inputPath);
    }
    const int width = inDs.width();
    const int height = inDs.height();
    const int blockRows = std::max(1, std::min(256, height));
    const size_t blockSize = static_cast<size_t>(width) * blockRows;

    // Pass 1: recoded value range. Input values are read through a float
    // window (GdalDatasetWrapper's conversion read): exact for Byte/UInt16
    // label rasters; Int32 labels beyond the float mantissa (> 2^24) would
    // lose precision — pathological for class maps, noted here so the
    // boundary is explicit.
    double outMin = std::numeric_limits<double>::max();
    double outMax = std::numeric_limits<double>::lowest();
    {
        std::vector<float> block(blockSize);
        for (int y0 = 0; y0 < height; y0 += blockRows) {
            context.throwIfCancelled();
            const int rows = std::min(blockRows, height - y0);
            const size_t n = static_cast<size_t>(width) * rows;
            if (!inDs.readBandWindow(1, 0, y0, width, rows, block.data())) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read label band: " + inputPath);
            }
            for (size_t i = 0; i < n; ++i) {
                const int v = recodeMap.value(static_cast<int>(block[i]),
                                              static_cast<int>(block[i]));
                outMin = std::min(outMin, static_cast<double>(v));
                outMax = std::max(outMax, static_cast<double>(v));
            }
        }
    }
    if (outMin > outMax) { // empty raster guard
        outMin = 0;
        outMax = 0;
    }

    // Dtype policy mirrors saveLabelRaster (ADR 0019 S4): Byte / UInt16 /
    // Int32 by output range; negative labels force Int32.
    GDALDataType gdt = GDT_Byte;
    if (outMin < 0.0 || outMax > 65535.0)
        gdt = GDT_Int32;
    else if (outMax > 255.0)
        gdt = GDT_UInt16;

    QVector<QRgb> colorTable;
    loadRasterColorTable(QString::fromStdString(inputPath), colorTable);
    if (!recodeMap.isEmpty()) {
        remappedColorTable(colorTable, recodeMap);
    }

    GdalStreamingOutput output(QString::fromStdString(outputPath), width, height, 1,
                               static_cast<int>(gdt), inDs.geoTransform(),
                               inDs.projection());
    if (!output.isOpen()) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to save recoded raster: " + outputPath);
    }
    if (gdt == GDT_Byte) {
        output.setBandColorTable(1, colorTable); // palette only for Byte output
    }

    // Pass 2: stream the mapping and write the output blocks. Any failure or
    // cancel must not leave a partial raster at the output path (#647).
    try {
    const int totalBlocks = (height + blockRows - 1) / blockRows;
    int blockIndex = 0;
    bool ok = true;
    {
        std::vector<float> block(blockSize);
        std::vector<int> outBlock(blockSize);
        std::vector<quint16> u16Block(blockSize);
        std::vector<quint8> u8Block(blockSize);
        for (int y0 = 0; y0 < height && ok; y0 += blockRows, ++blockIndex) {
            context.throwIfCancelled();
            const int rows = std::min(blockRows, height - y0);
            const size_t n = static_cast<size_t>(width) * rows;
            if (!inDs.readBandWindow(1, 0, y0, width, rows, block.data())) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read label band: " + inputPath);
            }
            for (size_t i = 0; i < n; ++i) {
                const int v = recodeMap.value(static_cast<int>(block[i]),
                                              static_cast<int>(block[i]));
                outBlock[i] = v;
            }
            const GdalBlockStream::Tile tile{0, y0, width, rows, 0, width, rows,
                                             blockIndex, totalBlocks};
            const void *pixels = outBlock.data();
            if (gdt == GDT_Byte) {
                for (size_t i = 0; i < n; ++i)
                    u8Block[i] = static_cast<quint8>(outBlock[i]);
                pixels = u8Block.data();
            } else if (gdt == GDT_UInt16) {
                for (size_t i = 0; i < n; ++i)
                    u16Block[i] = static_cast<quint16>(outBlock[i]);
                pixels = u16Block.data();
            }
            // writeTile writes floats; typed label blocks go through the raw
            // window writer so the output keeps the label dtype.
            ok = output.writeTileRaw(1, tile, pixels, gdt);
            context.reportProgress(0.3 + 0.5 * (static_cast<double>(blockIndex + 1) / totalBlocks),
                                   "Recoding class labels");
        }
    }

    if (!ok) {
        output.abandon();
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to save recoded raster: " + outputPath);
    }
    QString closeError;
    if (!output.closeWithError(&closeError)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to save recoded raster: " + closeError.toStdString());
    }
    } catch (...) {
        output.abandon();
        throw;
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
