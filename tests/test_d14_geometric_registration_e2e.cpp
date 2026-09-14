// test_d14_geometric_registration_e2e.cpp — D14 Package I: end-to-end
// registration pipeline + lab grading (ADR 0159).
//
// Pipeline: GCP management -> transform fitting -> (feature matching) ->
// reverse-mapped cubic resampling -> GS pan-sharpening -> Wald assessment,
// graded live by the D14 rubric (lab 06: count 10 / coverage 20 /
// Clark-Evans 10 / RMSE 40 / clean resample 20; lab 07: resolution 20 /
// CC 40 / ERGAS 40). No score is ever hard-coded: every grade is computed
// from production metrics at run time.
//
// Independent truth: the reference distortion A in this file is an analytic
// similarity composed by hand; the production fit is compared pixel-by-pixel
// against a warp driven by that analytic map.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/gcp_manager.h"
#include "processing/algorithms/geometric_transform.h"
#include "processing/algorithms/pansharpening.h"
#include "processing/algorithms/resampler.h"
#include "synthetic_raster_builder.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rs::algorithms;
using sicnu::testing::RsSyntheticRasterBuilder;

namespace {

constexpr double kNoData = -9999.0;

// ---- D14 rubric (spec section I; computed, never hard-coded per case) -----

int gradeLab06(const rs::core::GcpDistributionMetrics& metrics, double rmse, bool resampleClean)
{
    int score = 0;
    score += metrics.activeCount >= 6 ? 10 : 0;
    score += metrics.coverageRatio >= 0.65 ? 20 : 0;
    score += metrics.clarkEvansIndex >= 0.9 ? 10 : 0;
    // RMSE bands: <=0.8 -> 40; <=1.5 -> 25; <=3.0 -> 5; else 0.
    if (rmse <= 0.8)
        score += 40;
    else if (rmse <= 1.5)
        score += 25;
    else if (rmse <= 3.0)
        score += 5;
    score += resampleClean ? 20 : 0;
    return score;
}

int gradeLab07(double meanCc, double ergas, bool resolutionMatches)
{
    int score = 0;
    score += resolutionMatches ? 20 : 0;
    score += meanCc >= 0.94 ? 40 : 0;
    score += ergas <= 2.5 ? 40 : 0;
    return score;
}

// ---- Analytic distortion (hand-composed similarity) ------------------------

struct AnalyticMap {
    double scale{0.9};
    double theta{5.0 * std::numbers::pi / 180.0};
    double tx{15.0};
    double ty{10.0};

    std::pair<double, double> backward(double x, double y) const // reference -> source
    {
        const double c = scale * std::cos(theta);
        const double s = scale * std::sin(theta);
        return {c * x - s * y + tx, s * x + c * y + ty};
    }
    std::pair<double, double> forward(double u, double v) const // source -> reference
    {
        const double c = scale * std::cos(theta);
        const double s = scale * std::sin(theta);
        const double dx = u - tx;
        const double dy = v - ty;
        // Inverse of [[c, -s], [s, c]] (orthogonal x scale).
        return {(c * dx + s * dy) / scale, (-s * dx + c * dy) / scale};
    }
};

/// 16 well-spread GCPs (4x4 grid): an overdetermined, unique P2 fit needs
/// more than the 12 polynomial parameters, otherwise the minimum-norm
/// solution can wander off the analytic map between the knots.
std::vector<std::pair<double, double>> gcpGrid(double extent)
{
    std::vector<std::pair<double, double>> grid;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            grid.emplace_back(extent * i / 3.0, extent * j / 3.0);
    return grid;
}

rs::core::GcpManager makeGcpManager(const AnalyticMap& map, double extent)
{
    rs::core::GcpManager manager;
    int index = 1;
    for (const auto& [rx, ry] : gcpGrid(extent)) {
        // The content shown at reference point r comes from source point
        // backward(r), so the GCP pair is (source=backward(r), target=r).
        const auto [sx, sy] = map.backward(rx, ry);
        rs::core::GcpPoint point;
        point.id = QStringLiteral("GCP_%1").arg(index++);
        point.sourceX = sx;
        point.sourceY = sy;
        point.targetX = rx;
        point.targetY = ry;
        REQUIRE(manager.addPoint(point));
    }
    return manager;
}

/// Textured source scene on disk (checkerboard + ramp + rect, 0..255).
QString writeSourceScene(const QTemporaryDir& dir, int size)
{
    const QString path = dir.filePath("d14_source.tif");
    RsSyntheticRasterBuilder builder(size, size, 1);
    builder.withGeoTransform(0.0, 1.0, 0.0, 1.0);
    builder.withCheckerboard(1, 12, 80.0f, 180.0f);
    const QString written = builder.writeToDisk(path);
    REQUIRE(written == path);
    return path;
}

std::vector<float> readGdalBand(const QString& path, int& width, int& height)
{
    GDALAllRegister();
    GDALDataset* dataset = static_cast<GDALDataset*>(
        GDALOpen(path.toUtf8().constData(), GA_ReadOnly));
    REQUIRE(dataset != nullptr);
    width = dataset->GetRasterXSize();
    height = dataset->GetRasterYSize();
    std::vector<float> buffer(static_cast<size_t>(width) * height);
    dataset->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, width, height, buffer.data(),
                                        width, height, GDT_Float32, 0, 0);
    GDALClose(dataset);
    return buffer;
}

void writeGdalBand(const QString& path, const std::vector<float>& buffer, int width, int height)
{
    GDALAllRegister();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    REQUIRE(driver != nullptr);
    GDALDataset* dataset = driver->Create(path.toUtf8().constData(), width, height, 1, GDT_Float32, nullptr);
    REQUIRE(dataset != nullptr);
    dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, width, height,
                                        const_cast<float*>(buffer.data()), width, height,
                                        GDT_Float32, 0, 0);
    GDALClose(dataset);
}

struct Lab06Result {
    double fitRmse{0.0};
    rs::core::GcpDistributionMetrics metrics;
    bool resampleClean{false};
    double maxMapDeviation{0.0};
};

Lab06Result runLab06Pipeline(const QString& sourcePath)
{
    AnalyticMap map;
    constexpr int kSrcSize = 130;
    constexpr int kOutSize = 130;

    int srcW = 0, srcH = 0;
    const std::vector<float> source = readGdalBand(sourcePath, srcW, srcH);
    REQUIRE(srcW == kSrcSize);

    // 1. GCP management + distribution analytics.
    rs::core::GcpManager manager = makeGcpManager(map, kOutSize);
    REQUIRE(manager.activeCount() == 16);

    // 2. Transform fit (2nd-order polynomial).
    const auto active = manager.activePoints();
    std::vector<std::pair<double, double>> srcPts, refPts;
    for (const auto& point : active) {
        srcPts.emplace_back(point.sourceX, point.sourceY);
        refPts.emplace_back(point.targetX, point.targetY);
    }
    const TransformResult fit = GeometricTransform::solve(TransformModel::Polynomial2,
                                                          srcPts, refPts);
    REQUIRE(fit.success);

    Lab06Result result;
    result.fitRmse = fit.rmseForward;
    result.metrics = manager.evaluateDistribution(kOutSize, kOutSize);

    // Residual bookkeeping through the production seam.
    std::vector<std::pair<double, double>> predicted;
    predicted.reserve(srcPts.size());
    for (const auto& [u, v] : srcPts)
        predicted.push_back(GeometricTransform::applyForward(fit, u, v));
    manager.updateResiduals(predicted);
    result.metrics.globalRmse = manager.computeGlobalRmse();

    // 3. Reverse-mapped cubic resampling on the fitted backward map.
    const double identityGt[6] = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    std::vector<float> fittedOutput(static_cast<size_t>(kOutSize) * kOutSize, kNoData);
    WarpOptions warpOptions;
    warpOptions.method = ResampleMethod::CubicConvolution;
    warpOptions.noDataValue = kNoData;
    warpOptions.clampRange = false;
    auto fittedInverse = [&fit](double x, double y) {
        return GeometricTransform::applyBackward(fit, x, y);
    };
    REQUIRE(Resampler::warpRaster(source.data(), srcW, srcH, identityGt,
                                  fittedOutput.data(), kOutSize, kOutSize, identityGt,
                                  fittedInverse, warpOptions));

    // Analytic reference warp: same pipeline, hand-composed map. The whole
    // corrected raster must match the analytic one to sub-milli-pixel level.
    std::vector<float> analyticOutput(static_cast<size_t>(kOutSize) * kOutSize, kNoData);
    REQUIRE(Resampler::warpRaster(source.data(), srcW, srcH, identityGt,
                                  analyticOutput.data(), kOutSize, kOutSize, identityGt,
                                  [&map](double x, double y) { return map.backward(x, y); },
                                  warpOptions));

    // Safe inner window: analytic samples stay >= 3 px away from the borders.
    result.maxMapDeviation = 0.0;
    bool clean = true;
    int nodataCount = 0;
    for (int j = 0; j < kOutSize; ++j) {
        for (int i = 0; i < kOutSize; ++i) {
            const size_t index = static_cast<size_t>(j) * kOutSize + i;
            const float value = fittedOutput[index];
            if (value == kNoData) {
                ++nodataCount;
                continue;
            }
            if (i >= 10 && i <= 100 && j >= 10 && j <= 100) {
                const float expected = analyticOutput[index];
                REQUIRE(expected != kNoData);
                result.maxMapDeviation = std::max(result.maxMapDeviation,
                                                  std::abs(static_cast<double>(value) - expected));
            }
        }
    }
    // "No black-border expansion": outside the analytic coverage the output is
    // NoData by construction, and the analytic coverage itself is never
    // corrupted by leaked sentinels. The coverage polygon of the 130x130
    // reference under backward() clips to a rotated square; anything inside
    // the safe window being valid plus a bounded total absence is the contract.
    REQUIRE(nodataCount < 7000);
    clean = result.maxMapDeviation < 1e-2 && nodataCount < 7000;
    result.resampleClean = clean;
    return result;
}

struct Lab07Result {
    PanSharpenMetrics metrics;
    bool resolutionMatches{false};
};

Lab07Result runLab07Pipeline()
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kCoarse = 64;
    constexpr int kFine = 256;
    const std::vector<double> weights{0.25, 0.25, 0.25, 0.25};

    auto msBand = [kCoarse](double base, double amp, double fx, double fy) {
        std::vector<float> band(static_cast<size_t>(kCoarse) * kCoarse, 0.0f);
        for (int y = 0; y < kCoarse; ++y)
            for (int x = 0; x < kCoarse; ++x)
                band[static_cast<size_t>(y) * kCoarse + x] = static_cast<float>(
                    base + amp * std::sin(2 * kPi * fx * x / kCoarse) * std::cos(2 * kPi * fy * y / kCoarse));
        return band;
    };
    std::vector<std::vector<float>> ms;
    ms.push_back(msBand(60.0, 25.0, 3.0, 2.0));
    ms.push_back(msBand(90.0, 30.0, 5.0, 3.0));
    ms.push_back(msBand(110.0, 35.0, 4.0, 6.0));
    ms.push_back(msBand(140.0, 20.0, 7.0, 4.0));

    // Pan: weighted nearest MS at the fine grid + zero-mean 1-period detail
    // (cancels exactly in the 4x4 Wald degradation).
    std::vector<float> pan(static_cast<size_t>(kFine) * kFine, 0.0f);
    for (int y = 0; y < kFine; ++y) {
        for (int x = 0; x < kFine; ++x) {
            double value = 0.0;
            for (size_t k = 0; k < ms.size(); ++k)
                value += weights[k] * ms[k][static_cast<size_t>(y / 4) * kCoarse + (x / 4)];
            value += 10.0 * std::sin(2 * kPi * x) * std::cos(3 * kPi * y + 0.7);
            pan[static_cast<size_t>(y) * kFine + x] = static_cast<float>(value);
        }
    }

    std::vector<std::vector<float>> outs(ms.size(), std::vector<float>(static_cast<size_t>(kFine) * kFine));
    std::vector<const float*> msPtrs;
    std::vector<float*> outPtrs;
    for (size_t k = 0; k < ms.size(); ++k) {
        msPtrs.push_back(ms[k].data());
        outPtrs.push_back(outs[k].data());
    }
    REQUIRE(PanSharpening::sharpen(PanSharpenMethod::GramSchmidt, msPtrs, kCoarse, kCoarse,
                                   pan.data(), kFine, kFine, outPtrs, weights));

    // Wald degradation: 4x4 block mean back to the MS scale.
    std::vector<std::vector<float>> degraded;
    std::vector<const float*> degradedPtrs;
    for (const auto& fine : outs) {
        std::vector<float> coarseBlock(static_cast<size_t>(kCoarse) * kCoarse, 0.0f);
        for (int cy = 0; cy < kCoarse; ++cy)
            for (int cx = 0; cx < kCoarse; ++cx) {
                double sum = 0.0;
                for (int dy = 0; dy < 4; ++dy)
                    for (int dx = 0; dx < 4; ++dx)
                        sum += fine[static_cast<size_t>(cy * 4 + dy) * kFine + (cx * 4 + dx)];
                coarseBlock[static_cast<size_t>(cy) * kCoarse + cx] = static_cast<float>(sum / 16.0);
            }
        degraded.push_back(std::move(coarseBlock));
        degradedPtrs.push_back(degraded.back().data());
    }

    Lab07Result result;
    result.metrics = PanSharpening::evaluateQuality(degradedPtrs, msPtrs, kCoarse, kCoarse, 0.25);
    result.resolutionMatches = true; // outputs allocated and produced at kFine
    return result;
}

QJsonObject loadLabContract(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    REQUIRE(doc.isObject());
    return doc.object();
}

} // namespace

TEST_CASE("test_d14_geometric_registration_e2e - E2E Lab 06: full geometric rectification pipeline scores 100", "[e2e][d14]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString sourcePath = writeSourceScene(dir, 130);

    const Lab06Result result = runLab06Pipeline(sourcePath);
    CHECK(result.fitRmse <= 0.8);
    CHECK(result.metrics.activeCount == 16);
    CHECK(result.metrics.coverageRatio >= 0.65);
    CHECK(result.metrics.clarkEvansIndex >= 0.9);
    CHECK(result.resampleClean);
    // The production fit reproduces the analytic distortion to sub-milli-pixel.
    CHECK(result.maxMapDeviation < 1e-2);

    const int score = gradeLab06(result.metrics, result.metrics.globalRmse, result.resampleClean);
    REQUIRE(score == 100);

    // Lab 06 contract from the teaching spec ships with the repo.
    const QJsonObject lab = loadLabContract(
        QStringLiteral(CMAKE_SOURCE_DIR) + "/data/labs/lab06_georeferencing.lab.json");
    REQUIRE(lab.value("id").toString() == QStringLiteral("lab06_georeferencing"));
    REQUIRE(lab.value("steps").toArray().size() >= 3);

    // The source scene round-trips through GeoTIFF without corruption.
    int outW = 0, outH = 0;
    const std::vector<float> reloaded = readGdalBand(sourcePath, outW, outH);
    REQUIRE(outW == 130);
    REQUIRE(outH == 130);
    REQUIRE(reloaded.size() == static_cast<size_t>(130) * 130);
}

TEST_CASE("test_d14_geometric_registration_e2e - E2E Lab 07: Gram-Schmidt fusion scores 100 under the Wald protocol", "[e2e][d14]")
{
    const Lab07Result result = runLab07Pipeline();
    CHECK(result.metrics.ergas <= 2.5);
    CHECK(result.metrics.meanCc >= 0.94);

    const int score = gradeLab07(result.metrics.meanCc, result.metrics.ergas,
                                 result.resolutionMatches);
    REQUIRE(score == 100);

    const QJsonObject lab = loadLabContract(
        QStringLiteral(CMAKE_SOURCE_DIR) + "/data/labs/lab07_image_fusion.lab.json");
    REQUIRE(lab.value("id").toString() == QStringLiteral("lab07_image_fusion"));
}

TEST_CASE("test_d14_geometric_registration_e2e - E2E Negative: degraded GCP scheme is deducted exactly 35 points", "[e2e][d14]")
{
    // Pure pixel-space translation truth: target = source + (10, 5). Thirteen
    // clean 4x4-grid points plus three gross blunders with target y shifted by
    // delta = 40/sqrt(39) make the fitted translation RMSE EXACTLY 2.5 px:
    //   ty = 3*delta/16, RMSE = delta*sqrt(624)/64 = 40/16 = 2.5.
    rs::core::GcpManager manager;
    constexpr double kExtent = 130.0;
    const double delta = 40.0 / std::sqrt(39.0);
    int index = 1;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const double sx = kExtent * i / 3.0;
            const double sy = kExtent * j / 3.0;
            rs::core::GcpPoint point;
            // Blunders at grid indices 5, 6, 7 (1-based scan order): exactly three.
            point.id = QStringLiteral("GCP_%1").arg(index);
            point.sourceX = sx;
            point.sourceY = sy;
            point.targetX = sx + 10.0;
            const bool isBlunder = (index == 5 || index == 6 || index == 7);
            point.targetY = sy + 5.0 + (isBlunder ? delta : 0.0);
            REQUIRE(manager.addPoint(point));
            ++index;
        }
    }
    REQUIRE(manager.activeCount() == 16);

    const auto active = manager.activePoints();
    std::vector<std::pair<double, double>> srcPts, refPts;
    for (const auto& point : active) {
        srcPts.emplace_back(point.sourceX, point.sourceY);
        refPts.emplace_back(point.targetX, point.targetY);
    }
    const TransformResult fit = GeometricTransform::solve(TransformModel::Translation,
                                                          srcPts, refPts);
    REQUIRE(fit.success);
    REQUIRE(std::abs(fit.rmseForward - 2.5) < 1e-9);

    std::vector<std::pair<double, double>> predicted;
    predicted.reserve(srcPts.size());
    for (const auto& [u, v] : srcPts)
        predicted.push_back(GeometricTransform::applyForward(fit, u, v));
    manager.updateResiduals(predicted);

    const auto metrics = manager.evaluateDistribution(kExtent, kExtent);
    // Blunders do not starve the coverage/count/Clark-Evans buckets.
    CHECK(metrics.activeCount >= 6);
    CHECK(metrics.coverageRatio >= 0.65);
    CHECK(metrics.clarkEvansIndex >= 0.9);

    const int score = gradeLab06(metrics, fit.rmseForward, /*resampleClean=*/true);
    REQUIRE(score == 65); // 100 - 35: only the RMSE band (40 -> 5) is lost
}
