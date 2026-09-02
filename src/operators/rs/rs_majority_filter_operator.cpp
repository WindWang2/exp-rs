/***************************************************************************
 * rs_majority_filter_operator.cpp  —  3x3 Majority filter RSOperator
 ***************************************************************************/
#include "rs_majority_filter_operator.h"

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
#include <QString>
#include <QVector>
#include <QRgb>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

    context.logInfo("Running majority filter (kernel=" + std::to_string(kernel) + ") on " + inputPath);
    context.reportProgress(0.1, "Loading classification label raster");

    // Streaming execution (#666, ADR 0124 grade bit-exact): row-blocks with a
    // halo of kernel/2 pixels so every window sees the same neighbor values it
    // saw in the full-raster path. The mode rule is replicated exactly:
    // label 0 AND the declared NoData sentinel never vote (#700), ties break
    // toward the smaller label, an all-NoData window keeps the center pixel,
    // and a NoData CENTER stays NoData (#700) — classes are never grown into
    // NoData areas. Values pass through the same GDT_Int32 conversion
    // loadLabelRaster used.
    const int half = kernel / 2;
    GdalDatasetWrapper inDs;
    if (!inDs.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open label raster: " + inputPath);
    }
    // #700: the declared band NoData is a non-voting sentinel alongside the
    // label-0 convention. The threshold-mask output writes NoData=255, and
    // with only label 0 excluded those pixels won majorities and grew into
    // valid regions.
    bool hasDeclaredNoData = false;
    const double declaredNoData = inDs.bandNoDataValue(1, &hasDeclaredNoData);
    const bool excludeDeclared = hasDeclaredNoData && std::isfinite(declaredNoData);
    const int declaredNoDataLabel =
        excludeDeclared ? static_cast<int>(declaredNoData) : 0;
    const int width = inDs.width();
    const int height = inDs.height();
    const int blockRows = std::max(kernel, std::min(256, height));
    const int haloRows = std::min(half, height);

    // Output dtype: labels pass through unchanged outside the filter, so the
    // value range equals the input's; scan once to apply the ADR-0019-S4
    // policy the save path used (Byte/UInt16/Int32). Labels are read through
    // a float window (exact for Byte/UInt16; Int32 beyond 2^24 is pathological
    // for class maps and would lose precision — stated boundary).
    double inMin = std::numeric_limits<double>::max();
    double inMax = std::numeric_limits<double>::lowest();
    {
        std::vector<float> sweep(static_cast<size_t>(width) * std::min(blockRows, height));
        for (int y0 = 0; y0 < height; y0 += blockRows) {
            const int rows = std::min(blockRows, height - y0);
            if (!inDs.readBandWindow(1, 0, y0, width, rows, sweep.data())) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read label band: " + inputPath);
            }
            const size_t n = static_cast<size_t>(width) * rows;
            for (size_t i = 0; i < n; ++i) {
                inMin = std::min(inMin, static_cast<double>(sweep[i]));
                inMax = std::max(inMax, static_cast<double>(sweep[i]));
            }
        }
    }
    if (inMin > inMax) {
        inMin = 0;
        inMax = 0;
    }
    GDALDataType gdt = GDT_Byte;
    if (inMin < 0.0 || inMax > 65535.0)
        gdt = GDT_Int32;
    else if (inMax > 255.0)
        gdt = GDT_UInt16;

    QVector<QRgb> colorTable;
    loadRasterColorTable(QString::fromStdString(inputPath), colorTable);

    GdalStreamingOutput output(QString::fromStdString(outputPath), width, height, 1,
                               static_cast<int>(gdt), inDs.geoTransform(),
                               inDs.projection());
    if (!output.isOpen()) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to save majority-filtered raster: " + outputPath);
    }
    if (gdt == GDT_Byte) {
        output.setBandColorTable(1, colorTable);
    }

    // Any failure/cancel must not leave a partial raster at the output path
    // (#647 streaming-output contract).
    try {
    const int totalBlocks = (height + blockRows - 1) / blockRows;
    int blockIndex = 0;
    bool ok = true;
    for (int y0 = 0; y0 < height && ok; y0 += blockRows, ++blockIndex) {
        context.throwIfCancelled();
        const int rows = std::min(blockRows, height - y0);
        const int readY0 = std::max(0, y0 - half);
        const int readEnd = std::min(height, y0 + rows + half); // exclusive
        const int readRows = readEnd - readY0;
        const size_t readCount = static_cast<size_t>(width) * readRows;

        std::vector<int> labels(readCount);
        {
            std::vector<float> block(readCount);
            if (!inDs.readBandWindow(1, 0, readY0, width, readRows, block.data())) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read label band: " + inputPath);
            }
            for (size_t i = 0; i < readCount; ++i) {
                const float fv = block[i];
                // NaN never converts to int (UB); a NaN NoData label acts as
                // the non-voting 0 sentinel (#700).
                labels[i] = std::isfinite(fv) ? static_cast<int>(fv) : 0;
            }
        }

        // mode of the k*k window at (blockRow r, col c), matching
        // RsPostProcess::majorityFilter exactly (incl. boundary clamping).
        const auto pixelAt = [&](int absRow, int c) {
            return labels[static_cast<size_t>(absRow - readY0) * width + c];
        };
        std::vector<int> outRow(static_cast<size_t>(width) * rows);
        struct FreqEntry { int val; int count; };
        std::vector<FreqEntry> freq;
        freq.reserve(static_cast<size_t>(kernel) * kernel);
        for (int r = 0; r < rows; ++r) {
            const int absR = y0 + r;
            for (int c = 0; c < width; ++c) {
                const int r0 = std::max(0, absR - half);
                const int r1 = std::min(height - 1, absR + half);
                const int c0 = std::max(0, c - half);
                const int c1 = std::min(width - 1, c + half);
                freq.clear();
                for (int rr = r0; rr <= r1; ++rr) {
                    for (int cc = c0; cc <= c1; ++cc) {
                        const int v = pixelAt(rr, cc);
                        // Label 0 AND the declared NoData sentinel never vote
                        // (#700).
                        if (v == 0 || (excludeDeclared && v == declaredNoDataLabel))
                            continue;
                        bool found = false;
                        for (FreqEntry &e : freq) {
                            if (e.val == v) {
                                ++e.count;
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                            freq.push_back({ v, 1 });
                    }
                }
                // A NoData CENTER stays NoData (#700): the filter must not
                // grow classes into NoData areas, it only relabels valid
                // centers — under either NoData convention (label 0 or the
                // declared sentinel). Matches RsPostProcess::majorityFilter.
                const int center = pixelAt(absR, c);
                if (center == 0 || (excludeDeclared && center == declaredNoDataLabel)) {
                    outRow[static_cast<size_t>(r) * width + c] = center;
                    continue;
                }
                int bestVal = center;
                int bestCnt = -1;
                for (const FreqEntry &e : freq) {
                    if (e.count > bestCnt || (e.count == bestCnt && e.val < bestVal)) {
                        bestCnt = e.count;
                        bestVal = e.val;
                    }
                }
                outRow[static_cast<size_t>(r) * width + c] = bestVal;
            }
        }

        // Typed output block (labels keep their integer dtype).
        const size_t n = static_cast<size_t>(width) * rows;
        std::vector<quint8> u8;
        std::vector<quint16> u16;
        const void *pixels = outRow.data();
        if (gdt == GDT_Byte) {
            u8.resize(n);
            for (size_t i = 0; i < n; ++i)
                u8[i] = static_cast<quint8>(outRow[i]);
            pixels = u8.data();
        } else if (gdt == GDT_UInt16) {
            u16.resize(n);
            for (size_t i = 0; i < n; ++i)
                u16[i] = static_cast<quint16>(outRow[i]);
            pixels = u16.data();
        }
        const GdalBlockStream::Tile tile{0, y0, width, rows, 0, width, rows,
                                         blockIndex, totalBlocks};
        ok = output.writeTileRaw(1, tile, pixels, gdt);
        context.reportProgress(0.1 + 0.7 * (static_cast<double>(blockIndex + 1) / totalBlocks),
                               "Filtering labels");
    }

    context.throwIfCancelled();
    context.reportProgress(0.8, "Saving majority-filtered raster");

    if (!ok) {
        output.abandon();
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to save majority-filtered raster: " + outputPath);
    }
    QString closeError;
    if (!output.closeWithError(&closeError)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to save majority-filtered raster: " + closeError.toStdString());
    }
    } catch (...) {
        output.abandon();
        throw;
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
