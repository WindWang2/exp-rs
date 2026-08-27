/***************************************************************************
 * rs_apply_mask_operator.cpp  —  Apply a QA mask to a product raster
 ***************************************************************************/
#include "rs_apply_mask_operator.h"

#include "data/raster_grid_compat.h"
#include "processing/algorithms/satellite_products.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

/// Block height for the streaming apply pass. Bounds memory to
/// O(width * blockRows * 2 floats) regardless of raster height.
constexpr int kBlockRows = 256;

/// Maps an input-grid pixel (px, py) to a mask-grid pixel coordinate using the
/// inverse of the mask's affine transform. Returns {-1, -1} when the mapping
/// places the pixel outside the mask raster.
struct MaskCoord { long col = -1; long row = -1; };

MaskCoord mapToMask(const std::array<double, 6>& inGt,
                    const std::array<double, 6>& maskGt,
                    int maskWidth, int maskHeight,
                    int px, int py)
{
    // Input pixel center in geo coordinates.
    const double x = px + 0.5, y = py + 0.5;
    const double geoX = inGt[0] + x * inGt[1] + y * inGt[2];
    const double geoY = inGt[3] + x * inGt[4] + y * inGt[5];

    // Inverse of the mask's 2x2 linear part.
    const double a = maskGt[1], b = maskGt[2];
    const double d = maskGt[4], e = maskGt[5];
    const double det = a * e - b * d;
    if (std::abs(det) < 1e-12)
        return {};

    const double dx = geoX - maskGt[0];
    const double dy = geoY - maskGt[3];
    const double mCol = (dx * e - dy * b) / det;
    const double mRow = (dy * a - dx * d) / det;

    const long col = static_cast<long>(std::floor(mCol));
    const long row = static_cast<long>(std::floor(mRow));
    if (col < 0 || row < 0 || col >= maskWidth || row >= maskHeight)
        return {};
    return {col, row};
}

/// Maps the corners of an input block to the mask grid and returns the bounding
/// box (clamped to the mask raster) to read. Returns false when no part of the
/// block intersects the mask raster.
bool maskWindowBounds(const std::array<double, 6>& inGt,
                      const std::array<double, 6>& maskGt,
                      int maskWidth, int maskHeight,
                      int x0, int y0, int w, int h,
                      long* col0, long* row0, long* col1, long* row1)
{
    const double a = maskGt[1], b = maskGt[2];
    const double d = maskGt[4], e = maskGt[5];
    const double det = a * e - b * d;
    if (std::abs(det) < 1e-12)
        return false;

    double minMCol = 1e18, minMRow = 1e18, maxMCol = -1e18, maxMRow = -1e18;
    const int corners[4][2] = {{x0, y0},
                               {x0 + w, y0},
                               {x0, y0 + h},
                               {x0 + w, y0 + h}};
    for (const auto& c : corners) {
        const double x = inGt[0] + c[0] * inGt[1] + c[1] * inGt[2];
        const double y = inGt[3] + c[0] * inGt[4] + c[1] * inGt[5];
        const double dx = x - maskGt[0];
        const double dy = y - maskGt[3];
        const double mc = (dx * e - dy * b) / det;
        const double mr = (dy * a - dx * d) / det;
        minMCol = (std::min)(minMCol, mc);
        minMRow = (std::min)(minMRow, mr);
        maxMCol = (std::max)(maxMCol, mc);
        maxMRow = (std::max)(maxMRow, mr);
    }

    if (maxMCol < 0.0 || minMCol >= maskWidth || maxMRow < 0.0 || minMRow >= maskHeight)
        return false;

    const long c0 = std::clamp(static_cast<long>(std::floor(minMCol)), 0L, static_cast<long>(maskWidth - 1));
    const long r0 = std::clamp(static_cast<long>(std::floor(minMRow)), 0L, static_cast<long>(maskHeight - 1));
    const long c1 = std::clamp(static_cast<long>(std::ceil(maxMCol)), 0L, static_cast<long>(maskWidth - 1));
    const long r1 = std::clamp(static_cast<long>(std::ceil(maxMRow)), 0L, static_cast<long>(maskHeight - 1));

    if (c0 > c1 || r0 > r1)
        return false;

    *col0 = c0;
    *row0 = r0;
    *col1 = c1;
    *row1 = r1;
    return true;
}

} // anonymous namespace

Json::Value RsApplyMaskOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Product raster to mask (multi-band)");
    props["mask"] = makeRasterParam("mask", "Binary mask raster (band 1, value > 0 = masked)");
    props["output"] = makeOutputParam("output", "Output raster with masked pixels set to NoData", "tif");
    props["no_data"] = makeNumberParam("no_data", "NoData value for input bands that define none (required per-band in that case)", 0.0);
    props["align_mask"] = makeBooleanParam("align_mask", "Nearest-neighbor resample the mask onto the input grid when grids differ (same CRS only)", true);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["maskedPixels"] = makeIntegerParam("maskedPixels", "Masked pixel count", 0);
    outputs["totalPixels"] = makeIntegerParam("totalPixels", "Evaluated pixel count (input grid)", 0);
    outputs["maskedPercent"] = makeNumberParam("maskedPercent", "Masked pixel percentage", 0.0);
    outputs["aligned"] = makeBooleanParam("aligned", "Whether the mask was resampled onto the input grid", false);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "mask", "output"});
    return root;
}

Json::Value RsApplyMaskOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("qa");
    meta["tags"].append("cloud");
    meta["tags"].append("mask");
    meta["tags"].append("analysis-ready");
    meta["purpose"] = "Turn a binary QA mask into an analysis-ready product by "
                      "setting obscured pixels to NoData in every band.";
    meta["prerequisites"].append("Input and mask share a CRS (same-CRS grid differences are auto-aligned).");
    meta["prerequisites"].append("Input bands must define a NoData value, or `no_data` must be provided for bands without one.");
    meta["workflowHints"].append("Run rs:qa_mask first, then apply the mask before computing indices or change detection.");
    meta["workflowHints"].append("The output keeps band roles and wavelength metadata, so downstream operators stay product-aware.");
    meta["limitations"].append("Grid alignment uses nearest-neighbor sampling (appropriate for integer masks); CRS mismatches are not auto-corrected.");
    return meta;
}

Json::Value RsApplyMaskOperator::executionEstimate() const {
    // Streaming: full-width x kBlockRows (256) strips. Peak per block is the
    // float input buffer + float mask window + int32 mask offsets = 12
    // bytes/pixel; typical 1024x1024 input -> 1024x256 block -> ~3 MiB.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 1024;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 3145728;
    return est;
}

Json::Value RsApplyMaskOperator::run(const Json::Value& params,
                                     RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string maskPath = requireString(params, "mask");
    const std::string outputPath = requireString(params, "output");
    const bool hasNoDataParam = params.isMember("no_data");
    const double noDataParam = getDouble(params, "no_data", 0.0);
    const bool alignMask = getBool(params, "align_mask", true);

    for (const std::string& p : {inputPath, maskPath}) {
        if (!fileExists(p)) {
            throw RSOperatorError(ErrorCode::FileNotFound,
                                  "Input raster not found: " + p);
        }
    }

    ensureGdalInit();

    GdalDatasetWrapper input;
    if (!input.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open input raster: " + inputPath);
    }
    GdalDatasetWrapper mask;
    if (!mask.open(QString::fromStdString(maskPath))) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to open mask raster: " + maskPath);
    }

    const int width = input.width();
    const int height = input.height();
    const int bandCount = input.bandCount();
    if (bandCount < 1) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Input raster has no bands: " + inputPath);
    }
    if (mask.bandCount() < 1) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Mask raster has no bands: " + maskPath);
    }

    // Grid compatibility (shared service, ADR-0065 / A4): same-CRS grid
    // differences can be auto-aligned with nearest-neighbor sampling; CRS
    // mismatches are never auto-corrected here.
    const data::RasterGrid inputGrid = sicnu::processing::gridFromDataset(input);
    const data::RasterGrid maskGrid = sicnu::processing::gridFromDataset(mask);
    const data::GridCompatReport report = data::compareGrids(inputGrid, maskGrid);

    bool aligned = false;
    if (!report.compatible()) {
        if (const auto primary = report.primaryBlocking()) {
            const auto verdict = primary->verdict;
            if (verdict == data::GridCompatVerdict::CrsMismatch ||
                verdict == data::GridCompatVerdict::MissingCrs) {
                throw RSOperatorError(ErrorCode::InvalidInputData,
                                      "Cannot apply mask: " + primary->message.toStdString());
            }
            if (!alignMask) {
                throw RSOperatorError(ErrorCode::InvalidInputData,
                                      "Cannot apply mask: " + primary->message.toStdString());
            }
            aligned = true;
        }
    }

    // Ungeoreferenced fallback: same dimensions only.
    if (!inputGrid.hasGeoTransform && !maskGrid.hasGeoTransform) {
        if (mask.width() != width || mask.height() != height) {
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Mask and input have different dimensions and neither is "
                "georeferenced; cannot align the mask");
        }
    }

    // Per-band output NoData: reuse the input band's value; a band without one
    // requires the explicit `no_data` parameter.
    std::vector<double> outputNoData(bandCount);
    for (int b = 1; b <= bandCount; ++b) {
        bool hasNoData = false;
        const double bandNoData = input.bandNoDataValue(b, &hasNoData);
        if (hasNoData) {
            outputNoData[b - 1] = bandNoData;
        } else if (hasNoDataParam) {
            outputNoData[b - 1] = noDataParam;
        } else {
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "Input band " + std::to_string(b) + " defines no NoData value; "
                "pass `no_data` to choose the value masked pixels receive");
        }
    }

    context.logInfo("Applying mask (grid " +
                    std::string(aligned ? "auto-aligned" : "aligned") + ")");

    // Streaming pass: block rows, full width. Memory is O(width * kBlockRows).
    const size_t maxBlockPixels = static_cast<size_t>(width) * kBlockRows;
    std::vector<float> inputBuf(maxBlockPixels);
    std::vector<float> maskBuf;

    const int dtype = input.bandDataType(1);
    // #612: Float64/Int32/UInt32 inputs must not round-trip through a float32
    // buffer (silent precision loss: float64 mantissa, int32 > 2^24). These
    // dtypes use native-type GDALRasterIO; all others are exact in float32.
    const bool nativeDtypePath = (dtype == GDT_Float64 || dtype == GDT_Int32 || dtype == GDT_UInt32);
    std::vector<double> dblBuf;
    std::vector<int32_t> i32Buf;
    std::vector<uint32_t> u32Buf;
    if (nativeDtypePath) {
        if (dtype == GDT_Float64)
            dblBuf.resize(maxBlockPixels);
        else if (dtype == GDT_Int32)
            i32Buf.resize(maxBlockPixels);
        else
            u32Buf.resize(maxBlockPixels);
    }

    const std::array<double, 6> inGt = input.geoTransform();
    const std::array<double, 6> maskGt = mask.geoTransform();
    const bool sameGrid = !aligned;

    GdalDatasetWrapper out;
    QString errorMessage;
    if (!out.create(QString::fromStdString(outputPath), width, height, bandCount,
                    dtype, inGt, input.projection(), &errorMessage)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "Failed to create output raster: " +
                                  errorMessage.toStdString());
    }

    size_t masked = 0;
    const size_t totalPixels = static_cast<size_t>(width) * height;

    // Per-block mask offsets into maskBuf (flat index, -1 = outside the mask
    // window / clear): computed ONCE per block and shared by every band, so
    // the affine mapping and the same-grid indexing leave the per-pixel hot
    // path (ADR 0105 review remediation).
    std::vector<int32_t> maskOffsets(static_cast<size_t>(width) * kBlockRows, -1);

    for (int y0 = 0; y0 < height; y0 += kBlockRows) {
        const int blockH = (std::min)(kBlockRows, height - y0);
        const size_t blockPixels = static_cast<size_t>(width) * blockH;

        // Read the mask region covering this block (resampled or same grid).
        long mCol0 = 0, mRow0 = 0, mCol1 = -1, mRow1 = -1;
        bool haveMaskWindow = true;
        if (sameGrid) {
            mCol0 = 0;
            mRow0 = y0;
            mCol1 = width - 1;
            mRow1 = y0 + blockH - 1;
        } else {
            if (!maskWindowBounds(inGt, maskGt, mask.width(), mask.height(),
                                  0, y0, width, blockH,
                                  &mCol0, &mRow0, &mCol1, &mRow1)) {
                // Block lies entirely outside the mask extent: all pixels clear.
                haveMaskWindow = false;
            }
        }

        std::fill(maskOffsets.begin(), maskOffsets.begin() + static_cast<ptrdiff_t>(blockPixels), -1);
        if (haveMaskWindow) {
            const long mWindowW = mCol1 - mCol0 + 1;
            const long mWindowH = mRow1 - mRow0 + 1;
            maskBuf.resize(static_cast<size_t>(mWindowW) * mWindowH);
            if (!mask.readBandWindow(1, static_cast<int>(mCol0), static_cast<int>(mRow0),
                                     static_cast<int>(mWindowW), static_cast<int>(mWindowH),
                                     maskBuf.data())) {
                throw RSOperatorError(ErrorCode::GdalError,
                                      "Failed to read mask window");
            }
            for (int row = 0; row < blockH; ++row) {
                for (int col = 0; col < width; ++col) {
                    const size_t idx = static_cast<size_t>(row) * width + col;
                    if (sameGrid) {
                        maskOffsets[idx] = static_cast<int32_t>(
                            (y0 + row - mRow0) * mWindowW + (col - mCol0));
                    } else {
                        const MaskCoord m = mapToMask(inGt, maskGt, mask.width(),
                                                      mask.height(), col, y0 + row);
                        if (m.col >= mCol0 && m.col <= mCol1 && m.row >= mRow0 && m.row <= mRow1) {
                            const int32_t off = static_cast<int32_t>((m.row - mRow0) * mWindowW + (m.col - mCol0));
                            maskOffsets[idx] = (off >= 0 && static_cast<size_t>(off) < maskBuf.size()) ? off : -1;
                        } else {
                            maskOffsets[idx] = -1;
                        }
                    }
                }
            }
        }

        for (int b = 1; b <= bandCount; ++b) {
            const double noData = outputNoData[b - 1];
            if (nativeDtypePath) {
                void *raw = dtype == GDT_Float64 ? static_cast<void *>(dblBuf.data())
                           : dtype == GDT_Int32  ? static_cast<void *>(i32Buf.data())
                                                 : static_cast<void *>(u32Buf.data());
                GDALRasterBandH inBand = GDALGetRasterBand(input.dataset(), b);
                if (!inBand
                    || GDALRasterIO(inBand, GF_Read, 0, y0, width, blockH, raw,
                                    width, blockH, static_cast<GDALDataType>(dtype), 0, 0) != CE_None) {
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to read input band " + std::to_string(b));
                }
                for (size_t i = 0; i < blockPixels; ++i) {
                    const int32_t off = maskOffsets[i];
                    if (off >= 0 && maskBuf[static_cast<size_t>(off)] > 0.0f) {
                        if (dtype == GDT_Float64)
                            dblBuf[i] = noData;
                        else if (dtype == GDT_Int32)
                            i32Buf[i] = static_cast<int32_t>(std::llround(noData));
                        else
                            u32Buf[i] = static_cast<uint32_t>(std::llround(noData));
                        if (b == 1)
                            ++masked;
                    }
                }
                GDALRasterBandH outBand = GDALGetRasterBand(out.dataset(), b);
                if (!outBand
                    || GDALRasterIO(outBand, GF_Write, 0, y0, width, blockH, raw,
                                    width, blockH, static_cast<GDALDataType>(dtype), 0, 0) != CE_None) {
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to write output band " + std::to_string(b));
                }
            } else {
                if (!input.readBandWindow(b, 0, y0, width, blockH, inputBuf.data())) {
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to read input band " + std::to_string(b));
                }
                for (size_t i = 0; i < blockPixels; ++i) {
                    const int32_t off = maskOffsets[i];
                    if (off >= 0 && maskBuf[static_cast<size_t>(off)] > 0.0f) {
                        inputBuf[i] = static_cast<float>(noData);
                        if (b == 1)
                            ++masked;
                    }
                }
                if (!out.writeBandWindow(b, 0, y0, width, blockH, inputBuf.data())) {
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "Failed to write output band " + std::to_string(b));
                }
            }
        }

        context.throwIfCancelled();
        context.reportProgress(static_cast<double>(y0 + blockH) / height,
                               "Applying mask");
    }

    // Output band semantics: NoData values and product metadata (band roles,
    // wavelengths) so downstream operators stay product-aware.
    for (int b = 1; b <= bandCount; ++b) {
        GDALRasterBandH outBand = GDALGetRasterBand(out.dataset(), b);
        if (outBand)
            GDALSetRasterNoDataValue(outBand, outputNoData[b - 1]);
        const QString role = input.bandMetadataItem(b, "SICNU_BAND_ROLE");
        if (!role.isEmpty())
            GDALSetMetadataItem(outBand, "SICNU_BAND_ROLE", role.toUtf8().constData(), nullptr);
        const QString wavelength = input.bandMetadataItem(b, "WAVELENGTH");
        if (!wavelength.isEmpty())
            GDALSetMetadataItem(outBand, "WAVELENGTH", wavelength.toUtf8().constData(), nullptr);
        const QString fwhm = input.bandMetadataItem(b, "FWHM");
        if (!fwhm.isEmpty())
            GDALSetMetadataItem(outBand, "FWHM", fwhm.toUtf8().constData(), nullptr);
    }
    GDALSetMetadataItem(out.dataset(), "SICNU_MASKED_BY", maskPath.c_str(), nullptr);
    // The radiometric state is dataset-level product metadata; carry it over so
    // the calibration → apply-mask → change-detection chain keeps its
    // comparability check working (ADR 0114).
    const QString radiometricState =
        SatelliteProducts::readRadiometricState(QString::fromStdString(inputPath));
    if (!radiometricState.isEmpty()) {
        GDALSetMetadataItem(out.dataset(), SatelliteProducts::kRadiometricStateKey,
                            radiometricState.toUtf8().constData(), nullptr);
    }
    out.close();
    // Deferred flush/trailer errors only surface at close; a truncated output
    // must not be reported as success (ADR 0105 review remediation).
    if (CPLGetLastErrorType() != CE_None) {
        throw RSOperatorError(ErrorCode::GdalError,
                              "Failed to finalize output raster: " + outputPath);
    }

    context.reportProgress(1.0, "Mask applied");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["maskedPixels"] = static_cast<Json::UInt64>(masked);
    result["totalPixels"] = static_cast<Json::UInt64>(totalPixels);
    result["maskedPercent"] = totalPixels == 0
        ? 0.0
        : 100.0 * static_cast<double>(masked) / static_cast<double>(totalPixels);
    result["aligned"] = aligned;
    return result;
}

} // namespace sicnu::operators::rs
