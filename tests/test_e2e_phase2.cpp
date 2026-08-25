// tests/test_e2e_phase2.cpp — 4-Tier E2E Opaque-Box Test Suite for Phase 2 Remote Sensing Core Algorithms
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "synthetic_raster_builder.h"

#include "processing/algorithms/band_math.h"
#include "processing/algorithms/change_detection.h"
#include "analysis/segmentation/rs_segment_map.h"
#include "analysis/segmentation/rs_segment_features.h"
#include "analysis/segmentation/rs_simple_segmenter.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/gdal/gdal_operator_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "jobs/job_engine.h"
#include "workflow/workflow_runtime.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <future>
#include <limits>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::testing;
using namespace sicnu::operators;
using namespace sicnu::jobs;
using namespace sicnu::workflow;
using Catch::Approx;

namespace {

static bool g_initGDAL = (GDALAllRegister(), true);

} // namespace

// ============================================================================
// TIER 1: FEATURE COVERAGE (HAPPY PATHS)
// ============================================================================

// ----------------------------------------------------------------------------
// Feature 1.1: Band Math AST & Evaluation
// ----------------------------------------------------------------------------

TEST_CASE("Tier 1 - Band Math: Basic arithmetic evaluation", "[e2e][tier1][bandmath]") {
    constexpr size_t N = 4;
    BandMath::BandData bands;
    bands[1] = {10.0f, 20.0f, 30.0f, 40.0f};
    bands[2] = {2.0f,  4.0f,  5.0f,  8.0f};

    std::vector<float> out(N);

    // Addition
    REQUIRE(BandMath::evaluate("b1 + b2", bands, out.data(), N));
    CHECK(out[0] == Approx(12.0f));
    CHECK(out[1] == Approx(24.0f));
    CHECK(out[2] == Approx(35.0f));
    CHECK(out[3] == Approx(48.0f));

    // Subtraction
    REQUIRE(BandMath::evaluate("b1 - b2", bands, out.data(), N));
    CHECK(out[0] == Approx(8.0f));
    CHECK(out[1] == Approx(16.0f));

    // Multiplication
    REQUIRE(BandMath::evaluate("b1 * b2", bands, out.data(), N));
    CHECK(out[0] == Approx(20.0f));
    CHECK(out[1] == Approx(80.0f));

    // Division
    REQUIRE(BandMath::evaluate("b1 / b2", bands, out.data(), N));
    CHECK(out[0] == Approx(5.0f));
    CHECK(out[1] == Approx(5.0f));
    CHECK(out[2] == Approx(6.0f));
    CHECK(out[3] == Approx(5.0f));
}

TEST_CASE("Tier 1 - Band Math: Advanced mathematical and trigonometric functions", "[e2e][tier1][bandmath]") {
    constexpr size_t N = 3;
    BandMath::BandData bands;
    bands[1] = {0.0f, 4.0f, 9.0f};
    bands[2] = {1.0f, 2.0f, 3.0f};

    std::vector<float> out(N);

    // sqrt
    REQUIRE(BandMath::evaluate("sqrt(b1)", bands, out.data(), N));
    CHECK(out[0] == Approx(0.0f));
    CHECK(out[1] == Approx(2.0f));
    CHECK(out[2] == Approx(3.0f));

    // pow
    REQUIRE(BandMath::evaluate("pow(b2, 3)", bands, out.data(), N));
    CHECK(out[0] == Approx(1.0f));
    CHECK(out[1] == Approx(8.0f));
    CHECK(out[2] == Approx(27.0f));

    // min & max
    REQUIRE(BandMath::evaluate("min(b1, b2)", bands, out.data(), N));
    CHECK(out[0] == Approx(0.0f));
    CHECK(out[1] == Approx(2.0f));
    CHECK(out[2] == Approx(3.0f));

    REQUIRE(BandMath::evaluate("max(b1, b2)", bands, out.data(), N));
    CHECK(out[0] == Approx(1.0f));
    CHECK(out[1] == Approx(4.0f));
    CHECK(out[2] == Approx(9.0f));

    // exp and ln
    bands[3] = {1.0f, static_cast<float>(M_E), static_cast<float>(M_E * M_E)};
    REQUIRE(BandMath::evaluate("ln(b3)", bands, out.data(), N));
    CHECK(out[0] == Approx(0.0f).margin(1e-4f));
    CHECK(out[1] == Approx(1.0f).margin(1e-4f));
    CHECK(out[2] == Approx(2.0f).margin(1e-4f));
}

TEST_CASE("Tier 1 - Band Math: Comparison and logical operators", "[e2e][tier1][bandmath]") {
    constexpr size_t N = 4;
    BandMath::BandData bands;
    bands[1] = {0.2f, 0.5f, 0.8f, 0.5f};
    bands[2] = {0.3f, 0.5f, 0.4f, 0.6f};

    std::vector<float> out(N);

    // Greater than
    REQUIRE(BandMath::evaluate("b1 > b2", bands, out.data(), N));
    CHECK(out[0] == Approx(0.0f));
    CHECK(out[1] == Approx(0.0f));
    CHECK(out[2] == Approx(1.0f));
    CHECK(out[3] == Approx(0.0f));

    // Equal to
    REQUIRE(BandMath::evaluate("b1 == b2", bands, out.data(), N));
    CHECK(out[0] == Approx(0.0f));
    CHECK(out[1] == Approx(1.0f));
    CHECK(out[2] == Approx(0.0f));

    // Logical AND
    REQUIRE(BandMath::evaluate("b1 >= 0.5 && b2 <= 0.5", bands, out.data(), N));
    CHECK(out[0] == Approx(0.0f)); // 0.2>=0.5 (F)
    CHECK(out[1] == Approx(1.0f)); // 0.5>=0.5 && 0.5<=0.5 (T)
    CHECK(out[2] == Approx(1.0f)); // 0.8>=0.5 && 0.4<=0.5 (T)
    CHECK(out[3] == Approx(0.0f)); // 0.5>=0.5 && 0.6<=0.5 (F)

    // Logical OR
    REQUIRE(BandMath::evaluate("b1 > 0.7 || b2 > 0.55", bands, out.data(), N));
    CHECK(out[0] == Approx(0.0f));
    CHECK(out[1] == Approx(0.0f));
    CHECK(out[2] == Approx(1.0f)); // b1 = 0.8 > 0.7 (T)
    CHECK(out[3] == Approx(1.0f)); // b2 = 0.6 > 0.55 (T)
}

TEST_CASE("Tier 1 - Band Math: Ternary conditional expressions", "[e2e][tier1][bandmath]") {
    constexpr size_t N = 4;
    BandMath::BandData bands;
    bands[1] = {0.1f, 0.4f, 0.6f, 0.9f};
    bands[2] = {10.0f, 20.0f, 30.0f, 40.0f};

    std::vector<float> out(N);

    // Simple ternary: b1 > 0.5 ? b2 : 0
    REQUIRE(BandMath::evaluate("b1 > 0.5 ? b2 : 0.0", bands, out.data(), N));
    CHECK(out[0] == Approx(0.0f));
    CHECK(out[1] == Approx(0.0f));
    CHECK(out[2] == Approx(30.0f));
    CHECK(out[3] == Approx(40.0f));

    // Nested ternary: b1 < 0.3 ? 1.0 : (b1 < 0.7 ? 2.0 : 3.0)
    REQUIRE(BandMath::evaluate("b1 < 0.3 ? 1.0 : (b1 < 0.7 ? 2.0 : 3.0)", bands, out.data(), N));
    CHECK(out[0] == Approx(1.0f));
    CHECK(out[1] == Approx(2.0f));
    CHECK(out[2] == Approx(2.0f));
    CHECK(out[3] == Approx(3.0f));
}

TEST_CASE("Tier 1 - Band Math: File-level out-of-core evaluation", "[e2e][tier1][bandmath]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString srcPath = tmp.path() + "/src_multiband.tif";
    const QString dstPath = tmp.path() + "/dst_ndvi.tif";

    // Create 4-band 32x32 raster
    RsSyntheticRasterBuilder builder(32, 32, 4);
    builder.withConstantValue(3, 0.10f); // Red (Band 3)
    builder.withConstantValue(4, 0.60f); // NIR (Band 4)
    REQUIRE(!builder.writeToDisk(srcPath).isEmpty());

    QString err;
    // Compute NDVI: (b4 - b3) / (b4 + b3) = (0.6 - 0.1) / (0.6 + 0.1) = 0.5 / 0.7 ≈ 0.7142857
    const bool ok = BandMath::processFile(srcPath, dstPath, "(b4 - b3) / (b4 + b3)", &err);
    REQUIRE(ok);
    REQUIRE(err.isEmpty());
    REQUIRE(QFile::exists(dstPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(dstPath));
    CHECK(ds.width() == 32);
    CHECK(ds.height() == 32);
    CHECK(ds.bandCount() == 1);

    std::vector<float> data(32 * 32);
    REQUIRE(ds.readBandData(1, data.data(), 32, 32));
    for (float v : data) {
        CHECK(v == Approx(0.5f / 0.7f).margin(1e-4f));
    }
}

// ----------------------------------------------------------------------------
// Feature 1.2: OBIA Segmentation Framework
// ----------------------------------------------------------------------------

TEST_CASE("Tier 1 - OBIA: Built-in segmentation happy path", "[e2e][tier1][obia]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString rasterPath = tmp.path() + "/obia_test.tif";
    RsSyntheticRasterBuilder builder(32, 32, 1);
    builder.withCheckerboard(1, 8, 20.0f, 180.0f);
    REQUIRE(!builder.writeToDisk(rasterPath).isEmpty());

    const auto &band1 = builder.band(1);
    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 8;
    params.minRegionSize = 10;

    RsSegmentMap segMap = RsSimpleSegmenter::segment(band1.data(), 32, 32, -9999.0f, params);
    REQUIRE(!segMap.isEmpty());
    CHECK(segMap.width() == 32);
    CHECK(segMap.height() == 32);
    CHECK(segMap.segmentCount() >= 4); // Checkerboard of 4x4 squares (16 squares) merged into distinct regions

    auto labels = segMap.uniqueLabels();
    CHECK(!labels.isEmpty());
    for (quint32 lbl : labels) {
        CHECK(lbl > 0);
        CHECK(segMap.pixelCount(lbl) >= 10);
    }
}

TEST_CASE("Tier 1 - OBIA: Multi-band segmentation with intensity reduction", "[e2e][tier1][obia]") {
    constexpr int W = 24, H = 24;
    RsSyntheticRasterBuilder builder(W, H, 3);
    builder.withRect(1, 0, 0, 12, 24, 10.0f);
    builder.withRect(1, 12, 0, 24, 24, 100.0f);
    builder.withRect(2, 0, 0, 12, 24, 20.0f);
    builder.withRect(2, 12, 0, 24, 24, 200.0f);
    builder.withRect(3, 0, 0, 12, 24, 30.0f);
    builder.withRect(3, 12, 0, 24, 24, 300.0f);

    const auto &vecs = builder.toVectors();
    const float* bands[3] = {vecs[0].data(), vecs[1].data(), vecs[2].data()};

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    // 2 bins: coarse quantization folds the boundary smoothing band into the
    // darker half, leaving exactly the two ecological zones.
    params.quantizeBins = 2;
    params.minRegionSize = 20;

    RsSegmentMap segMap = RsSimpleSegmenter::segmentMultiBand(bands, 3, W, H, -9999.0f, params);
    REQUIRE(!segMap.isEmpty());
    CHECK(segMap.segmentCount() == 2); // Split into left half and right half

    // Verify left and right pixels have distinct labels
    quint32 leftLabel = segMap.labelAt(5, 5);
    quint32 rightLabel = segMap.labelAt(5, 18);
    CHECK(leftLabel != 0);
    CHECK(rightLabel != 0);
    CHECK(leftLabel != rightLabel);
}

TEST_CASE("Tier 1 - OBIA: Segment feature extraction (spectral & geometric)", "[e2e][tier1][obia]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString rasterPath = tmp.path() + "/features_input.tif";
    constexpr int W = 16, H = 16;
    RsSyntheticRasterBuilder builder(W, H, 2);
    builder.withRect(1, 0, 0, 8, 16, 50.0f);
    builder.withRect(1, 8, 0, 16, 16, 150.0f);
    builder.withRect(2, 0, 0, 8, 16, 20.0f);
    builder.withRect(2, 8, 0, 16, 16, 80.0f);
    REQUIRE(!builder.writeToDisk(rasterPath).isEmpty());

    // Create 2-segment label map
    QVector<quint32> labels(W * H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            labels[y * W + x] = (x < 8) ? 1 : 2;
        }
    }
    RsSegmentMap segMap(labels, W, H);
    REQUIRE(segMap.segmentCount() == 2);

    auto stats = RsSegmentFeatures::extract(rasterPath, segMap, {1, 2});
    REQUIRE(stats.contains(1));
    REQUIRE(stats.contains(2));

    // Check segment 1 stats
    CHECK(stats[1].area == 128.0);
    CHECK(stats[1].mean[0] == Approx(50.0).margin(1.0));
    CHECK(stats[1].mean[1] == Approx(20.0).margin(1.0));

    // Check segment 2 stats
    CHECK(stats[2].area == 128.0);
    CHECK(stats[2].mean[0] == Approx(150.0).margin(1.0));
    CHECK(stats[2].mean[1] == Approx(80.0).margin(1.0));
}

TEST_CASE("Tier 1 - OBIA: GLCM texture statistics extraction", "[e2e][tier1][obia]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString rasterPath = tmp.path() + "/glcm_input.tif";
    constexpr int W = 16, H = 16;
    RsSyntheticRasterBuilder builder(W, H, 1);
    builder.withCheckerboard(1, 2, 10.0f, 90.0f);
    REQUIRE(!builder.writeToDisk(rasterPath).isEmpty());

    QVector<quint32> labels(W * H, 1); // 1 single segment covering entire checkerboard
    RsSegmentMap segMap(labels, W, H);

    auto stats = RsSegmentFeatures::extract(rasterPath, segMap, {1});
    REQUIRE(stats.contains(1));

    // Checkerboard texture has high contrast and non-zero GLCM features
    CHECK(stats[1].glcmContrast.size() == 1);
    CHECK(stats[1].glcmContrast[0] > 0.0);
    CHECK(stats[1].glcmEnergy.size() == 1);
    CHECK(stats[1].glcmEnergy[0] > 0.0);
    CHECK(stats[1].glcmHomogeneity.size() == 1);
    CHECK(stats[1].glcmHomogeneity[0] > 0.0);
}

TEST_CASE("Tier 1 - OBIA: Segment map GeoTIFF export and roundtrip", "[e2e][tier1][obia]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString refPath = tmp.path() + "/ref.tif";
    const QString segPath = tmp.path() + "/export_seg.tif";

    constexpr int W = 10, H = 10;
    RsSyntheticRasterBuilder builder(W, H, 1);
    REQUIRE(!builder.writeToDisk(refPath).isEmpty());

    QVector<quint32> labels(W * H);
    for (int i = 0; i < W * H; ++i) {
        labels[i] = (i < 50) ? 1 : 2;
    }
    RsSegmentMap segMap(labels, W, H);

    QString err;
    REQUIRE(segMap.toGeoTIFF(segPath, refPath, &err));
    REQUIRE(QFile::exists(segPath));

    RsSegmentMap loadedMap = RsSegmentMap::fromGeoTIFF(segPath);
    REQUIRE(!loadedMap.isEmpty());
    CHECK(loadedMap.width() == W);
    CHECK(loadedMap.height() == H);
    CHECK(loadedMap.segmentCount() == 2);
    CHECK(loadedMap.labelAt(0, 0) == 1);
    CHECK(loadedMap.labelAt(9, 9) == 2);
}

// ----------------------------------------------------------------------------
// Feature 1.3: Multi-Temporal Change Detection Suite
// ----------------------------------------------------------------------------

TEST_CASE("Tier 1 - Change Detection: Absolute & Normalized Difference", "[e2e][tier1][changedet]") {
    std::vector<float> before = {100.0f, 200.0f, 50.0f,  0.0f};
    std::vector<float> after  = {150.0f, 200.0f, 25.0f,  0.0f};
    std::vector<float> diff(4), normDiff(4);

    REQUIRE(ChangeDetection::difference(before.data(), after.data(), diff.data(), 4));
    CHECK(diff[0] == Approx(50.0f));
    CHECK(diff[1] == Approx(0.0f));
    CHECK(diff[2] == Approx(25.0f));
    CHECK(diff[3] == Approx(0.0f));

    REQUIRE(ChangeDetection::normalizedDifference(before.data(), after.data(), normDiff.data(), 4));
    CHECK(normDiff[0] == Approx(50.0f / 250.0f)); // 0.2
    CHECK(normDiff[1] == Approx(0.0f));
    CHECK(normDiff[2] == Approx(-25.0f / 75.0f)); // -0.3333
    CHECK(std::isnan(normDiff[3]));               // 0/0 = NaN
}

TEST_CASE("Tier 1 - Change Detection: Ratio change kernel", "[e2e][tier1][changedet]") {
    std::vector<float> before = {10.0f, 20.0f, 50.0f, 0.0f};
    std::vector<float> after  = {30.0f, 20.0f, 25.0f, 10.0f};
    std::vector<float> ratioOut(4);

    REQUIRE(ChangeDetection::ratio(before.data(), after.data(), ratioOut.data(), 4));
    CHECK(ratioOut[0] == Approx(3.0f));
    CHECK(ratioOut[1] == Approx(1.0f));
    CHECK(ratioOut[2] == Approx(0.5f));
    CHECK(std::isnan(ratioOut[3])); // Division by zero before == 0
}

TEST_CASE("Tier 1 - Change Detection: Change Vector Analysis (CVA) Magnitude", "[e2e][tier1][changedet]") {
    constexpr size_t N = 3;
    const float b0[] = {10.0f, 5.0f, 20.0f};
    const float b1[] = {20.0f, 5.0f, 10.0f};
    const float a0[] = {13.0f, 5.0f, 20.0f}; // deltas: +3, 0, 0
    const float a1[] = {24.0f, 9.0f, 10.0f}; // deltas: +4, +4, 0

    const float* beforeBands[] = {b0, b1};
    const float* afterBands[]  = {a0, a1};
    std::vector<float> outMag(N);

    QString err;
    REQUIRE(ChangeDetection::cvaMagnitude(beforeBands, afterBands, 2, N, outMag.data(), &err));
    CHECK(outMag[0] == Approx(5.0f)); // sqrt(3^2 + 4^2) = 5.0
    CHECK(outMag[1] == Approx(4.0f)); // sqrt(0^2 + 4^2) = 4.0
    CHECK(outMag[2] == Approx(0.0f)); // sqrt(0^2 + 0^2) = 0.0
}

TEST_CASE("Tier 1 - Change Detection: Automated Otsu & Percentile Thresholding", "[e2e][tier1][changedet]") {
    // Generate bimodal distribution (low change group around 2.0, high change group around 20.0)
    std::vector<float> values;
    for (int i = 0; i < 50; ++i) {
        values.push_back(2.0f + 0.05f * i);
        values.push_back(20.0f + 0.05f * i);
    }

    float otsuT = 0.0f;
    REQUIRE(ChangeDetection::otsuThreshold(values.data(), values.size(), &otsuT));
    CHECK(otsuT > 4.5f);
    CHECK(otsuT < 19.5f);

    float p90 = 0.0f;
    REQUIRE(ChangeDetection::percentileThreshold(values.data(), values.size(), 90.0, &p90));
    CHECK(p90 > 18.0f);
}

TEST_CASE("Tier 1 - Change Detection: Post-classification change transition matrix operator", "[e2e][tier1][changedet]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString beforePath = tmp.path() + "/before_class.tif";
    const QString afterPath = tmp.path() + "/after_class.tif";
    const QString outPath = tmp.path() + "/post_change.tif";

    constexpr int W = 4, H = 4;
    // 0=Water, 1=Forest, 2=Urban
    // Class transitions: 4 pixels 0->0, 4 pixels 1->1, 4 pixels 1->2 (Deforestation to Urban), 4 pixels 2->2
    const std::vector<float> beforeClasses = {
        0, 0, 0, 0,
        1, 1, 1, 1,
        1, 1, 1, 1,
        2, 2, 2, 2
    };
    const std::vector<float> afterClasses = {
        0, 0, 0, 0,
        1, 1, 1, 1,
        2, 2, 2, 2, // Changed from 1 to 2
        2, 2, 2, 2
    };

    RsSyntheticRasterBuilder b1(W, H, 1);
    for (int i = 0; i < W * H; ++i) b1.withPixel(1, i % W, i / W, beforeClasses[i]);
    REQUIRE(!b1.writeToDisk(beforePath).isEmpty());

    RsSyntheticRasterBuilder b2(W, H, 1);
    for (int i = 0; i < W * H; ++i) b2.withPixel(1, i % W, i / W, afterClasses[i]);
    REQUIRE(!b2.writeToDisk(afterPath).isEmpty());

    auto op = RSOperatorRegistry::instance().create("rs:post_classification_change");
    REQUIRE(op != nullptr);

    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = outPath.toStdString();

    RSOperatorContext ctx;
    Json::Value result = op->run(params, ctx);

    REQUIRE(QFile::exists(outPath));
    CHECK(result["classCount"].asInt() == 3);
    CHECK(result["changedPixels"].asUInt64() == 4);
    CHECK(result["unchangedPixels"].asUInt64() == 12);
    CHECK(result["totalPixels"].asUInt64() == 16);
    CHECK(result["changedPercent"].asDouble() == Approx(25.0));

    // Check transition matrix [from][to]
    REQUIRE(result["transitionMatrix"].size() == 3);
    CHECK(result["transitionMatrix"][0][0].asUInt64() == 4); // 0 -> 0
    CHECK(result["transitionMatrix"][1][1].asUInt64() == 4); // 1 -> 1
    CHECK(result["transitionMatrix"][1][2].asUInt64() == 4); // 1 -> 2
    CHECK(result["transitionMatrix"][2][2].asUInt64() == 4); // 2 -> 2
}

// ----------------------------------------------------------------------------
// Feature 1.4: Concurrency & Subsystem Stability
// ----------------------------------------------------------------------------

TEST_CASE("Tier 1 - Subsystem: JobEngine concurrent worker dispatch", "[e2e][tier1][subsystem]") {
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers(4);

    std::atomic<int> completedCount{0};
    constexpr int NUM_JOBS = 8;

    for (int i = 0; i < NUM_JOBS; ++i) {
        JobRequest req;
        req.title = "concurrent_test_" + std::to_string(i);
        req.algorithmId = "callable:test";
        req.exclusive = false;

        engine.submit(req, [&completedCount](const JobRequest&, RSOperatorContext&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            completedCount++;
            return Json::Value();
        });
    }

    engine.waitUntilIdleForTests(5000);
    CHECK(completedCount == NUM_JOBS);
    engine.clearCompleted();
}

TEST_CASE("Tier 1 - Subsystem: JobEngine drain-then-exclusive barrier execution", "[e2e][tier1][subsystem]") {
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers(3);

    std::atomic<int> inFlight{0};
    std::atomic<int> maxConcurrentDuringExclusive{0};
    std::atomic<bool> exclusiveRan{false};

    // 1. Submit 3 reader jobs
    for (int i = 0; i < 3; ++i) {
        JobRequest req;
        req.title = "reader_before_" + std::to_string(i);
        req.algorithmId = "callable:reader";
        req.exclusive = false;

        engine.submit(req, [&inFlight](const JobRequest&, RSOperatorContext&) {
            inFlight++;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            inFlight--;
            return Json::Value();
        });
    }

    // 2. Submit 1 exclusive job
    {
        JobRequest req;
        req.title = "exclusive_barrier";
        req.algorithmId = "callable:exclusive";
        req.exclusive = true;

        engine.submit(req, [&inFlight, &maxConcurrentDuringExclusive, &exclusiveRan](const JobRequest&, RSOperatorContext&) {
            inFlight++;
            maxConcurrentDuringExclusive = inFlight.load();
            exclusiveRan = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            inFlight--;
            return Json::Value();
        });
    }

    // 3. Submit 3 trailing reader jobs
    for (int i = 0; i < 3; ++i) {
        JobRequest req;
        req.title = "reader_after_" + std::to_string(i);
        req.algorithmId = "callable:reader";
        req.exclusive = false;

        engine.submit(req, [&inFlight](const JobRequest&, RSOperatorContext&) {
            inFlight++;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            inFlight--;
            return Json::Value();
        });
    }

    engine.waitUntilIdleForTests(5000);
    CHECK(exclusiveRan.load());
    CHECK(maxConcurrentDuringExclusive.load() == 1); // Exclusive job executed with strictly 1 active job
    engine.clearCompleted();
}

TEST_CASE("Tier 1 - Subsystem: Cooperative job cancellation hook", "[e2e][tier1][subsystem]") {
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers(2);

    std::atomic<bool> hookInvoked{false};
    std::atomic<bool> runningStarted{false};

    JobRequest req;
    req.title = "cancel_target_job";
    req.algorithmId = "callable:cancellable";
    req.exclusive = false;

    std::string jobId = engine.submit(
        req,
        [&runningStarted](const JobRequest&, RSOperatorContext &ctx) {
            runningStarted = true;
            for (int i = 0; i < 100; ++i) {
                ctx.throwIfCancelled();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return Json::Value();
        },
        [&hookInvoked]() {
            hookInvoked = true;
        }
    );

    // Wait until started
    while (!runningStarted.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(engine.cancel(jobId));
    engine.waitUntilIdleForTests(3000);

    auto snap = engine.snapshot(jobId);
    REQUIRE(snap.has_value());
    CHECK(snap->state == JobState::Cancelled);
    CHECK(hookInvoked.load());
    engine.clearCompleted();
}

TEST_CASE("Tier 1 - Subsystem: WorkflowRuntime session management and artifact passing", "[e2e][tier1][subsystem]") {
    WorkflowRuntime runtime(true);

    WorkflowDefinition def;
    def.id = "test_pipeline";
    def.title = "Test Pipeline";
    StepDef s1;
    s1.id = "step1";
    s1.kind = StepKind::Operator;
    s1.operatorId = "rs:ndvi";
    def.steps.push_back(s1);

    runtime.registerDefinition(def);
    CHECK(runtime.hasDefinition("test_pipeline"));

    std::string sessionId = runtime.open("test_pipeline");
    REQUIRE(!sessionId.empty());

    runtime.setArtifact(sessionId, "raster_input", "/path/to/in.tif");
    SessionSnapshot snap = runtime.state(sessionId);
    CHECK(snap.artifacts.count("raster_input") == 1);
    CHECK(snap.artifacts.at("raster_input") == "/path/to/in.tif");

    runtime.close(sessionId);
}

TEST_CASE("Tier 1 - Subsystem: JobEngine record pruning and retention policy", "[e2e][tier1][subsystem]") {
    auto &engine = JobEngine::instance();
    engine.clearCompleted();

    for (int i = 0; i < 10; ++i) {
        JobRequest req;
        req.title = "prune_test_" + std::to_string(i);
        req.algorithmId = "callable:fast";
        engine.submit(req, [](const JobRequest&, RSOperatorContext&) {
            return Json::Value();
        });
    }

    engine.waitUntilIdleForTests(3000);
    CHECK(engine.list().size() == 10);

    // Prune down to 3 records
    size_t removed = engine.pruneCompleted(3);
    CHECK(removed == 7);
    CHECK(engine.list().size() == 3);

    engine.clearCompleted();
    CHECK(engine.list().empty());
}

// ============================================================================
// TIER 2: BOUNDARY & CORNER CASES
// ============================================================================

// ----------------------------------------------------------------------------
// Feature 2.1: Band Math Boundaries
// ----------------------------------------------------------------------------

TEST_CASE("Tier 2 - Band Math: Division by zero and 0/0 indeterminacy", "[e2e][tier2][bandmath]") {
    BandMath::BandData bands;
    bands[1] = {5.0f, 0.0f, -3.0f};
    bands[2] = {0.0f, 0.0f,  0.0f};
    std::vector<float> out(3);

    REQUIRE(BandMath::evaluate("b1 / b2", bands, out.data(), 3));
    CHECK((std::isinf(out[0]) || std::isnan(out[0]))); // 5/0 -> Inf or NaN
    CHECK(std::isnan(out[1]));                         // 0/0 -> NaN
    CHECK((std::isinf(out[2]) || std::isnan(out[2]))); // -3/0 -> -Inf or NaN
}

TEST_CASE("Tier 2 - Band Math: Mathematical domain violations", "[e2e][tier2][bandmath]") {
    BandMath::BandData bands;
    bands[1] = {-4.0f, -1.0f, 2.5f};
    std::vector<float> out(3);

    // sqrt of negative
    REQUIRE(BandMath::evaluate("sqrt(b1)", bands, out.data(), 3));
    CHECK(std::isnan(out[0]));

    // ln of negative
    REQUIRE(BandMath::evaluate("ln(b1)", bands, out.data(), 3));
    CHECK(std::isnan(out[1]));

    // asin out of [-1, 1]
    REQUIRE(BandMath::evaluate("asin(b1)", bands, out.data(), 3));
    CHECK(std::isnan(out[2]));
}

TEST_CASE("Tier 2 - Band Math: Large-magnitude GDAL float NoData sentinel propagation", "[e2e][tier2][bandmath]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString srcPath = tmp.path() + "/sentinel_in.tif";
    const QString dstPath = tmp.path() + "/sentinel_out.tif";

    const float sentinel = -3.4028235e+38f;
    RsSyntheticRasterBuilder builder(2, 2, 1);
    builder.withNoData(sentinel);
    builder.withPixel(1, 0, 0, 10.0f);
    builder.withPixel(1, 1, 0, sentinel);
    builder.withPixel(1, 0, 1, sentinel);
    builder.withPixel(1, 1, 1, 20.0f);
    REQUIRE(!builder.writeToDisk(srcPath).isEmpty());

    QString err;
    REQUIRE(BandMath::processFile(srcPath, dstPath, "b1 * 2.0 + 5.0", &err));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(dstPath));
    std::vector<float> out(4);
    REQUIRE(ds.readBandData(1, out.data(), 2, 2));

    CHECK(out[0] == Approx(25.0f));
    CHECK(std::isnan(out[1])); // Sentinel masked to NaN
    CHECK(std::isnan(out[2])); // Sentinel masked to NaN
    CHECK(out[3] == Approx(45.0f));
}

TEST_CASE("Tier 2 - Band Math: Strict Non-Finite (NaN/Inf) input masking", "[e2e][tier2][bandmath]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    BandMath::BandData bands;
    bands[1] = {nan, 5.0f, inf, 10.0f};
    bands[2] = {1.0f, nan, 2.0f, inf};
    std::vector<float> out(4);

    REQUIRE(BandMath::evaluate("b1 + b2", bands, out.data(), 4));
    CHECK(std::isnan(out[0]));
    CHECK(std::isnan(out[1]));
    CHECK((std::isnan(out[2]) || std::isinf(out[2])));
    CHECK((std::isnan(out[3]) || std::isinf(out[3])));
}

TEST_CASE("Tier 2 - Band Math: Syntax errors and out-of-range band indexing", "[e2e][tier2][bandmath]") {
    BandMath::BandData bands;
    bands[1] = {1.0f, 2.0f};
    bands[2] = {3.0f, 4.0f};
    std::vector<float> out(2);

    // Empty expression
    CHECK_FALSE(BandMath::evaluate("", bands, out.data(), 2));

    // Incomplete expression
    CHECK_FALSE(BandMath::evaluate("b1 +", bands, out.data(), 2));

    // Unmatched parenthesis
    CHECK_FALSE(BandMath::evaluate("(b1 + b2", bands, out.data(), 2));

    // Out-of-bounds band index
    CHECK_FALSE(BandMath::evaluate("b3", bands, out.data(), 2));

    // Null pointer buffer
    CHECK_FALSE(BandMath::evaluate("b1 + b2", bands, nullptr, 2));
}

// ----------------------------------------------------------------------------
// Feature 2.2: OBIA Segmentation Boundaries
// ----------------------------------------------------------------------------

TEST_CASE("Tier 2 - OBIA: Degenerate 0x0 and 1x1 raster extents", "[e2e][tier2][obia]") {
    RsSimpleSegmenter::Params params;
    float singleVal = 42.0f;

    // 0x0 extent
    RsSegmentMap emptyMap = RsSimpleSegmenter::segment(nullptr, 0, 0, -9999.0f, params);
    CHECK(emptyMap.isEmpty());

    // 1x1 extent
    RsSegmentMap singleMap = RsSimpleSegmenter::segment(&singleVal, 1, 1, -9999.0f, params);
    CHECK(!singleMap.isEmpty());
    CHECK(singleMap.width() == 1);
    CHECK(singleMap.height() == 1);
}

TEST_CASE("Tier 2 - OBIA: Uniform and All-NoData rasters", "[e2e][tier2][obia]") {
    constexpr int W = 16, H = 16;
    std::vector<float> uniformData(W * H, 50.0f);
    std::vector<float> noDataOnly(W * H, -9999.0f);

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 8;
    params.minRegionSize = 10;

    // Uniform raster -> should produce 1 single region or empty without crashing
    RsSegmentMap map1 = RsSimpleSegmenter::segment(uniformData.data(), W, H, -9999.0f, params);
    CHECK(map1.segmentCount() <= 1);

    // All-NoData raster -> all pixels 0 (nodata), zero active segments
    RsSegmentMap map2 = RsSimpleSegmenter::segment(noDataOnly.data(), W, H, -9999.0f, params);
    CHECK(map2.segmentCount() == 0);
}

TEST_CASE("Tier 2 - OBIA: Extreme parameter boundary values", "[e2e][tier2][obia]") {
    constexpr int W = 8, H = 8;
    std::vector<float> data(W * H, 10.0f);
    for (int i = 0; i < W * H / 2; ++i) data[i] = 90.0f;

    RsSimpleSegmenter::Params params;
    // Extreme values
    params.smoothKernel = 1;     // Minimum kernel
    params.quantizeBins = 2;     // Minimum bins
    params.minRegionSize = 0;    // Zero min size

    RsSegmentMap map = RsSimpleSegmenter::segment(data.data(), W, H, -9999.0f, params);
    CHECK(!map.isEmpty());
    CHECK(map.segmentCount() >= 1);
}

TEST_CASE("Tier 2 - OBIA: 64-bit raster coordinate bounds safety (#472, #497)", "[e2e][tier2][obia]") {
    // Construct label map with dimensions where 32-bit math would be tested
    constexpr int W = 20, H = 20;
    QVector<quint32> labels(W * H, 1);
    labels[W * H - 1] = 2; // Last pixel

    RsSegmentMap segMap(labels, W, H);
    CHECK(segMap.labelAt(0, 0) == 1);
    CHECK(segMap.labelAt(H - 1, W - 1) == 2);

    // Out of bounds queries
    CHECK(segMap.labelAt(-1, 0) == 0);
    CHECK(segMap.labelAt(H, W) == 0);
    CHECK(segMap.labelAt(100, 100) == 0);
}

TEST_CASE("Tier 2 - OBIA: Disconnected single-pixel islands & diagonal bridges", "[e2e][tier2][obia]") {
    constexpr int W = 8, H = 8;
    std::vector<float> data(W * H, 10.0f);
    // Add isolated single-pixel island and diagonal bridge
    data[0] = 100.0f;
    data[1 * W + 1] = 100.0f;
    data[7 * W + 7] = 200.0f;

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 4;
    params.minRegionSize = 1;

    RsSegmentMap map = RsSimpleSegmenter::segment(data.data(), W, H, -9999.0f, params);
    CHECK(!map.isEmpty());
}

// ----------------------------------------------------------------------------
// Feature 2.3: Change Detection Boundaries
// ----------------------------------------------------------------------------

TEST_CASE("Tier 2 - Change Detection: Identical temporal pairs (T1 == T2)", "[e2e][tier2][changedet]") {
    constexpr size_t N = 8;
    std::vector<float> t1 = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f};
    std::vector<float> t2 = t1; // Identical
    std::vector<float> diff(N), normDiff(N), ratioOut(N);

    REQUIRE(ChangeDetection::difference(t1.data(), t2.data(), diff.data(), N));
    for (float v : diff) CHECK(v == Approx(0.0f));

    REQUIRE(ChangeDetection::normalizedDifference(t1.data(), t2.data(), normDiff.data(), N));
    for (float v : normDiff) CHECK(v == Approx(0.0f));

    REQUIRE(ChangeDetection::ratio(t1.data(), t2.data(), ratioOut.data(), N));
    for (float v : ratioOut) CHECK(v == Approx(1.0f));
}

TEST_CASE("Tier 2 - Change Detection: Zero-variance constant change magnitude Otsu handling", "[e2e][tier2][changedet]") {
    std::vector<float> constantMag(50, 5.0f);
    float threshold = 0.0f;

    REQUIRE(ChangeDetection::otsuThreshold(constantMag.data(), constantMag.size(), &threshold));
    CHECK(threshold == Approx(5.0f));
}

TEST_CASE("Tier 2 - Change Detection: Sparse valid pixels with extensive NaNs", "[e2e][tier2][changedet]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> diff(100, nan);
    diff[42] = 15.0f; // Single valid pixel

    ChangeDetection::ChangeStats stats = ChangeDetection::statistics(diff.data(), diff.size());
    CHECK(stats.count == 100);
    CHECK(stats.validCount == 1);
    CHECK(stats.mean == Approx(15.0f));
    CHECK(stats.min == Approx(15.0f));
    CHECK(stats.max == Approx(15.0f));
    CHECK(stats.stddev == Approx(0.0f));
}

TEST_CASE("Tier 2 - Change Detection: Morphological cleanup on NoData sentinels", "[e2e][tier2][changedet]") {
    // 5x5 mask with NoData (255) center
    std::vector<uint8_t> mask(25, 0);
    mask[12] = 255; // Center is NoData
    mask[0] = 1;    // Corner isolated pixel

    ChangeDetection::morphologicalCleanup(mask.data(), 5, 5, 1, ChangeDetection::MorphOp::Open);
    CHECK(mask[12] == 255); // NoData preserved
    CHECK(mask[0] == 0);    // Isolated noise removed
}

TEST_CASE("Tier 2 - Change Detection: Connected component MMU filter edge cases", "[e2e][tier2][changedet]") {
    // 8-connectivity note: the isolated pixel must not touch the 2x2 block
    // even diagonally, or the union-find merges them into one component.
    std::vector<uint8_t> mask(16, 0);
    mask[0] = 1; // 1 pixel component at (0,0)
    mask[10] = mask[11] = mask[14] = mask[15] = 1; // 4 pixel 2x2 block at rows 2-3, cols 2-3

    // Min area = 3: 1-pixel component dropped, 4-pixel component kept
    REQUIRE(ChangeDetection::connectedComponentFilter(mask.data(), 4, 4, 3));
    CHECK(mask[0] == 0);
    CHECK(mask[10] == 1);
    CHECK(mask[11] == 1);
    CHECK(mask[14] == 1);
    CHECK(mask[15] == 1);

    // Invalid arguments fail-closed
    CHECK_FALSE(ChangeDetection::connectedComponentFilter(nullptr, 4, 4, 2));
    CHECK_FALSE(ChangeDetection::connectedComponentFilter(mask.data(), 0, 4, 2));
}

// ----------------------------------------------------------------------------
// Feature 2.4: Subsystem & Concurrency Boundaries
// ----------------------------------------------------------------------------

TEST_CASE("Tier 2 - Subsystem: High-frequency job queue flooding", "[e2e][tier2][subsystem]") {
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers(4);

    constexpr int FLOOD_COUNT = 50;
    std::atomic<int> counter{0};

    for (int i = 0; i < FLOOD_COUNT; ++i) {
        JobRequest req;
        req.title = "flood_" + std::to_string(i);
        req.algorithmId = "callable:flood";
        engine.submit(req, [&counter](const JobRequest&, RSOperatorContext&) {
            counter++;
            return Json::Value();
        });
    }

    engine.waitUntilIdleForTests(5000);
    CHECK(counter == FLOOD_COUNT);
    engine.clearCompleted();
}

TEST_CASE("Tier 2 - Subsystem: Rapid submission and cancellation race", "[e2e][tier2][subsystem]") {
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers(2);

    for (int i = 0; i < 20; ++i) {
        JobRequest req;
        req.title = "race_" + std::to_string(i);
        req.algorithmId = "callable:race";
        std::string jobId = engine.submit(req, [](const JobRequest&, RSOperatorContext &ctx) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ctx.throwIfCancelled();
            return Json::Value();
        });
        engine.cancel(jobId);
    }

    engine.waitUntilIdleForTests(3000);
    engine.clearCompleted();
    SUCCEED("Rapid cancellation race completed without deadlock or crash");
}

TEST_CASE("Tier 2 - Subsystem: WorkflowRuntime non-existent step error handling", "[e2e][tier2][subsystem]") {
    WorkflowRuntime runtime(true);
    WorkflowDefinition def;
    def.id = "empty_flow";
    runtime.registerDefinition(def);

    std::string sessionId = runtime.open("empty_flow");
    REQUIRE(!sessionId.empty());

    CHECK_FALSE(runtime.gotoStep(sessionId, "non_existent_step"));
    CHECK_THROWS_AS(runtime.runStep(sessionId, "non_existent_step"), std::runtime_error);

    runtime.close(sessionId);
}

TEST_CASE("Tier 2 - Subsystem: JobEngine max worker clamping boundaries", "[e2e][tier2][subsystem]") {
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers(-5);
    CHECK(engine.maxWorkers() == 2); // Clamped to min 2

    engine.setMaxWorkers(100);
    CHECK(engine.maxWorkers() == 4); // Clamped to max 4

    engine.setMaxWorkers(3);
    CHECK(engine.maxWorkers() == 3);
}

TEST_CASE("Tier 2 - Subsystem: Thread-safe concurrent workflow definition queries", "[e2e][tier2][subsystem]") {
    WorkflowRuntime runtime(true);
    constexpr int THREADS = 4;
    std::vector<std::thread> workers;

    for (int t = 0; t < THREADS; ++t) {
        workers.emplace_back([&runtime, t]() {
            for (int i = 0; i < 50; ++i) {
                WorkflowDefinition def;
                def.id = "def_" + std::to_string(t) + "_" + std::to_string(i);
                runtime.registerDefinition(def);
                CHECK(runtime.hasDefinition(def.id));
                std::string sess = runtime.open(def.id);
                if (!sess.empty()) {
                    runtime.close(sess);
                }
            }
        });
    }

    for (auto &th : workers) {
        th.join();
    }
}

// ============================================================================
// TIER 3: CROSS-FEATURE COMBINATIONS (PAIRWISE INTEGRATION)
// ============================================================================

TEST_CASE("Tier 3 - Pairwise: Band-Math Index to Baatz-Schäpe OBIA Segmentation", "[e2e][tier3][pairwise]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString multiBandPath = tmp.path() + "/multispectral.tif";
    const QString indexPath = tmp.path() + "/custom_index.tif";
    const QString segPath = tmp.path() + "/index_seg.tif";

    constexpr int W = 32, H = 32;
    // Generate 4-band image with 2 distinct ecological zones (Forest left, Desert right)
    RsSyntheticRasterBuilder builder(W, H, 4);
    builder.withRect(3, 0, 0, 16, 32, 0.05f);  // Forest Red (Band 3)
    builder.withRect(4, 0, 0, 16, 32, 0.65f);  // Forest NIR (Band 4)
    builder.withRect(3, 16, 0, 32, 32, 0.35f); // Desert Red (Band 3)
    builder.withRect(4, 16, 0, 32, 32, 0.40f); // Desert NIR (Band 4)
    REQUIRE(!builder.writeToDisk(multiBandPath).isEmpty());

    // 1. Evaluate custom index: (b4 - b3) / (b4 + b3)
    QString err;
    REQUIRE(BandMath::processFile(multiBandPath, indexPath, "(b4 - b3) / (b4 + b3)", &err));
    REQUIRE(QFile::exists(indexPath));

    // 2. Feed index raster directly into OBIA segmenter
    GdalDatasetWrapper indexDs;
    REQUIRE(indexDs.open(indexPath));
    std::vector<float> indexData(W * H);
    REQUIRE(indexDs.readBandData(1, indexData.data(), W, H));

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    // 2 bins: folds the index transition band into one zone (binary zoning).
    params.quantizeBins = 2;
    params.minRegionSize = 20;

    RsSegmentMap segMap = RsSimpleSegmenter::segment(indexData.data(), W, H, -9999.0f, params);
    REQUIRE(!segMap.isEmpty());
    CHECK(segMap.segmentCount() == 2); // Perfectly separates Forest from Desert based on computed index

    quint32 forestSeg = segMap.labelAt(16, 5);
    quint32 desertSeg = segMap.labelAt(16, 25);
    CHECK(forestSeg != desertSeg);
    CHECK(forestSeg != 0);
    CHECK(desertSeg != 0);
}

TEST_CASE("Tier 3 - Pairwise: Normalized Indices to Multi-Temporal Differencing & Otsu Threshold", "[e2e][tier3][pairwise]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString t1Path = tmp.path() + "/pre_fire.tif";
    const QString t2Path = tmp.path() + "/post_fire.tif";
    const QString nbr1Path = tmp.path() + "/nbr1.tif";
    const QString nbr2Path = tmp.path() + "/nbr2.tif";

    constexpr int W = 32, H = 32;
    // Pre-fire: High NIR (b4=0.7), Low SWIR2 (b6=0.1) across entire scene
    RsSyntheticRasterBuilder preBuilder(W, H, 6);
    preBuilder.withConstantValue(4, 0.70f);
    preBuilder.withConstantValue(6, 0.10f);
    REQUIRE(!preBuilder.writeToDisk(t1Path).isEmpty());

    // Post-fire: Burn scar in central 16x16 box (b4=0.15, b6=0.50)
    RsSyntheticRasterBuilder postBuilder(W, H, 6);
    postBuilder.withConstantValue(4, 0.70f);
    postBuilder.withConstantValue(6, 0.10f);
    postBuilder.withRect(4, 8, 8, 24, 24, 0.15f);
    postBuilder.withRect(6, 8, 8, 24, 24, 0.50f);
    REQUIRE(!postBuilder.writeToDisk(t2Path).isEmpty());

    // 1. Calculate NBR = (b4 - b6) / (b4 + b6)
    QString err;
    REQUIRE(BandMath::processFile(t1Path, nbr1Path, "(b4 - b6) / (b4 + b6)", &err));
    REQUIRE(BandMath::processFile(t2Path, nbr2Path, "(b4 - b6) / (b4 + b6)", &err));

    // 2. Read NBR arrays and calculate dNBR = NBR_pre - NBR_post
    GdalDatasetWrapper ds1, ds2;
    REQUIRE(ds1.open(nbr1Path));
    REQUIRE(ds2.open(nbr2Path));
    std::vector<float> nbr1(W * H), nbr2(W * H), dnbr(W * H);
    REQUIRE(ds1.readBandData(1, nbr1.data(), W, H));
    REQUIRE(ds2.readBandData(1, nbr2.data(), W, H));

    for (size_t i = 0; i < W * H; ++i) {
        dnbr[i] = nbr1[i] - nbr2[i];
    }

    // 3. Automated Otsu thresholding on dNBR
    float threshold = 0.0f;
    REQUIRE(ChangeDetection::otsuThreshold(dnbr.data(), dnbr.size(), &threshold));
    CHECK(threshold > 0.3f); // Threshold separates unburned (~0.0) from burned (~1.28)

    std::vector<uint8_t> burnMask(W * H);
    REQUIRE(ChangeDetection::changeMask(dnbr.data(), burnMask.data(), W * H, threshold));

    // Verify center burn box is classified as burned (1)
    CHECK(burnMask[16 * W + 16] == 1);
    // Verify corner is unburned (0)
    CHECK(burnMask[0] == 0);
}

TEST_CASE("Tier 3 - Pairwise: OBIA Segmentation to Feature Extraction & Decision Rule Classification", "[e2e][tier3][pairwise]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString rasterPath = tmp.path() + "/classified_obia.tif";
    constexpr int W = 32, H = 32;

    // 4 bands: Water (top half: low NIR, high Blue), Vegetation (bottom half: high NIR, low Blue)
    RsSyntheticRasterBuilder builder(W, H, 4);
    builder.withRect(1, 0, 0, 32, 16, 0.20f);  // Water Blue
    builder.withRect(4, 0, 0, 32, 16, 0.05f);  // Water NIR
    builder.withRect(1, 0, 16, 32, 32, 0.03f); // Veg Blue
    builder.withRect(4, 0, 16, 32, 32, 0.60f); // Veg NIR
    REQUIRE(!builder.writeToDisk(rasterPath).isEmpty());

    const auto &vecs = builder.toVectors();
    const float* bands[4] = {vecs[0].data(), vecs[1].data(), vecs[2].data(), vecs[3].data()};

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    // The 1-row water/vegetation transition rounds into different per-band
    // bins (blue dips, NIR rises), so it always forms its own 32-pixel class;
    // a minRegionSize above the 2-row band width (64 px) absorbs it into a neighbour.
    params.quantizeBins = 2;
    params.minRegionSize = 80;

    RsSegmentMap segMap = RsSimpleSegmenter::segmentMultiBand(bands, 4, W, H, -9999.0f, params);
    REQUIRE(segMap.segmentCount() == 2);

    auto stats = RsSegmentFeatures::extract(rasterPath, segMap, {1, 4});
    REQUIRE(stats.size() == 2);

    // Decision rule: if Mean(NIR) > 0.3 -> Class 1 (Vegetation), else Class 2 (Water)
    std::map<quint32, int> classMap;
    for (auto it = stats.begin(); it != stats.end(); ++it) {
        quint32 segId = it.key();
        double meanNir = it.value().mean[1]; // Band 4 is 2nd extracted band
        classMap[segId] = (meanNir > 0.3) ? 1 : 2;
    }

    quint32 waterSeg = segMap.labelAt(5, 16);
    quint32 vegSeg = segMap.labelAt(25, 16);

    CHECK(classMap[waterSeg] == 2); // Water
    CHECK(classMap[vegSeg] == 1);   // Veg
}

TEST_CASE("Tier 3 - Pairwise: Multi-Band CVA Magnitude to Thresholding & MMU Mask Cleanup", "[e2e][tier3][pairwise]") {
    constexpr int W = 16, H = 16;
    constexpr size_t N = W * H;

    // 2-band before and after
    std::vector<float> b1(N, 10.0f), b2(N, 10.0f);
    std::vector<float> a1(N, 10.0f), a2(N, 10.0f);

    // Large change in 4x4 block (rows 6..9, cols 6..9)
    for (int y = 6; y < 10; ++y) {
        for (int x = 6; x < 10; ++x) {
            a1[y * W + x] = 50.0f; // delta = 40
            a2[y * W + x] = 40.0f; // delta = 30 -> CVA mag = 50.0
        }
    }
    // Isolated 1-pixel noise speck at (0, 0)
    a1[0] = 50.0f; a2[0] = 40.0f;

    const float* beforeBands[] = {b1.data(), b2.data()};
    const float* afterBands[]  = {a1.data(), a2.data()};
    std::vector<float> mag(N);

    QString err;
    REQUIRE(ChangeDetection::cvaMagnitude(beforeBands, afterBands, 2, N, mag.data(), &err));

    // Threshold at 25.0
    std::vector<uint8_t> mask(N);
    REQUIRE(ChangeDetection::changeMask(mag.data(), mask.data(), N, 25.0f));
    CHECK(mask[0] == 1);             // Isolated speck is currently 1
    CHECK(mask[7 * W + 7] == 1);     // Main block is 1

    // Apply MMU filter with minArea = 4
    REQUIRE(ChangeDetection::connectedComponentFilter(mask.data(), W, H, 4));
    CHECK(mask[0] == 0);             // Isolated speck eliminated by MMU
    CHECK(mask[7 * W + 7] == 1);     // 16-pixel block survived
}

TEST_CASE("Tier 3 - Pairwise: Workflow DAG Engine Multi-Step Session Execution", "[e2e][tier3][pairwise]") {
    WorkflowRuntime runtime(true);

    WorkflowDefinition def;
    def.id = "ndvi_change_pipeline";
    def.title = "NDVI Change Pipeline";

    StepDef s1;
    s1.id = "calc_ndvi";
    s1.kind = StepKind::Operator;
    s1.operatorId = "rs:ndvi";
    def.steps.push_back(s1);

    runtime.registerDefinition(def);
    std::string sessionId = runtime.open("ndvi_change_pipeline");
    REQUIRE(!sessionId.empty());

    runtime.markStepComplete(sessionId, "calc_ndvi");
    SessionSnapshot snap = runtime.state(sessionId);
    const std::vector<std::string> &completed = snap.completedStepIds;
    CHECK(std::find(completed.begin(), completed.end(), "calc_ndvi") != completed.end());

    runtime.close(sessionId);
}

// ============================================================================
// TIER 4: REAL-WORLD APPLICATION SCENARIOS
// ============================================================================

TEST_CASE("Tier 4 - Scenario 1: Wildfire Burn Severity & Forest Recovery Assessment (Sentinel-2)", "[e2e][tier4][scenario]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString prePath = tmp.path() + "/s2_pre_fire.tif";
    const QString postPath = tmp.path() + "/s2_post_fire.tif";
    const QString dnbrPath = tmp.path() + "/s2_dnbr.tif";

    constexpr int W = 64, H = 64;
    // Build pre-fire and post-fire 6-band Sentinel-2 scenes (B2, B3, B4, B8A, B11, B12)
    RsSyntheticRasterBuilder preBuilder(W, H, 6);
    preBuilder.withSimulatedScenery(SceneryType::Wildfire, false);
    REQUIRE(!preBuilder.writeToDisk(prePath).isEmpty());

    RsSyntheticRasterBuilder postBuilder(W, H, 6);
    postBuilder.withSimulatedScenery(SceneryType::Wildfire, true);
    REQUIRE(!postBuilder.writeToDisk(postPath).isEmpty());

    // 1. Compute NBR for pre and post: (b4 - b6) / (b4 + b6)
    const QString nbrPrePath = tmp.path() + "/nbr_pre.tif";
    const QString nbrPostPath = tmp.path() + "/nbr_post.tif";
    QString err;
    REQUIRE(BandMath::processFile(prePath, nbrPrePath, "(b4 - b6) / (b4 + b6)", &err));
    REQUIRE(BandMath::processFile(postPath, nbrPostPath, "(b4 - b6) / (b4 + b6)", &err));

    // 2. Compute dNBR
    GdalDatasetWrapper preDs, postDs;
    REQUIRE(preDs.open(nbrPrePath));
    REQUIRE(postDs.open(nbrPostPath));
    std::vector<float> preNbr(W * H), postNbr(W * H), dnbr(W * H);
    REQUIRE(preDs.readBandData(1, preNbr.data(), W, H));
    REQUIRE(postDs.readBandData(1, postNbr.data(), W, H));

    for (size_t i = 0; i < W * H; ++i) {
        dnbr[i] = preNbr[i] - postNbr[i];
    }

    // 3. Slice into USGS burn severity classes:
    // Unburned (< 0.1), Low (0.1 - 0.27), Moderate (0.27 - 0.66), High (> 0.66)
    int unburnedCount = 0;
    int burnedCount = 0;
    for (size_t i = 0; i < W * H; ++i) {
        if (dnbr[i] < 0.10f) {
            unburnedCount++;
        } else {
            burnedCount++;
        }
    }

    CHECK(unburnedCount > 0);
    CHECK(burnedCount > 0);
    // Center burn scar was created with radius ~ W/3
    // Area of circle radius 21 is pi*21^2 ≈ 1385 pixels out of 4096 (~33%)
    CHECK(burnedCount > 1000);
    CHECK(burnedCount < 2000);

    // Verify center pixel has high burn severity
    float centerDnbr = dnbr[(H / 2) * W + (W / 2)];
    CHECK(centerDnbr > 0.66f); // High severity
}

TEST_CASE("Tier 4 - Scenario 2: Multi-Parcel Agricultural Crop Field OBIA Delineation", "[e2e][tier4][scenario]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString agPath = tmp.path() + "/ag_fields.tif";
    constexpr int W = 64, H = 64;

    // 4-band agricultural scene with 4x4 grid of crop fields with distinct phenology
    RsSyntheticRasterBuilder builder(W, H, 4);
    builder.withSimulatedScenery(SceneryType::Agriculture);
    REQUIRE(!builder.writeToDisk(agPath).isEmpty());

    const auto &vecs = builder.toVectors();
    const float* bands[4] = {vecs[0].data(), vecs[1].data(), vecs[2].data(), vecs[3].data()};

    RsSimpleSegmenter::Params params;
    params.smoothKernel = 3;
    params.quantizeBins = 16;
    params.minRegionSize = 30;

    RsSegmentMap segMap = RsSimpleSegmenter::segmentMultiBand(bands, 4, W, H, -9999.0f, params);
    REQUIRE(!segMap.isEmpty());
    CHECK(segMap.segmentCount() >= 10); // Delineates the distinct agricultural parcel clusters

    // Extract GLCM texture and geometry for all field parcels
    auto stats = RsSegmentFeatures::extract(agPath, segMap, {1, 2, 3, 4});
    REQUIRE(stats.size() >= 10);

    for (auto it = stats.begin(); it != stats.end(); ++it) {
        CHECK(it.value().area >= 30.0);
        CHECK(it.value().mean.size() == 4);
        CHECK(it.value().glcmContrast.size() == 4);
    }
}

TEST_CASE("Tier 4 - Scenario 3: Urban Sprawl & Multi-Temporal Change Detection", "[e2e][tier4][scenario]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString t1Path = tmp.path() + "/urban_t1.tif";
    const QString t2Path = tmp.path() + "/urban_t2.tif";

    constexpr int W = 48, H = 48;
    constexpr size_t N = W * H;

    RsSyntheticRasterBuilder b1(W, H, 4);
    b1.withSimulatedScenery(SceneryType::UrbanSprawl, false);
    REQUIRE(!b1.writeToDisk(t1Path).isEmpty());

    RsSyntheticRasterBuilder b2(W, H, 4);
    b2.withSimulatedScenery(SceneryType::UrbanSprawl, true);
    REQUIRE(!b2.writeToDisk(t2Path).isEmpty());

    // Compute CVA magnitude across all 4 bands
    const auto &v1 = b1.toVectors();
    const auto &v2 = b2.toVectors();
    const float* beforeBands[] = {v1[0].data(), v1[1].data(), v1[2].data(), v1[3].data()};
    const float* afterBands[]  = {v2[0].data(), v2[1].data(), v2[2].data(), v2[3].data()};

    std::vector<float> cvaMag(N);
    QString err;
    REQUIRE(ChangeDetection::cvaMagnitude(beforeBands, afterBands, 4, N, cvaMag.data(), &err));

    // Automated Otsu threshold
    float threshold = 0.0f;
    REQUIRE(ChangeDetection::otsuThreshold(cvaMag.data(), N, &threshold));

    std::vector<uint8_t> changeMask(N);
    REQUIRE(ChangeDetection::changeMask(cvaMag.data(), changeMask.data(), N, threshold));

    // Apply MMU cleanup
    REQUIRE(ChangeDetection::connectedComponentFilter(changeMask.data(), W, H, 10));

    // Calculate changed area (expansion zone is in middle third: 16 <= x < 32)
    int changedPixels = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (changeMask[y * W + x] == 1) {
                changedPixels++;
                CHECK(x >= 16);
                CHECK(x < 32);
            }
        }
    }

    CHECK(changedPixels > 500); // 16 * 48 = 768 pixels in expansion corridor
}

TEST_CASE("Tier 4 - Scenario 4: Rapid Flood Disaster Inundation Mapping (MNDWI)", "[e2e][tier4][scenario]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const QString floodPath = tmp.path() + "/flood_scene.tif";
    const QString mndwiPath = tmp.path() + "/mndwi_out.tif";

    constexpr int W = 50, H = 50;
    // 5-band imagery: 1=Blue, 2=Green, 3=Red, 4=NIR, 5=SWIR1
    RsSyntheticRasterBuilder builder(W, H, 5);
    builder.withSimulatedScenery(SceneryType::FloodInundation, true);
    REQUIRE(!builder.writeToDisk(floodPath).isEmpty());

    // 1. Evaluate MNDWI = (Green - SWIR1) / (Green + SWIR1) = (b2 - b5) / (b2 + b5)
    QString err;
    REQUIRE(BandMath::processFile(floodPath, mndwiPath, "(b2 - b5) / (b2 + b5)", &err));
    REQUIRE(QFile::exists(mndwiPath));

    GdalDatasetWrapper ds;
    REQUIRE(ds.open(mndwiPath));
    std::vector<float> mndwi(W * H);
    REQUIRE(ds.readBandData(1, mndwi.data(), W, H));

    // 2. Water detection: MNDWI > 0.0 indicates water
    int floodPixelCount = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float val = mndwi[y * W + x];
            if (val > 0.0f) {
                floodPixelCount++;
                // Flooded river valley was simulated between y=20 and y=30
                CHECK(y >= 18);
                CHECK(y <= 32);
            }
        }
    }

    // River valley width is 11 rows * 50 cols = 550 pixels
    CHECK(floodPixelCount >= 500);
    CHECK(floodPixelCount <= 600);
}
