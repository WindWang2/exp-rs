/***************************************************************************
 * rs_feature_normalize_operator.cpp — Feature cube normalization (Platform 3.0)
 ***************************************************************************/
#include "rs_feature_normalize_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/features/feature_cube.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <QFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = { "zscore", "minmax", "robust" };
constexpr int kTileSize = 256;
constexpr int kHistogramBins = 512;

/// Welford online mean/variance accumulator (numerically stable single pass).
struct Welford
{
    std::uint64_t n = 0;
    double mean = 0.0;
    double m2 = 0.0;
    void add( double v )
    {
        ++n;
        const double delta = v - mean;
        mean += delta / n;
        m2 += delta * ( v - mean );
    }
    double stddev() const { return n > 1 ? std::sqrt( m2 / ( n - 1 ) ) : 0.0; }
};

/// Per-band normalization parameters as stored in the contract:
/// zscore → [mean, std]; minmax → [min, max]; robust → [p0.5, p99.5] range.
struct BandStats
{
    double a = 0.0;
    double b = 0.0;
    bool degenerate = false; ///< std<=0 / zero range: the band is left as-is
};

/// Plain-raster fallback contract: bands without a cube contract get
/// synthetic ids "band_1..N" and no semantic roles.
sicnu::features::FeatureCubeContract plainContract( int bandCount )
{
    sicnu::features::FeatureCubeContract c;
    c.featureId = QStringLiteral( "band_raster" );
    c.bands.reserve( bandCount );
    for ( int b = 1; b <= bandCount; ++b ) {
        sicnu::features::FeatureBand fb;
        fb.id = QStringLiteral( "band_%1" ).arg( b );
        fb.band = b;
        c.bands.push_back( fb );
    }
    return c;
}

} // anonymous namespace

Json::Value RsFeatureNormalizeOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input",
                                     "Input raster (a feature cube is recommended; plain "
                                     "rasters get synthetic band ids band_1..N)");
    props["output"] = makeOutputParam("output", "Output normalized feature cube (Float32)", "tif");
    props["method"] = makeEnumParam("method",
                                    "Normalization method: zscore ((v-mean)/std), minmax "
                                    "((v-min)/(max-min)) or robust ((v-p0.5)/(p99.5-p0.5))",
                                    s_methods, "zscore");
    props["inverse"] = makeBooleanParam("inverse",
                                        "Apply the EXISTING stored stats inversely "
                                        "(un-normalize) instead of fitting new ones",
                                        false);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Normalized feature cube path");
    outputs["method"] = makeStringParam("method", "Applied method (or inverse_<method>)");
    outputs["bands"] = makeIntegerParam("bands", "Number of normalized bands");
    Json::Value statsOut(Json::objectValue);
    statsOut["type"] = "array";
    statsOut["description"] = "Per-band stats as nested arrays: [mean,std], [min,max] or "
                              "[p0.5,p99.5]";
    outputs["stats"] = statsOut;

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsFeatureNormalizeOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("features");
    meta["tags"].append("normalization");
    meta["tags"].append("standardization");
    meta["tags"].append("ml");
    meta["purpose"] = "Standardize feature cube bands and persist the fit statistics INTO the "
                      "cube contract, so the exact same stats ride the file into training and "
                      "inference (train/inference consistency) instead of living in a side file.";
    meta["prerequisites"].append("Input raster (feature cube recommended); inverse=true requires "
                                 "a cube whose contract carries normalization stats.");
    meta["workflowHints"].append("Fit once on the training scene(s); the stats travel inside the "
                                 "cube, so rs:infer preprocessing should read "
                                 "contract.normalization rather than re-fitting per tile.");
    meta["workflowHints"].append("Run with inverse=true to undo a previous normalization (e.g. "
                                 "before re-fitting with another method).");
    meta["limitations"].append("NoData-aware: declared NoData sentinels and non-finite pixels are "
                               "excluded from the fit and left unchanged.");
    meta["limitations"].append("Constant bands (std<=0 or zero range) are skipped with a warning.");
    meta["limitations"].append("robust approximates the 0.5/99.5 percentile range from a 512-bin "
                               "histogram, not exact percentiles.");
    return meta;
}

Json::Value RsFeatureNormalizeOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = kTileSize;
    est["tileHeight"] = kTileSize;
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * kTileSize * kTileSize * 4ULL
                                                    + kHistogramBins * sizeof( std::uint64_t ) );
    return est;
}

Json::Value RsFeatureNormalizeOperator::run(const Json::Value& params,
                                            RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }
    const std::string method = getEnum(params, "method", s_methods, "zscore");
    const bool inverse = getBool(params, "inverse", false);

    // Contract: cubes carry their band identity; plain rasters get a
    // synthesized one (ids "band_1..N", no roles).
    sicnu::features::FeatureCubeContract contract;
    QString contractError;
    if (!sicnu::features::readFeatureCubeMetadata(QString::fromStdString(inputPath),
                                                  &contract, &contractError)) {
        context.logInfo("No feature cube contract on '" + inputPath +
                        "'; synthesizing band identities band_1..N");
    }

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }
    const int bandCount = src.bandCount();
    const int width = src.width();
    const int height = src.height();
    if (contract.bands.isEmpty()
        || static_cast<int>(contract.bands.size()) != bandCount) {
        if (!contract.bands.isEmpty()) {
            context.logWarning("Feature cube contract lists " +
                               std::to_string(contract.bands.size()) +
                               " bands but the raster has " + std::to_string(bandCount) +
                               "; rebuilding a plain band identity");
        }
        contract = plainContract(bandCount);
    }

    // Effective NoData sentinel per band: the raster's declared value, falling
    // back to the contract's; NaN (undeclared) otherwise.
    std::vector<double> nodataRaw(bandCount, 0.0);
    std::vector<bool> hasNodata(bandCount, false);
    for (int b = 0; b < bandCount; ++b) {
        bool has = false;
        nodataRaw[b] = src.bandNoDataValue(b + 1, &has);
        if (!has && !std::isnan(contract.bands[b].nodata)) {
            nodataRaw[b] = contract.bands[b].nodata;
            has = true;
        }
        hasNodata[b] = has;
    }
    auto isValid = [&nodataRaw, &hasNodata](int b, float v) {
        return std::isfinite(v) && (!hasNodata[b] || v != static_cast<float>(nodataRaw[b]));
    };

    // --- Resolve the stats: parse stored (inverse) or fit (pass 1) ----------
    std::vector<BandStats> stats(bandCount);
    Json::Value normParams(Json::arrayValue); // per-band [a, b] rows
    std::string methodUsed = method;

    if (inverse) {
        const Json::Value& stored = contract.normalization;
        const bool usable = stored.isObject() && stored.isMember("method")
                            && stored["method"].isString() && stored.isMember("params")
                            && stored["params"].isArray()
                            && static_cast<int>(stored["params"].size()) == bandCount;
        if (!usable) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "nothing to invert: '" + inputPath +
                                      "' carries no usable normalization stats in its "
                                      "feature cube contract");
        }
        methodUsed = stored["method"].asString();
        if (std::find(s_methods.begin(), s_methods.end(), methodUsed) == s_methods.end()) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "unknown normalization method '" + methodUsed +
                                      "' stored in the contract");
        }
        if (stored.isMember("applied") && stored["applied"].isBool()
            && !stored["applied"].asBool()) {
            context.logWarning("Stored normalization stats are marked as not applied; "
                               "inverting anyway");
        }
        for (int b = 0; b < bandCount; ++b) {
            const Json::Value& row = stored["params"][b];
            if (!row.isArray() || row.size() != 2 || !row[0].isNumeric() || !row[1].isNumeric()) {
                throw RSOperatorError(ErrorCode::InvalidParameter,
                                      "normalization params for band " +
                                          std::to_string(b + 1) + " must be an [a, b] pair");
            }
            stats[b].a = row[0].asDouble();
            stats[b].b = row[1].asDouble();
            Json::Value outRow(Json::arrayValue);
            outRow.append(stats[b].a);
            outRow.append(stats[b].b);
            normParams.append(outRow);
        }
    } else {
        // Pass 1: Welford mean/variance + min/max over valid pixels.
        std::vector<Welford> welford(bandCount);
        std::vector<double> vmin(bandCount, std::numeric_limits<double>::infinity());
        std::vector<double> vmax(bandCount, -std::numeric_limits<double>::infinity());
        std::vector<float> tile(static_cast<size_t>(kTileSize) * kTileSize);
        for (int b = 0; b < bandCount; ++b) {
            for (int y = 0; y < height; y += kTileSize) {
                const int h = std::min(kTileSize, height - y);
                for (int x = 0; x < width; x += kTileSize) {
                    context.throwIfCancelled();
                    const int w = std::min(kTileSize, width - x);
                    if (!src.readBandWindow(b + 1, x, y, w, h, tile.data())) {
                        throw RSOperatorError(ErrorCode::GdalError,
                                              "failed reading band " + std::to_string(b + 1) +
                                                  " for statistics");
                    }
                    const size_t pixels = static_cast<size_t>(w) * h;
                    for (size_t p = 0; p < pixels; ++p) {
                        const float v = tile[p];
                        if (!isValid(b, v)) {
                            continue;
                        }
                        welford[b].add(v);
                        vmin[b] = std::min(vmin[b], static_cast<double>(v));
                        vmax[b] = std::max(vmax[b], static_cast<double>(v));
                    }
                }
            }
            context.reportProgress(0.05 + 0.35 * (b + 1) / bandCount,
                                   "Fitted statistics for band " + std::to_string(b + 1) +
                                       "/" + std::to_string(bandCount));
        }

        std::vector<double> robustLo(bandCount, 0.0);
        std::vector<double> robustHi(bandCount, 0.0);
        if (method == "robust") {
            // Pass 1b: 512-bin histogram per band over [vmin, vmax]; the robust
            // scaling range is the histogram midpoint of the 0.5 and 99.5
            // percentile bins (a simple outlier-trimmed approximation).
            std::vector<std::uint64_t> hist(static_cast<size_t>(bandCount) * kHistogramBins, 0);
            for (int b = 0; b < bandCount; ++b) {
                if (!(vmax[b] > vmin[b])) {
                    continue; // zero-range band: stays degenerate below
                }
                const double lo = vmin[b];
                const double span = vmax[b] - vmin[b];
                const size_t base = static_cast<size_t>(b) * kHistogramBins;
                for (int y = 0; y < height; y += kTileSize) {
                    const int h = std::min(kTileSize, height - y);
                    for (int x = 0; x < width; x += kTileSize) {
                        context.throwIfCancelled();
                        const int w = std::min(kTileSize, width - x);
                        if (!src.readBandWindow(b + 1, x, y, w, h, tile.data())) {
                            throw RSOperatorError(ErrorCode::GdalError,
                                                  "failed reading band " +
                                                      std::to_string(b + 1) +
                                                      " for the robust histogram");
                        }
                        const size_t pixels = static_cast<size_t>(w) * h;
                        for (size_t p = 0; p < pixels; ++p) {
                            const float v = tile[p];
                            if (!isValid(b, v)) {
                                continue;
                            }
                            const int bin = std::clamp(
                                static_cast<int>((v - lo) / span * kHistogramBins),
                                0, kHistogramBins - 1);
                            ++hist[base + bin];
                        }
                    }
                }
            }
            for (int b = 0; b < bandCount; ++b) {
                const size_t base = static_cast<size_t>(b) * kHistogramBins;
                const std::uint64_t total = std::accumulate(
                    hist.begin() + base, hist.begin() + base + kHistogramBins, std::uint64_t{0});
                if (total == 0 || !(vmax[b] > vmin[b])) {
                    continue;
                }
                const std::uint64_t lowTarget =
                    static_cast<std::uint64_t>(0.005 * static_cast<double>(total));
                const std::uint64_t highTarget =
                    static_cast<std::uint64_t>(0.995 * static_cast<double>(total));
                int lowBin = 0;
                int highBin = kHistogramBins - 1;
                std::uint64_t cum = 0;
                for (int k = 0; k < kHistogramBins; ++k) {
                    cum += hist[base + k];
                    if (cum >= lowTarget) {
                        lowBin = k;
                        break;
                    }
                }
                cum = 0;
                for (int k = 0; k < kHistogramBins; ++k) {
                    cum += hist[base + k];
                    if (cum >= highTarget) {
                        highBin = k;
                        break;
                    }
                }
                const double binWidth = (vmax[b] - vmin[b]) / kHistogramBins;
                robustLo[b] = vmin[b] + (lowBin + 0.5) * binWidth;
                robustHi[b] = vmin[b] + (highBin + 0.5) * binWidth;
            }
        }

        for (int b = 0; b < bandCount; ++b) {
            if (method == "zscore") {
                stats[b].a = welford[b].mean;
                stats[b].b = welford[b].stddev();
            } else if (method == "minmax") {
                stats[b].a = vmin[b];
                stats[b].b = vmax[b];
            } else { // robust
                stats[b].a = robustLo[b];
                stats[b].b = robustHi[b];
            }
            // Bands without a single valid pixel (minmax: ±inf) get neutral
            // stats so the contract stays JSON-safe; they are degenerate below.
            if (!std::isfinite(stats[b].a) || !std::isfinite(stats[b].b)) {
                stats[b].a = 0.0;
                stats[b].b = 0.0;
            }
            Json::Value row(Json::arrayValue);
            row.append(stats[b].a);
            row.append(stats[b].b);
            normParams.append(row);
        }
    }

    // Zero-variance / zero-range bands are skipped in BOTH directions: the
    // forward pass left them unchanged, so the inverse must too.
    for (int b = 0; b < bandCount; ++b) {
        stats[b].degenerate = methodUsed == "zscore" ? stats[b].b <= 0.0
                                                     : !(stats[b].b > stats[b].a);
        if (stats[b].degenerate) {
            context.logWarning("Band " + std::to_string(b + 1) + " (" +
                               contract.bands[b].id.toStdString() +
                               ") has zero variance/range; it is copied unchanged");
        }
    }

    // --- Persist the stats INTO the contract --------------------------------
    Json::Value normalization(Json::objectValue);
    normalization["method"] = methodUsed;
    normalization["params"] = normParams;
    normalization["applied"] = !inverse;
    contract.normalization = normalization;

    // --- Create the output cube ---------------------------------------------
    context.reportProgressForced(0.45, inverse ? "Inverting normalization"
                                               : "Applying normalization");
    GdalDatasetWrapper out;
    QString outErr;
    if (!out.create(QString::fromStdString(outputPath), width, height, bandCount,
                    static_cast<int>(GDT_Float32), src.geoTransform(), src.projection(),
                    &outErr)) {
        throw RSOperatorError(ErrorCode::FileNotWritable,
                              "failed to create output: " + outErr.toStdString());
    }
    for (int b = 0; b < bandCount; ++b) {
        out.setBandNoDataValue(b + 1, hasNodata[b] ? nodataRaw[b]
                                                   : std::numeric_limits<double>::quiet_NaN());
    }

    // --- Pass 2: apply (or invert) per band ----------------------------------
    // zscore: (v-mean)/std  |  inverse v*std+mean
    // minmax/robust: (v-lo)/(hi-lo)  |  inverse v*(hi-lo)+lo
    auto transform = [&](const BandStats& s, float v) {
        if (methodUsed == "zscore") {
            return inverse ? static_cast<float>(v * s.b + s.a)
                           : static_cast<float>((v - s.a) / s.b);
        }
        return inverse ? static_cast<float>(v * (s.b - s.a) + s.a)
                       : static_cast<float>((v - s.a) / (s.b - s.a));
    };
    std::vector<float> tile(static_cast<size_t>(kTileSize) * kTileSize);
    for (int b = 0; b < bandCount; ++b) {
        context.throwIfCancelled();
        const BandStats& s = stats[b];
        for (int y = 0; y < height; y += kTileSize) {
            const int h = std::min(kTileSize, height - y);
            for (int x = 0; x < width; x += kTileSize) {
                const int w = std::min(kTileSize, width - x);
                if (!src.readBandWindow(b + 1, x, y, w, h, tile.data())) {
                    out.close();
                    QFile::remove(QString::fromStdString(outputPath));
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "failed reading band " + std::to_string(b + 1) +
                                              " for normalization");
                }
                if (!s.degenerate) {
                    const size_t pixels = static_cast<size_t>(w) * h;
                    // Invalid pixels (declared sentinel / non-finite) are left
                    // unchanged so masking semantics survive.
                    for (size_t p = 0; p < pixels; ++p) {
                        const float v = tile[p];
                        if (isValid(b, v)) {
                            tile[p] = transform(s, v);
                        }
                    }
                }
                if (!out.writeBandWindow(b + 1, x, y, w, h, tile.data())) {
                    out.close();
                    QFile::remove(QString::fromStdString(outputPath));
                    throw RSOperatorError(ErrorCode::GdalError,
                                          "failed writing normalized band " +
                                              std::to_string(b + 1));
                }
            }
        }
        context.reportProgress(0.45 + 0.5 * (b + 1) / bandCount,
                               "Normalized band " + std::to_string(b + 1) + "/" +
                                   std::to_string(bandCount));
    }

    // The updated contract (with the normalization section) must ride on the
    // open dataset before close; oversized contracts spill to the sidecar.
    const QString sidecar = QString::fromStdString(outputPath) + QStringLiteral(".features.json");
    if (!sicnu::features::writeFeatureCubeMetadata(out.dataset(), contract, sidecar)) {
        out.close();
        QFile::remove(QString::fromStdString(outputPath));
        QFile::remove(sidecar);
        throw RSOperatorError(ErrorCode::GdalError,
                              "failed to write the feature cube contract onto " + outputPath);
    }

    QString closeErr;
    if (!out.closeWithError(&closeErr)) {
        QFile::remove(QString::fromStdString(outputPath));
        throw RSOperatorError(ErrorCode::GdalError,
                              "output flush failed (disk full?): " + closeErr.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = inverse ? "inverse_" + methodUsed : methodUsed;
    result["bands"] = bandCount;
    result["stats"] = normParams;
    context.reportProgress(1.0, "Feature normalization complete");
    return result;
}

} // namespace sicnu::operators::rs
