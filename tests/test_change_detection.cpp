// tests/test_change_detection.cpp — TDD for change detection algorithm
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/math_utils.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "operators/rs/rs_change_primitives.h"
#include "operators/rs/rs_change_detection_operator.h"
#include "operators/framework/rs_operator_context.h"

#include <QTemporaryDir>
#include <QString>

#include <gdal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

using namespace ChangeDetection;
using Catch::Approx;

TEST_CASE("ChangeDetection difference computes absolute change", "[processing][change_detection]") {
    std::vector<float> before = {10.0f, 20.0f, 30.0f, 40.0f};
    std::vector<float> after  = {12.0f, 18.0f, 30.0f, 50.0f};

    std::vector<float> out(4, 0.0f);

    REQUIRE(difference(before.data(), after.data(), out.data(), 4));

    CHECK(out[0] == Approx(2.0f));
    CHECK(out[1] == Approx(2.0f));
    CHECK(out[2] == Approx(0.0f));
    CHECK(out[3] == Approx(10.0f));
}

TEST_CASE("ChangeDetection difference returns false on null pointers", "[processing][change_detection]") {
    std::vector<float> data(4, 1.0f);
    std::vector<float> out(4, 0.0f);

    CHECK_FALSE(difference(nullptr, data.data(), out.data(), 4));
    CHECK_FALSE(difference(data.data(), nullptr, out.data(), 4));
    CHECK_FALSE(difference(data.data(), data.data(), nullptr, 4));
}

TEST_CASE("ChangeDetection difference returns false on zero count", "[processing][change_detection]") {
    std::vector<float> data(1, 1.0f);
    std::vector<float> out(1, 0.0f);

    CHECK_FALSE(difference(data.data(), data.data(), out.data(), 0));
}

TEST_CASE("ChangeDetection normalizedDifference computes relative change", "[processing][change_detection]") {
    std::vector<float> before = {100.0f, 50.0f, 0.0f, 10.0f};
    std::vector<float> after  = {120.0f, 30.0f, 0.0f, 10.0f};

    std::vector<float> out(4, 0.0f);

    REQUIRE(normalizedDifference(before.data(), after.data(), out.data(), 4));

    // (after - before) / (after + before)
    // (120-100)/(120+100) = 20/220 ≈ 0.0909
    CHECK(out[0] == Approx(20.0f / 220.0f).margin(0.001f));
    // (30-50)/(30+50) = -20/80 = -0.25
    CHECK(out[1] == Approx(-0.25f).margin(0.001f));
    // (0-0)/(0+0) = NaN → should be NaN
    CHECK(std::isnan(out[2]));
    // (10-10)/(10+10) = 0/20 = 0
    CHECK(out[3] == Approx(0.0f));
}

TEST_CASE("ChangeDetection changeMask with threshold", "[processing][change_detection]") {
    std::vector<float> diff = {0.5f, 2.0f, 0.1f, 5.0f, 1.0f};
    std::vector<uint8_t> mask(5, 0);

    REQUIRE(changeMask(diff.data(), mask.data(), 5, 1.0f));

    CHECK(mask[0] == 0);  // 0.5 < 1.0
    CHECK(mask[1] == 1);  // 2.0 >= 1.0
    CHECK(mask[2] == 0);  // 0.1 < 1.0
    CHECK(mask[3] == 1);  // 5.0 >= 1.0
    CHECK(mask[4] == 1);  // 1.0 >= 1.0 (boundary)
}

TEST_CASE("ChangeDetection changeMask returns false on null", "[processing][change_detection]") {
    std::vector<uint8_t> mask(4, 0);
    CHECK_FALSE(changeMask(nullptr, mask.data(), 4, 1.0f));
    CHECK_FALSE(changeMask(nullptr, nullptr, 4, 1.0f));
}

TEST_CASE("ChangeDetection statistics computes change stats", "[processing][change_detection]") {
    std::vector<float> diff = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

    ChangeStats stats = statistics(diff.data(), 5);

    CHECK(stats.count == 5);
    CHECK(stats.mean == Approx(3.0f));
    CHECK(stats.min == Approx(1.0f));
    CHECK(stats.max == Approx(5.0f));
    CHECK(stats.stddev > 0.0f);
}

TEST_CASE("ChangeDetection statistics with single value", "[processing][change_detection]") {
    std::vector<float> diff = {42.0f};

    ChangeStats stats = statistics(diff.data(), 1);

    CHECK(stats.count == 1);
    CHECK(stats.mean == Approx(42.0f));
    CHECK(stats.min == Approx(42.0f));
    CHECK(stats.max == Approx(42.0f));
    CHECK(stats.stddev == Approx(0.0f));
}

TEST_CASE("ChangeDetection statistics returns zero on null/empty", "[processing][change_detection]") {
    ChangeStats stats = statistics(nullptr, 0);
    CHECK(stats.count == 0);
    CHECK(stats.mean == 0.0f);
}

// ---------------------------------------------------------------------------
// Change Detection 2.0 kernels: ratio, CVA, thresholds, morphological cleanup
// ---------------------------------------------------------------------------

TEST_CASE("ChangeDetection ratio divides after by before", "[processing][change_detection][c1]") {
    const float before[] = {2.0f, 5.0f, 0.0f, -2.0f};
    const float after[] = {6.0f, 5.0f, 10.0f, 4.0f};
    float out[4] = {};
    REQUIRE(ratio(before, after, out, 4));
    CHECK(out[0] == Approx(3.0f));
    CHECK(out[1] == Approx(1.0f));
    CHECK(std::isnan(out[2])); // before == 0 -> NaN
    CHECK(out[3] == Approx(-2.0f));
}

TEST_CASE("ChangeDetection cvaMagnitude sums squared band deltas", "[processing][change_detection][c1]") {
    // 2 bands, 3 pixels. Deltas: b0 = [3, 4, 1], b1 = [4, 0, 0]
    const float before0[] = {0, 0, 0};
    const float after0[] = {3, 4, 1};
    const float before1[] = {0, 0, 0};
    const float after1[] = {4, 0, 0};
    const float* beforeBands[] = {before0, before1};
    const float* afterBands[] = {after0, after1};
    float out[3] = {};
    QString err;
    REQUIRE(cvaMagnitude(beforeBands, afterBands, 2, 3, out, &err));
    CHECK(out[0] == Approx(5.0f)); // sqrt(9+16)
    CHECK(out[1] == Approx(4.0f)); // sqrt(16+0)
    CHECK(out[2] == Approx(1.0f)); // sqrt(1+0)
}

TEST_CASE("ChangeDetection otsuThreshold separates a bimodal scene", "[processing][change_detection][c1]") {
    // Two clusters with spread: ~50 values near 1, ~50 values near 10.
    std::vector<float> values;
    for (int i = 0; i < 50; ++i) {
        values.push_back(0.5f + 0.01f * static_cast<float>(i));
        values.push_back(9.5f + 0.01f * static_cast<float>(i));
    }
    float threshold = 0.0f;
    REQUIRE(otsuThreshold(values.data(), values.size(), &threshold));
    // Between two compact clusters the between-class variance is a plateau:
    // any split strictly between the clusters is optimal. Assert the threshold
    // separates the clusters (all lows at/below, all highs at/above).
    for (int i = 0; i < 50; ++i) {
        CHECK(values[2 * i] <= threshold);      // low cluster
        CHECK(values[2 * i + 1] >= threshold);  // high cluster
    }
}

TEST_CASE("ChangeDetection otsuThreshold handles single-level and invalid input", "[processing][change_detection][c1]") {
    SECTION("All identical values -> that value") {
        const float v[] = {3.0f, 3.0f, 3.0f};
        float t = 0.0f;
        REQUIRE(otsuThreshold(v, 3, &t));
        CHECK(t == Approx(3.0f));
    }
    SECTION("All NaN -> false") {
        const float v[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
        float t = 0.0f;
        CHECK_FALSE(otsuThreshold(v, 2, &t));
    }
    SECTION("Null/empty -> false") {
        float t = 0.0f;
        CHECK_FALSE(otsuThreshold(nullptr, 5, &t));
        CHECK_FALSE(otsuThreshold(nullptr, 0, &t));
    }
}

TEST_CASE("ChangeDetection percentileThreshold uses nearest-rank", "[processing][change_detection][c1]") {
    std::vector<float> values = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    float t = 0.0f;
    REQUIRE(percentileThreshold(values.data(), values.size(), 90.0, &t));
    CHECK(t == Approx(90.0f));
    REQUIRE(percentileThreshold(values.data(), values.size(), 50.0, &t));
    CHECK(t == Approx(50.0f));
    // Out-of-range percentile clamps.
    REQUIRE(percentileThreshold(values.data(), values.size(), 150.0, &t));
    CHECK(t == Approx(100.0f));
}

TEST_CASE("ChangeDetection morphologicalCleanup removes isolated noise and fills holes", "[processing][change_detection][c1]") {
    // 5x5 mask: a solid 3x3 block (rows 1-3, cols 1-3) survives open; isolated
    // pixels are removed by the erosion pass.
    std::vector<uint8_t> mask(25, 0);
    for (int r = 1; r <= 3; ++r) {
        for (int c = 1; c <= 3; ++c)
            mask[static_cast<size_t>(r) * 5 + c] = 1;
    }
    mask[0] = 1;  // isolated corner pixel
    mask[21] = 1; // isolated pixel (row 4, col 1)

    morphologicalCleanup(mask.data(), 5, 5, 1, MorphOp::Open);
    CHECK(mask[0] == 0);  // isolated noise removed
    CHECK(mask[21] == 0); // isolated noise removed
    for (int r = 1; r <= 3; ++r) {
        for (int c = 1; c <= 3; ++c)
            CHECK(mask[static_cast<size_t>(r) * 5 + c] == 1); // block survives
    }

    // Close fills a one-pixel hole (3x3 ring).
    std::vector<uint8_t> ring(9, 0);
    ring[0] = ring[1] = ring[2] = 1;
    ring[3] = ring[5] = 1;
    ring[6] = ring[7] = ring[8] = 1;
    morphologicalCleanup(ring.data(), 3, 3, 1, MorphOp::Close);
    CHECK(ring[4] == 1); // center hole filled
}

TEST_CASE("ChangeDetection morphologicalCleanup never touches NoData", "[processing][change_detection][c1]") {
    std::vector<uint8_t> mask(9, 0);
    mask[4] = 255; // NoData center
    mask[0] = 1;
    morphologicalCleanup(mask.data(), 3, 3, 1, MorphOp::Erode);
    CHECK(mask[4] == 255);
}

TEST_CASE("ChangeDetection connectedComponentFilter enforces the MMU", "[processing][change_detection][c1]") {
    // 5x5: 2x2 block (4 px, top-left), 1-px dot (bottom-right), NoData centre.
    std::vector<uint8_t> mask(25, 0);
    mask[1 * 5 + 1] = mask[1 * 5 + 2] = 1;
    mask[2 * 5 + 1] = 1;
    mask[2 * 5 + 2] = 255; // NoData centre
    mask[4 * 5 + 4] = 1;   // isolated dot

    SECTION("minArea 0 is a no-op") {
        REQUIRE(connectedComponentFilter(mask.data(), 5, 5, 0));
        CHECK(mask[1 * 5 + 1] == 1);
        CHECK(mask[4 * 5 + 4] == 1);
        CHECK(mask[2 * 5 + 2] == 255);
    }

    SECTION("Components below minArea are dropped, larger survive") {
        REQUIRE(connectedComponentFilter(mask.data(), 5, 5, 4));
        // 2x2 block has 3 mask pixels here (centre is NoData) -> also dropped at 4.
        // Use a threshold of 3 so the block (3 px) survives and the dot (1 px) is dropped.
        std::vector<uint8_t> m2(25, 0);
        m2[1 * 5 + 1] = m2[1 * 5 + 2] = 1;
        m2[2 * 5 + 1] = 1; // 3-px component
        m2[4 * 5 + 4] = 1; // 1-px dot
        m2[2 * 5 + 2] = 255;
        REQUIRE(connectedComponentFilter(m2.data(), 5, 5, 3));
        CHECK(m2[1 * 5 + 1] == 1);
        CHECK(m2[2 * 5 + 1] == 1);
        CHECK(m2[4 * 5 + 4] == 0); // dot removed
        CHECK(m2[2 * 5 + 2] == 255); // NoData untouched
    }

    SECTION("All components below minArea vanish") {
        REQUIRE(connectedComponentFilter(mask.data(), 5, 5, 10));
        CHECK(mask[1 * 5 + 1] == 0);
        CHECK(mask[4 * 5 + 4] == 0);
        CHECK(mask[2 * 5 + 2] == 255);
    }

    SECTION("Invalid arguments fail") {
        CHECK_FALSE(connectedComponentFilter(nullptr, 5, 5, 1));
        CHECK_FALSE(connectedComponentFilter(mask.data(), 0, 5, 1));
    }
}

TEST_CASE("ChangeDetection connectedComponentFilter sparse path matches dense semantics", "[processing][change_detection][c1]") {
    // #648: masks below 25% foreground run the hash-table store, all-
    // foreground masks take the single-component early-out. Both must agree
    // with the dense union-find on the MMU decision.
    SECTION("Sparse mask (8% foreground) keeps large component, drops dots") {
        std::vector<uint8_t> mask(50 * 50, 0);
        for (int r = 10; r < 20; ++r)
            for (int c = 10; c < 20; ++c)
                mask[r * 50 + c] = 1; // 100-px component (4% of the scene)
        mask[30 * 50 + 30] = 1; // 1-px dot
        mask[45 * 50 + 45] = 1; // another dot
        REQUIRE(connectedComponentFilter(mask.data(), 50, 50, 5));
        CHECK(mask[15 * 50 + 15] == 1);
        CHECK(mask[30 * 50 + 30] == 0);
        CHECK(mask[45 * 50 + 45] == 0);
    }

    SECTION("All-foreground mask is a single component (no clearing below minArea)") {
        std::vector<uint8_t> mask(8 * 8, 1);
        REQUIRE(connectedComponentFilter(mask.data(), 8, 8, 64));
        for (const uint8_t v : mask)
            CHECK(v == 1);
    }

    SECTION("All-foreground mask with minArea > count clears everything") {
        std::vector<uint8_t> mask(8 * 8, 1);
        REQUIRE(connectedComponentFilter(mask.data(), 8, 8, 65));
        for (const uint8_t v : mask)
            CHECK(v == 0);
    }
}

TEST_CASE("ChangeDetection percentileThreshold p=0 returns the minimum", "[processing][change_detection]") {
    std::vector<float> values = {10.0f, 5.0f, 20.0f, 3.0f};
    float threshold = 0.0f;
    REQUIRE(percentileThreshold(values.data(), values.size(), 0.0, &threshold));
    CHECK(threshold == 3.0f); // nearest-rank p=0 is the minimum, not the max
    REQUIRE(percentileThreshold(values.data(), values.size(), 100.0, &threshold));
    CHECK(threshold == 20.0f);
}

TEST_CASE("ChangeDetection madChange computes Chi-Square distance for multi-band change", "[processing][change_detection][mad]") {
    constexpr size_t N = 16;
    constexpr int B = 2;

    std::vector<std::vector<float>> beforeBands(B, std::vector<float>(N, 0.0f));
    std::vector<std::vector<float>> afterBands(B, std::vector<float>(N, 0.0f));

    for (size_t i = 0; i < N; ++i) {
        // Linear variation for background to establish positive covariance
        float base = static_cast<float>(i + 1);
        beforeBands[0][i] = base * 10.0f;
        beforeBands[1][i] = base * 20.0f;

        if (i < 14) {
            // Unchanged background (14 pixels)
            afterBands[0][i] = base * 10.0f;
            afterBands[1][i] = base * 20.0f;
        } else {
            // Localized change anomaly (2 pixels)
            afterBands[0][i] = base * 10.0f + 200.0f;
            afterBands[1][i] = base * 20.0f - 100.0f;
        }
    }

    std::vector<const float*> bPtrs = {beforeBands[0].data(), beforeBands[1].data()};
    std::vector<const float*> aPtrs = {afterBands[0].data(), afterBands[1].data()};
    std::vector<float> mag(N, 0.0f);

    QString err;
    REQUIRE(madChange(bPtrs.data(), aPtrs.data(), B, N, mag.data(), &err));

    // Verify changed pixels (index 14..15) have significantly higher magnitude than unchanged (0..13)
    float maxUnchanged = 0.0f;
    for (size_t i = 0; i < 14; ++i) {
        if (mag[i] > maxUnchanged) maxUnchanged = mag[i];
    }

    float minChanged = 1e9f;
    for (size_t i = 14; i < 16; ++i) {
        if (mag[i] < minChanged) minChanged = mag[i];
    }

    CHECK(minChanged > maxUnchanged);
}


// ---------------------------------------------------------------------------
// Streaming MAD primitives: memory-bounded multi-pass pipeline
// ---------------------------------------------------------------------------

namespace {

/// Synthesize a deterministic before/after MAD pair: @p bands bands, a smooth
/// gradient background, a changed region (x in [width/4, width/2]) in the
/// after image, and NaN sprinkled on every 97th pixel of every band (which
/// makes those pixels invalid under the all-bands-finite MAD predicate).
void writeMadTestPair(const QString &beforePath, const QString &afterPath,
                      int width, int height, int bands)
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    const auto writeRaster = [&](const QString &path, bool withChange) {
        GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(),
                                     width, height, bands, GDT_Float32, nullptr);
        REQUIRE(ds != nullptr);
        std::vector<float> buf(static_cast<size_t>(width) * height);
        for (int b = 0; b < bands; ++b) {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t i = static_cast<size_t>(y) * width + x;
                    double v = x * 0.5 + y * 0.25 + b * 3.0;
                    if (withChange && x >= width / 4 && x < width / 2)
                        v += 40.0 + 10.0 * b;
                    if (i % 97 == 0)
                        v = std::numeric_limits<float>::quiet_NaN();
                    buf[i] = static_cast<float>(v);
                }
            }
            GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
            REQUIRE(band != nullptr);
            REQUIRE(GDALRasterIO(band, GF_Write, 0, 0, width, height,
                                 buf.data(), width, height, GDT_Float32, 0, 0) == CE_None);
        }
        GDALClose(ds);
    };

    writeRaster(beforePath, false);
    writeRaster(afterPath, true);
}

/// Drive the streaming MAD primitives over 256x256 tile windows of @p beforeDs /
/// @p afterDs (edge tiles clamped), mirroring the operator's pass structure:
/// pass 1 (sums) -> finalizeMeans -> pass 2 (centered products) -> finalize ->
/// pass 3 (transform). Returns the per-pixel chi-square output.
std::vector<float> runStreamingMad(const GdalDatasetWrapper &beforeDs,
                                   const GdalDatasetWrapper &afterDs,
                                   int width, int height, int bands)
{
    constexpr int tile = 256;
    const size_t maxTilePixels = static_cast<size_t>(tile) * tile;
    const size_t B = static_cast<size_t>(bands);
    std::vector<float> beforeBip(maxTilePixels * B);
    std::vector<float> afterBip(maxTilePixels * B);
    std::vector<float> scratch(maxTilePixels);
    std::vector<float> tileOut(maxTilePixels);

    const auto readTile = [&](int x, int y, int tw, int th) {
        const size_t n = static_cast<size_t>(tw) * th;
        for (int b = 0; b < bands; ++b) {
            REQUIRE(beforeDs.readBandWindow(b + 1, x, y, tw, th, scratch.data()));
            for (size_t p = 0; p < n; ++p)
                beforeBip[p * B + static_cast<size_t>(b)] = scratch[p];
            REQUIRE(afterDs.readBandWindow(b + 1, x, y, tw, th, scratch.data()));
            for (size_t p = 0; p < n; ++p)
                afterBip[p * B + static_cast<size_t>(b)] = scratch[p];
        }
    };

    MadStreamingState state;
    QString err;
    for (int y = 0; y < height; y += tile) {
        const int th = std::min(tile, height - y);
        for (int x = 0; x < width; x += tile) {
            const int tw = std::min(tile, width - x);
            readTile(x, y, tw, th);
            REQUIRE(madAccumulateSums(beforeBip.data(), afterBip.data(),
                                      static_cast<size_t>(tw) * th, bands, &state));
        }
    }
    REQUIRE(madFinalizeMeans(&state, &err));

    for (int y = 0; y < height; y += tile) {
        const int th = std::min(tile, height - y);
        for (int x = 0; x < width; x += tile) {
            const int tw = std::min(tile, width - x);
            readTile(x, y, tw, th);
            REQUIRE(madAccumulateCentered(beforeBip.data(), afterBip.data(),
                                          static_cast<size_t>(tw) * th, bands, &state));
        }
    }
    REQUIRE(madFinalize(&state, &err));

    std::vector<float> out(static_cast<size_t>(width) * height, 0.0f);
    for (int y = 0; y < height; y += tile) {
        const int th = std::min(tile, height - y);
        for (int x = 0; x < width; x += tile) {
            const int tw = std::min(tile, width - x);
            const size_t n = static_cast<size_t>(tw) * th;
            readTile(x, y, tw, th);
            madTransformTile(beforeBip.data(), afterBip.data(), n, bands, state, tileOut.data());
            for (int dy = 0; dy < th; ++dy) {
                std::copy_n(tileOut.data() + static_cast<size_t>(dy) * tw, tw,
                            out.data() + static_cast<size_t>(y + dy) * width + x);
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("MAD streaming peak memory is independent of raster size", "[processing][change_detection][mad][memory]") {
#if defined(__linux__) || defined(__APPLE__)
    // Keep GDAL's internal block cache out of the measurement: the streaming
    // pipeline itself must not allocate proportionally to the raster extent.
    GDALSetCacheMax(16 * 1024 * 1024);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    const auto runOver = [&](int width, int height) {
        const QString beforePath = tmp.path() + "/mem_before.tif";
        const QString afterPath = tmp.path() + "/mem_after.tif";
        writeMadTestPair(beforePath, afterPath, width, height, 3);
        GdalDatasetWrapper beforeDs, afterDs;
        REQUIRE(beforeDs.open(beforePath));
        REQUIRE(afterDs.open(afterPath));
        const std::vector<float> out = runStreamingMad(beforeDs, afterDs, width, height, 3);
        REQUIRE(out.size() == static_cast<size_t>(width) * height);
    };

    const auto peakRssKiB = []() -> double {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
        return static_cast<double>(usage.ru_maxrss) / 1024.0; // macOS: bytes -> KiB
#else
        return static_cast<double>(usage.ru_maxrss); // Linux: KiB
#endif
    };

    // Small run first (baseline), then a run with 64x the pixels.
    runOver(256, 256);
    const double rssBefore = peakRssKiB();
    runOver(2048, 2048);
    const double rssAfter = peakRssKiB();

    // ru_maxrss is a process high-water mark, so the delta is the growth the
    // large run caused. A full-scene O(N*bands) MAD would add hundreds of MiB
    // at 3 bands (e.g. the X_mat/Y_mat doubles alone); the streaming pipeline
    // stays within a few tile buffers and the bands^2 state.
    INFO("peak RSS before: " << rssBefore << " KiB, after: " << rssAfter << " KiB");
    CHECK(rssAfter - rssBefore < 128.0 * 1024.0); // growth < 128 MiB
#else
    SUCCEED("RSS measurement requires Linux or macOS; no-op elsewhere");
#endif
}

TEST_CASE("MAD streaming tiles match the legacy madChange result", "[processing][change_detection][mad]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 512, H = 512, B = 3;
    const QString beforePath = tmp.path() + "/eq_before.tif";
    const QString afterPath = tmp.path() + "/eq_after.tif";
    writeMadTestPair(beforePath, afterPath, W, H, B);

    GdalDatasetWrapper beforeDs, afterDs;
    REQUIRE(beforeDs.open(beforePath));
    REQUIRE(afterDs.open(afterPath));

    // Reference: legacy full-scene madChange over band-major buffers.
    const size_t pixels = static_cast<size_t>(W) * H;
    std::vector<std::vector<float>> beforeBands(B, std::vector<float>(pixels));
    std::vector<std::vector<float>> afterBands(B, std::vector<float>(pixels));
    std::vector<const float *> bPtrs(B), aPtrs(B);
    for (int b = 0; b < B; ++b) {
        REQUIRE(beforeDs.readBandData(b + 1, beforeBands[b].data(), W, H));
        REQUIRE(afterDs.readBandData(b + 1, afterBands[b].data(), W, H));
        bPtrs[b] = beforeBands[b].data();
        aPtrs[b] = afterBands[b].data();
    }
    std::vector<float> reference(pixels);
    QString err;
    REQUIRE(madChange(bPtrs.data(), aPtrs.data(), B, pixels, reference.data(), &err));

    // Streaming: the same scene through 256x256 tiles.
    const std::vector<float> streamed = runStreamingMad(beforeDs, afterDs, W, H, B);
    REQUIRE(streamed.size() == pixels);

    size_t nanCount = 0;
    for (size_t p = 0; p < pixels; ++p) {
        if (std::isnan(reference[p])) {
            ++nanCount;
            CHECK(std::isnan(streamed[p]));
        } else {
            // The two runs share the covariance/SVD math and differ only in
            // summation order across tile boundaries; margin covers near-zero
            // chi-square values, epsilon the large ones.
            CHECK(streamed[p] == Approx(reference[p]).margin(1e-3f).epsilon(1e-4f));
        }
    }
    CHECK(nanCount > 0); // the NaN sprinkling was exercised
}

// ---------------------------------------------------------------------------
// Phase 2 Change Detection & Extended Indices Tests
// ---------------------------------------------------------------------------

TEST_CASE("ChangeDetection cvaMagnitudeAndAngle computes 4 quadrants correctly", "[processing][change_detection][cva]") {
    // 4 pixels corresponding to 4 quadrants:
    // Pixel 0: dx1 = 3, dx2 = 4 (Q1) -> mag = 5, angle = atan2(4, 3) > 0
    // Pixel 1: dx1 = -3, dx2 = 4 (Q2) -> mag = 5, angle = atan2(4, -3) > pi/2
    // Pixel 2: dx1 = -3, dx2 = -4 (Q3) -> mag = 5, angle = atan2(-4, -3) < -pi/2
    // Pixel 3: dx1 = 3, dx2 = -4 (Q4) -> mag = 5, angle = atan2(-4, 3) < 0
    const float b1[] = {10.0f, 10.0f, 10.0f, 10.0f};
    const float b2[] = {20.0f, 20.0f, 20.0f, 20.0f};
    const float a1[] = {13.0f,  7.0f,  7.0f, 13.0f};
    const float a2[] = {24.0f, 24.0f, 16.0f, 16.0f};

    float mag[4] = {};
    float angle[4] = {};
    uint8_t quad[4] = {};
    QString err;

    REQUIRE(cvaMagnitudeAndAngle(b1, b2, a1, a2, 4, mag, angle, &err));
    REQUIRE(cvaQuadrant(b1, b2, a1, a2, 4, quad, &err));

    for (int i = 0; i < 4; ++i) {
        CHECK(mag[i] == Approx(5.0f));
    }

    CHECK(angle[0] == Approx(std::atan2(4.0, 3.0)));
    CHECK(angle[1] == Approx(std::atan2(4.0, -3.0)));
    CHECK(angle[2] == Approx(std::atan2(-4.0, -3.0)));
    CHECK(angle[3] == Approx(std::atan2(-4.0, 3.0)));

    CHECK(quad[0] == 1);
    CHECK(quad[1] == 2);
    CHECK(quad[2] == 3);
    CHECK(quad[3] == 4);

    SECTION("NaN handling") {
        const float b1_nan[] = {std::numeric_limits<float>::quiet_NaN(), 10.0f};
        const float b2_nan[] = {20.0f, 20.0f};
        const float a1_nan[] = {13.0f, 7.0f};
        const float a2_nan[] = {24.0f, std::numeric_limits<float>::quiet_NaN()};
        float m[2] = {}, ang[2] = {};
        uint8_t q[2] = {};
        REQUIRE(cvaMagnitudeAndAngle(b1_nan, b2_nan, a1_nan, a2_nan, 2, m, ang, &err));
        REQUIRE(cvaQuadrant(b1_nan, b2_nan, a1_nan, a2_nan, 2, q, &err));
        CHECK(std::isnan(m[0]));
        CHECK(std::isnan(ang[0]));
        CHECK(q[0] == 255);
        CHECK(std::isnan(m[1]));
        CHECK(std::isnan(ang[1]));
        CHECK(q[1] == 255);
    }
}

TEST_CASE("ChangeDetection samChangeAngle computes spectral angle accurately", "[processing][change_detection][sam]") {
    SECTION("Scale invariance (pure illumination change)") {
        const float b0[] = {10.0f, 20.0f, 30.0f};
        const float b1[] = {40.0f, 50.0f, 60.0f};
        const float a0[] = {20.0f, 40.0f, 60.0f}; // 2x b0
        const float a1[] = {80.0f, 100.0f, 120.0f}; // 2x b1
        const float* before[] = {b0, b1};
        const float* after[] = {a0, a1};
        float angles[3] = {};
        QString err;
        REQUIRE(samChangeAngle(before, after, 2, 3, angles, &err));
        for (int i = 0; i < 3; ++i) {
            CHECK(angles[i] == Approx(0.0f).margin(1e-5f));
        }
    }

    SECTION("Known geometric angles") {
        // Pixel 0: orthogonal [1, 0] vs [0, 1] -> pi/2
        // Pixel 1: 45 deg [1, 0] vs [1, 1] -> pi/4
        // Pixel 2: identical [5, 5] vs [5, 5] -> 0
        const float b0[] = {1.0f, 1.0f, 5.0f};
        const float b1[] = {0.0f, 0.0f, 5.0f};
        const float a0[] = {0.0f, 1.0f, 5.0f};
        const float a1[] = {1.0f, 1.0f, 5.0f};
        const float* before[] = {b0, b1};
        const float* after[] = {a0, a1};
        float angles[3] = {};
        QString err;
        REQUIRE(samChangeAngle(before, after, 2, 3, angles, &err));
        CHECK(angles[0] == Approx(static_cast<float>(M_PI / 2.0)).margin(1e-4f));
        CHECK(angles[1] == Approx(static_cast<float>(M_PI / 4.0)).margin(1e-4f));
        CHECK(angles[2] == Approx(0.0f).margin(1e-5f));
    }

    SECTION("NaN in any band gives NaN") {
        const float b0[] = {std::numeric_limits<float>::quiet_NaN()};
        const float b1[] = {1.0f};
        const float a0[] = {1.0f};
        const float a1[] = {1.0f};
        const float* before[] = {b0, b1};
        const float* after[] = {a0, a1};
        float angles[1] = {};
        QString err;
        REQUIRE(samChangeAngle(before, after, 2, 1, angles, &err));
        CHECK(std::isnan(angles[0]));
    }
}

TEST_CASE("ChangeDetection logRatio computes symmetric SAR change", "[processing][change_detection][log_ratio]") {
    const float before[] = {10.0f, 100.0f, 50.0f, std::numeric_limits<float>::quiet_NaN()};
    const float after[]  = {100.0f, 10.0f, 50.0f, 50.0f};
    float out[4] = {};
    REQUIRE(logRatio(before, after, out, 4, 1e-4f));
    CHECK(out[0] == Approx(std::log(100.0001) - std::log(10.0001)).margin(1e-3f));
    CHECK(out[1] == Approx(std::log(10.0001) - std::log(100.0001)).margin(1e-3f));
    CHECK(out[2] == Approx(0.0f).margin(1e-4f));
    CHECK(std::isnan(out[3]));
}

TEST_CASE("ChangeDetection kittlerIllingworthThreshold separates skewed bimodal mixtures", "[processing][change_detection][ki_met]") {
    // Mixture: 90% background around 5 (stddev 1), 10% change around 25 (stddev 2)
    std::vector<float> data;
    data.reserve(1000);
    for (int i = 0; i < 900; ++i) {
        float val = 4.0f + 2.0f * (static_cast<float>(i % 100) / 100.0f);
        data.push_back(val);
    }
    for (int i = 0; i < 100; ++i) {
        float val = 22.0f + 6.0f * (static_cast<float>(i % 50) / 50.0f);
        data.push_back(val);
    }

    float tKi = 0.0f;
    float tOtsu = 0.0f;
    REQUIRE(kittlerIllingworthThreshold(data.data(), data.size(), &tKi));
    REQUIRE(otsuThreshold(data.data(), data.size(), &tOtsu));

    // Both should find a threshold in the valley between 6 and 22
    CHECK(tKi >= 6.0f);
    CHECK(tKi <= 22.0f);
    CHECK(tOtsu >= 6.0f);
    CHECK(tOtsu <= 22.0f);
}

TEST_CASE("ChangeDetection irMadChange converges on multi-band scenes", "[processing][change_detection][irmad]") {
    constexpr size_t N = 64;
    constexpr int B = 3;

    std::vector<std::vector<float>> beforeBands(B, std::vector<float>(N, 0.0f));
    std::vector<std::vector<float>> afterBands(B, std::vector<float>(N, 0.0f));

    for (size_t i = 0; i < N; ++i) {
        float x = static_cast<float>(i + 1);
        beforeBands[0][i] = x * 2.0f;
        beforeBands[1][i] = x * 3.0f + 1.0f;
        beforeBands[2][i] = x * 1.5f + 4.0f;

        if (i < 58) {
            // Unchanged with small noise
            afterBands[0][i] = x * 2.0f + 0.1f * (i % 3);
            afterBands[1][i] = x * 3.0f + 1.0f - 0.1f * (i % 2);
            afterBands[2][i] = x * 1.5f + 4.0f + 0.05f * (i % 4);
        } else {
            // Strong multi-band change
            afterBands[0][i] = x * 2.0f + 50.0f;
            afterBands[1][i] = x * 3.0f - 40.0f;
            afterBands[2][i] = x * 1.5f + 30.0f;
        }
    }

    std::vector<const float*> bPtrs = {beforeBands[0].data(), beforeBands[1].data(), beforeBands[2].data()};
    std::vector<const float*> aPtrs = {afterBands[0].data(), afterBands[1].data(), afterBands[2].data()};
    std::vector<float> chiSq(N, 0.0f);
    QString err;

    REQUIRE(irMadChange(bPtrs.data(), aPtrs.data(), B, N, chiSq.data(), 20, 1e-4, &err));

    float maxUnchanged = 0.0f;
    for (size_t i = 0; i < 58; ++i) {
        if (chiSq[i] > maxUnchanged) maxUnchanged = chiSq[i];
    }
    float minChanged = 1e9f;
    for (size_t i = 58; i < N; ++i) {
        if (chiSq[i] < minChanged) minChanged = chiSq[i];
    }

    CHECK(minChanged > maxUnchanged);
}

TEST_CASE("ChangeDetection::irMadChange Degenerate Input Does Not Crash", "[change_detection][irmad]") {
    constexpr size_t N = 16;
    constexpr int B = 2;

    std::vector<std::vector<float>> beforeBands(B, std::vector<float>(N, 0.0f));
    std::vector<std::vector<float>> afterBands(B, std::vector<float>(N, 0.0f));

    std::vector<const float*> bPtrs = {beforeBands[0].data(), beforeBands[1].data()};
    std::vector<const float*> aPtrs = {afterBands[0].data(), afterBands[1].data()};
    std::vector<float> chiSq(N, 0.0f);
    QString err;

    CHECK_FALSE(irMadChange(bPtrs.data(), aPtrs.data(), B, N, chiSq.data(), 20, 1e-4, &err));
}

TEST_CASE("RsSpectralIndexOperator supports extended indices (NBR, dNBR, BSI, NDRE, CI, NDSI, NDTI)", "[operators][spectral_index]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    constexpr int W = 4, H = 4, B = 6;
    const QString rasterPath = tmp.path() + "/synth_multiband.tif";
    const QString postRasterPath = tmp.path() + "/synth_postfire.tif";

    const auto makeRaster = [&](const QString &path, float factor) {
        GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), W, H, B, GDT_Float32, nullptr);
        REQUIRE(ds != nullptr);
        for (int b = 1; b <= B; ++b) {
            std::vector<float> data(W * H);
            for (size_t i = 0; i < W * H; ++i) {
                float baseVal = (b == 1 ? 10.0f : b == 2 ? 20.0f : b == 3 ? 30.0f : b == 4 ? 80.0f : b == 5 ? 50.0f : 40.0f);
                data[i] = baseVal * factor;
            }
            GDALRasterBandH band = GDALGetRasterBand(ds, b);
            REQUIRE(GDALRasterIO(band, GF_Write, 0, 0, W, H, data.data(), W, H, GDT_Float32, 0, 0) == CE_None);
        }
        GDALClose(ds);
    };

    makeRaster(rasterPath, 1.0f);
    makeRaster(postRasterPath, 0.5f);

    sicnu::operators::rs::RsSpectralIndexOperator op;
    sicnu::operators::RSOperatorContext ctx;

    const auto testIndex = [&](const std::string &idxName, const std::string &outName) {
        Json::Value params(Json::objectValue);
        params["input"] = rasterPath.toStdString();
        params["output"] = (tmp.path() + "/" + QString::fromStdString(outName)).toStdString();
        params["index"] = idxName;
        params["blue"] = 1;
        params["green"] = 2;
        params["red"] = 3;
        params["nir"] = 4;
        params["swir"] = 5;
        params["swir2"] = 6;
        params["rededge"] = 5;
        if (idxName == "dNBR") {
            params["postfire"] = postRasterPath.toStdString();
        }
        Json::Value res = op.run(params, ctx);
        CHECK(res["index"].asString() == idxName);
        CHECK(res["width"].asInt() == W);
        CHECK(res["height"].asInt() == H);
    };

    testIndex("NBR", "nbr.tif");
    testIndex("dNBR", "dnbr.tif");
    testIndex("BSI", "bsi.tif");
    testIndex("NDRE", "ndre.tif");
    testIndex("CI", "ci.tif");
    testIndex("NDSI", "ndsi.tif");
    testIndex("NDTI", "ndti.tif");
}

TEST_CASE("New change primitive operators execute and output valid rasters", "[operators][change_primitives]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    constexpr int W = 4, H = 4, B = 2;
    const QString beforePath = tmp.path() + "/prim_before.tif";
    const QString afterPath = tmp.path() + "/prim_after.tif";

    const auto make2BandRaster = [&](const QString &path, float b1Val, float b2Val) {
        GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), W, H, B, GDT_Float32, nullptr);
        REQUIRE(ds != nullptr);
        std::vector<float> data1(W * H), data2(W * H);
        for (int i = 0; i < W * H; ++i) {
            data1[i] = b1Val + static_cast<float>(i + 1);
            data2[i] = b2Val + static_cast<float>(i * 2 + 1);
        }
        GDALRasterBandH band1 = GDALGetRasterBand(ds, 1);
        GDALRasterBandH band2 = GDALGetRasterBand(ds, 2);
        REQUIRE(GDALRasterIO(band1, GF_Write, 0, 0, W, H, data1.data(), W, H, GDT_Float32, 0, 0) == CE_None);
        REQUIRE(GDALRasterIO(band2, GF_Write, 0, 0, W, H, data2.data(), W, H, GDT_Float32, 0, 0) == CE_None);
        GDALClose(ds);
    };

    make2BandRaster(beforePath, 10.0f, 20.0f);
    make2BandRaster(afterPath, 15.0f, 25.0f);

    sicnu::operators::RSOperatorContext ctx;

    SECTION("RsChangeCvaAngleOperator angle & quadrant") {
        sicnu::operators::rs::RsChangeCvaAngleOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/cva_angle.tif").toStdString();
        params["mode"] = "angle";
        Json::Value res = op.run(params, ctx);
        CHECK(res["method"].asString() == "cva_angle");

        params["output"] = (tmp.path() + "/cva_quad.tif").toStdString();
        params["mode"] = "quadrant";
        Json::Value resQ = op.run(params, ctx);
        CHECK(resQ["mode"].asString() == "quadrant");
    }

    SECTION("RsChangeSamOperator") {
        sicnu::operators::rs::RsChangeSamOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/sam.tif").toStdString();
        Json::Value res = op.run(params, ctx);
        CHECK(res["method"].asString() == "sam");
    }

    SECTION("RsChangeLogRatioOperator") {
        sicnu::operators::rs::RsChangeLogRatioOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/log_ratio.tif").toStdString();
        Json::Value res = op.run(params, ctx);
        CHECK(res["method"].asString() == "log_ratio");
    }

    SECTION("RsChangeIrMadOperator") {
        sicnu::operators::rs::RsChangeIrMadOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/irmad.tif").toStdString();
        params["maxIterations"] = 5;
        Json::Value res = op.run(params, ctx);
        CHECK(res["method"].asString() == "irmad");
    }
}

// ===========================================================================
// Regression tests for #679 (atoms mask declared NoData) and #700 (area units)
// ===========================================================================

TEST_CASE("Change primitive atoms mask declared NoData to NaN (#679)", "[operators][change_primitives][679]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    // 8 pixels; after masking 2, IR-MAD still has N=6 >= bandCount+2 valid.
    constexpr int W = 8, H = 1, B = 2;
    const float ND = -9999.0f;
    const QString beforePath = tmp.path() + "/nd_before.tif";
    const QString afterPath = tmp.path() + "/nd_after.tif";

    const auto makeRaster = [&](const QString &path,
                                const std::vector<std::vector<float>> &bands) {
        GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), W, H, B, GDT_Float32, nullptr);
        REQUIRE(ds != nullptr);
        for (int b = 0; b < B; ++b) {
            GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
            REQUIRE(GDALSetRasterNoDataValue(band, ND) == CE_None);
            REQUIRE(GDALRasterIO(band, GF_Write, 0, 0, W, H,
                                 const_cast<float *>(bands[b].data()), W, H, GDT_Float32, 0, 0) == CE_None);
        }
        GDALClose(ds);
    };

    // Pixel 0: NoData in before band 1. Pixel 1: NoData in after band 2.
    std::vector<std::vector<float>> before = {
        { ND, 12.f, 11.f, 12.f, 10.f, 11.f, 12.f, 10.f },
        { 20.f, 21.f, 20.f, 22.f, 21.f, 20.f, 22.f, 21.f } };
    std::vector<std::vector<float>> after = {
        { 11.f, 12.f, 12.f, 11.f, 12.f, 11.f, 12.f, 11.f },
        { 21.f, ND, 21.f, 20.f, 21.f, 21.f, 20.f, 21.f } };
    makeRaster(beforePath, before);
    makeRaster(afterPath, after);

    sicnu::operators::RSOperatorContext ctx;
    const auto readBand = [&](const QString &path, std::vector<float> &buf) {
        GdalDatasetWrapper out;
        REQUIRE(out.open(path));
        REQUIRE(out.bandCount() == 1);
        buf.assign(W, 0.0f);
        REQUIRE(out.readBandData(1, buf.data(), W, H));
    };

    SECTION("RsChangeSamOperator") {
        sicnu::operators::rs::RsChangeSamOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/nd_sam.tif").toStdString();
        Json::Value res = op.run(params, ctx);
        std::vector<float> out;
        readBand(QString::fromStdString(res["output"].asString()), out);
        // Declared-NoData pixels must be NaN, not angles computed from -9999.
        CHECK(std::isnan(out[0]));
        CHECK(std::isnan(out[1]));
        // Unmasked pixels stay finite.
        CHECK(std::isfinite(out[2]));
        CHECK(std::isfinite(out[3]));
    }

    SECTION("RsChangeCvaAngleOperator") {
        sicnu::operators::rs::RsChangeCvaAngleOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/nd_cva.tif").toStdString();
        Json::Value res = op.run(params, ctx);
        std::vector<float> out;
        readBand(QString::fromStdString(res["output"].asString()), out);
        // Pixels 2/3 are fully valid; 0/1 each touch a declared sentinel.
        CHECK(std::isnan(out[0]));
        CHECK(std::isnan(out[1]));
        CHECK(std::isfinite(out[2]));
    }

    SECTION("RsChangeLogRatioOperator") {
        sicnu::operators::rs::RsChangeLogRatioOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/nd_lr.tif").toStdString();
        Json::Value res = op.run(params, ctx);
        std::vector<float> out;
        readBand(QString::fromStdString(res["output"].asString()), out);
        // before band 1 pixel 0 is the declared sentinel -> NaN, not ln of it.
        CHECK(std::isnan(out[0]));
        CHECK(std::isfinite(out[1]));
    }

    SECTION("RsChangeIrMadOperator") {
        sicnu::operators::rs::RsChangeIrMadOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/nd_irmad.tif").toStdString();
        params["maxIterations"] = 3;
        Json::Value res = op.run(params, ctx);
        std::vector<float> out;
        readBand(QString::fromStdString(res["output"].asString()), out);
        // IR-MAD excludes non-finite pixels from the fit and NaN-fills them.
        CHECK(std::isnan(out[0]));
        CHECK(std::isnan(out[1]));
        CHECK(std::isfinite(out[2]));
        CHECK(std::isfinite(out[3]));
    }
}

TEST_CASE("Change mask reports changedArea in m2 for geographic CRS (#700)", "[operators][change_primitives][700]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    constexpr int W = 4, H = 4;
    // Geographic CRS (EPSG:4326-style WKT), 0.001-degree pixels.
    const char *geogWkt =
        "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
        "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]";
    std::array<double, 6> gt = { 0.0, 0.001, 0.0, 50.0, 0.0, -0.001 };

    const QString beforePath = tmp.path() + "/geo_before.tif";
    const QString afterPath = tmp.path() + "/geo_after.tif";
    std::vector<float> before(W * H, 10.0f);
    std::vector<float> after(W * H, 10.0f);
    after[0] = 50.0f;  // one strong change

    const auto makeGeoRaster = [&](const QString &path, const std::vector<float> &data) {
        GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr);
        REQUIRE(ds != nullptr);
        REQUIRE(GDALSetGeoTransform(ds, const_cast<double *>(gt.data())) == CE_None);
        REQUIRE(GDALSetProjection(ds, geogWkt) == CE_None);
        REQUIRE(GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Write, 0, 0, W, H,
                             const_cast<float *>(data.data()), W, H, GDT_Float32, 0, 0) == CE_None);
        GDALClose(ds);
    };
    makeGeoRaster(beforePath, before);
    makeGeoRaster(afterPath, after);

    sicnu::operators::rs::RsChangeDetectionOperator op;
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = (tmp.path() + "/geo_mask.tif").toStdString();
    params["method"] = "difference";
    params["makeMask"] = true;
    params["thresholdMethod"] = "manual";
    params["threshold"] = 10.0;
    Json::Value res = op.run(params, ctx);

    REQUIRE(res.isMember("changedArea"));
    // The unit is always square metres now.
    REQUIRE(res.isMember("changedAreaUnit"));
    CHECK(res["changedAreaUnit"].asString() == "m2");

    const double changedPixels = res["changedPixels"].asDouble();
    REQUIRE(changedPixels > 0.0);

    // Per-pixel area via the same scene-centre geodesic approximation the
    // operator uses (arc lengths at the centre latitude).
    const double phiDeg = gt[3] + (H / 2.0) * gt[5];
    const double phiRad = phiDeg * 0.017453292519943295;
    const double mPerDegLat =
        111132.92 - 559.82 * std::cos(2 * phiRad) + 1.175 * std::cos(4 * phiRad);
    const double mPerDegLon = 111412.84 * std::cos(phiRad) - 93.5 * std::cos(3 * phiRad);
    const double expectedPixelArea =
        std::abs(gt[1]) * mPerDegLon * std::abs(gt[5]) * mPerDegLat;

    const double perPixel = res["changedArea"].asDouble() / changedPixels;
    CHECK(perPixel == Catch::Approx(expectedPixelArea).margin(expectedPixelArea * 0.01));
    // Guard against a regression to square degrees (0.001^2 = 1e-6 per pixel).
    CHECK(perPixel > 1000.0);
}

// ===========================================================================
// Tile-streaming conversion of the change atoms (#691): log_ratio, cva_angle,
// sam, irmad must reproduce the full-frame kernels from change_detection.cpp
// exactly (masked NoData -> NaN), across 256-tile boundaries too.
// ===========================================================================

namespace {

constexpr float kAtomNoData = -9999.0f;

/// Deterministic before/after pair for the streaming-atom tests: smooth
/// gradient background, a changed block (x in [width/4, width/2)) in the after
/// image, NaN on every 97th pixel of every band, and — when @p withNoData — a
/// declared -9999 sentinel on every 23rd pixel of before band 1 / after band 2
/// (metadata + values, the #679 regression shape).
void writeAtomPair(const QString &beforePath, const QString &afterPath,
                   int width, int height, int bands, bool withNoData)
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    const auto writeRaster = [&](const QString &path, bool withChange) {
        GDALDatasetH ds = GDALCreate(driver, path.toUtf8().constData(),
                                     width, height, bands, GDT_Float32, nullptr);
        REQUIRE(ds != nullptr);
        for (int b = 0; b < bands; ++b) {
            std::vector<float> buf(static_cast<size_t>(width) * height);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t i = static_cast<size_t>(y) * width + x;
                    double v = x * 0.5 + y * 0.25 + b * 3.0;
                    if (withChange && x >= width / 4 && x < width / 2)
                        v += 40.0 + 10.0 * b;
                    if (i % 97 == 0)
                        v = std::numeric_limits<float>::quiet_NaN();
                    if (withNoData && b <= 1 && i % 23 == 7)
                        v = kAtomNoData;
                    buf[i] = static_cast<float>(v);
                }
            }
            GDALRasterBandH band = GDALGetRasterBand(ds, b + 1);
            REQUIRE(band != nullptr);
            if (withNoData)
                REQUIRE(GDALSetRasterNoDataValue(band, kAtomNoData) == CE_None);
            REQUIRE(GDALRasterIO(band, GF_Write, 0, 0, width, height,
                                 buf.data(), width, height, GDT_Float32, 0, 0) == CE_None);
        }
        GDALClose(ds);
    };

    writeRaster(beforePath, false);
    writeRaster(afterPath, true);
}

/// Reads all @p bands of @p path with the atoms' masked-read semantics
/// (declared NoData + non-finite -> NaN) into band-major buffers, which is
/// exactly the input convention of the full-frame kernels.
void readMaskedBands(const QString &path, int bands,
                     std::vector<std::vector<float>> &out)
{
    GdalDatasetWrapper ds;
    REQUIRE(ds.open(path));
    out.assign(static_cast<size_t>(bands), {});
    for (int b = 0; b < bands; ++b) {
        out[static_cast<size_t>(b)].assign(
            static_cast<size_t>(ds.width()) * ds.height(), 0.0f);
        REQUIRE(ds.readBandMasked(b + 1, out[static_cast<size_t>(b)].data(),
                                  ds.width(), ds.height()));
    }
}

/// Reads back the single-band Float32 output raster a streaming atom wrote.
std::vector<float> readAtomOutput(const Json::Value &result)
{
    GdalDatasetWrapper outDs;
    REQUIRE(outDs.open(QString::fromStdString(result["output"].asString())));
    REQUIRE(outDs.bandCount() == 1);
    std::vector<float> out(static_cast<size_t>(outDs.width()) * outDs.height(), 0.0f);
    REQUIRE(outDs.readBandData(1, out.data(), outDs.width(), outDs.height()));
    return out;
}

/// NaN-aware streaming-vs-oracle comparison: every oracle NaN must stay NaN in
/// the streamed output, every finite oracle value must match within
/// @p margin / @p epsilon, and the scene must have exercised at least one NaN
/// (i.e. the masked-read propagation actually ran).
void checkStreamingMatchesOracle(const std::vector<float> &out,
                                 const std::vector<float> &oracle,
                                 double margin, double epsilon)
{
    REQUIRE(out.size() == oracle.size());
    size_t nanCount = 0;
    for (size_t i = 0; i < oracle.size(); ++i) {
        if (std::isnan(oracle[i])) {
            ++nanCount;
            CHECK(std::isnan(out[i]));
        } else {
            CHECK(out[i] == Approx(oracle[i]).margin(margin).epsilon(epsilon));
        }
    }
    CHECK(nanCount > 0);
}

/// The result JSON's mean/stddev must describe the oracle raster (streaming
/// Welford vs the kernel's two-pass stats: FP-rounding tolerance only).
void checkResultStats(const Json::Value &result, const std::vector<float> &oracle)
{
    const MathUtils::Stats stats = MathUtils::computeStats(oracle.data(), oracle.size());
    CHECK(result["mean"].asDouble() == Approx(stats.mean).margin(1e-3).epsilon(1e-4));
    CHECK(result["stddev"].asDouble() == Approx(stats.stddev).margin(1e-3).epsilon(1e-4));
}

/// Band-major pointer arrays over masked full-frame reads — the exact shape
/// the full-frame kernels (samChangeAngle / irMadChange) take.
void maskedBandPtrs(const std::vector<std::vector<float>> &bands,
                    std::vector<const float *> &ptrs)
{
    ptrs.clear();
    for (const auto &band : bands)
        ptrs.push_back(band.data());
}

} // namespace

TEST_CASE("rs:change_cva_angle streams tiles and matches the full-frame kernel",
          "[operators][change_primitives][streaming]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 6, H = 4, B = 3;
    const QString beforePath = tmp.path() + "/cva_before.tif";
    const QString afterPath = tmp.path() + "/cva_after.tif";
    writeAtomPair(beforePath, afterPath, W, H, B, /*withNoData=*/true);

    // Oracle: full-frame kernels over masked reads of the same rasters.
    std::vector<std::vector<float>> beforeBands, afterBands;
    readMaskedBands(beforePath, B, beforeBands);
    readMaskedBands(afterPath, B, afterBands);
    const size_t pixels = static_cast<size_t>(W) * H;
    std::vector<float> oracleMag(pixels), oracleAngle(pixels);
    std::vector<uint8_t> oracleQuad(pixels);
    QString err;
    REQUIRE(cvaMagnitudeAndAngle(beforeBands[0].data(), beforeBands[1].data(),
                                 afterBands[0].data(), afterBands[1].data(),
                                 pixels, oracleMag.data(), oracleAngle.data(), &err));
    REQUIRE(cvaQuadrant(beforeBands[0].data(), beforeBands[1].data(),
                        afterBands[0].data(), afterBands[1].data(),
                        pixels, oracleQuad.data(), &err));

    sicnu::operators::rs::RsChangeCvaAngleOperator op;
    sicnu::operators::RSOperatorContext ctx;
    const auto runCva = [&](const std::string &outName, const char *mode) {
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/" + QString::fromStdString(outName)).toStdString();
        params["mode"] = mode;
        return op.run(params, ctx);
    };

    SECTION("angle mode matches cvaMagnitudeAndAngle") {
        Json::Value res = runCva("cva_angle_out.tif", "angle");
        CHECK(res["method"].asString() == "cva_angle");
        CHECK(res["mode"].asString() == "angle");
        CHECK(res["width"].asInt() == W);
        CHECK(res["height"].asInt() == H);
        checkStreamingMatchesOracle(readAtomOutput(res), oracleAngle, 1e-6, 1e-7);
    }

    SECTION("quadrant mode matches cvaQuadrant") {
        Json::Value res = runCva("cva_quad_out.tif", "quadrant");
        CHECK(res["mode"].asString() == "quadrant");
        const std::vector<float> out = readAtomOutput(res);
        REQUIRE(out.size() == pixels);
        size_t noDataCount = 0;
        for (size_t i = 0; i < pixels; ++i) {
            CHECK(out[i] == static_cast<float>(oracleQuad[i]));
            if (oracleQuad[i] == 255)
                ++noDataCount;
        }
        CHECK(noDataCount > 0); // sentinel pixels classified NoData, not quadrant 4
    }
}

TEST_CASE("rs:change_sam streams tiles and matches the full-frame kernel",
          "[operators][change_primitives][streaming]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 6, H = 4, B = 3;
    const QString beforePath = tmp.path() + "/sam_before.tif";
    const QString afterPath = tmp.path() + "/sam_after.tif";
    writeAtomPair(beforePath, afterPath, W, H, B, /*withNoData=*/true);

    std::vector<std::vector<float>> beforeBands, afterBands;
    readMaskedBands(beforePath, B, beforeBands);
    readMaskedBands(afterPath, B, afterBands);
    std::vector<const float *> bPtrs, aPtrs;
    maskedBandPtrs(beforeBands, bPtrs);
    maskedBandPtrs(afterBands, aPtrs);
    const size_t pixels = static_cast<size_t>(W) * H;
    std::vector<float> oracle(pixels);
    QString err;
    REQUIRE(samChangeAngle(bPtrs.data(), aPtrs.data(), B, pixels, oracle.data(), &err));

    sicnu::operators::rs::RsChangeSamOperator op;
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = (tmp.path() + "/sam_out.tif").toStdString();
    Json::Value res = op.run(params, ctx);
    CHECK(res["method"].asString() == "sam");
    CHECK(res["width"].asInt() == W);
    CHECK(res["height"].asInt() == H);

    checkStreamingMatchesOracle(readAtomOutput(res), oracle, 1e-6, 1e-7);
    checkResultStats(res, oracle);
}

TEST_CASE("rs:change_log_ratio streams tiles and matches the full-frame kernel",
          "[operators][change_primitives][streaming]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 6, H = 4, B = 3;
    const QString beforePath = tmp.path() + "/lr_before.tif";
    const QString afterPath = tmp.path() + "/lr_after.tif";
    writeAtomPair(beforePath, afterPath, W, H, B, /*withNoData=*/true);

    std::vector<std::vector<float>> beforeBands, afterBands;
    readMaskedBands(beforePath, B, beforeBands);
    readMaskedBands(afterPath, B, afterBands);
    const size_t pixels = static_cast<size_t>(W) * H;
    std::vector<float> oracle(pixels);
    REQUIRE(logRatio(beforeBands[0].data(), afterBands[0].data(),
                     oracle.data(), pixels, 1e-4f));

    sicnu::operators::rs::RsChangeLogRatioOperator op;
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = (tmp.path() + "/lr_out.tif").toStdString();
    Json::Value res = op.run(params, ctx);
    CHECK(res["method"].asString() == "log_ratio");
    CHECK(res["width"].asInt() == W);
    CHECK(res["height"].asInt() == H);

    checkStreamingMatchesOracle(readAtomOutput(res), oracle, 1e-6, 1e-7);
    checkResultStats(res, oracle);

    SECTION("custom epsilon follows the kernel") {
        std::vector<float> oracleEps(pixels);
        REQUIRE(logRatio(beforeBands[0].data(), afterBands[0].data(),
                         oracleEps.data(), pixels, 0.5f));
        params["output"] = (tmp.path() + "/lr_eps_out.tif").toStdString();
        params["epsilon"] = 0.5;
        Json::Value resEps = op.run(params, ctx);
        checkStreamingMatchesOracle(readAtomOutput(resEps), oracleEps, 1e-6, 1e-7);
    }
}

TEST_CASE("rs:change_irmad streams every pass and matches the full-frame kernel",
          "[operators][change_primitives][streaming]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    constexpr int W = 6, H = 4, B = 3;
    const QString beforePath = tmp.path() + "/irmad_before.tif";
    const QString afterPath = tmp.path() + "/irmad_after.tif";
    writeAtomPair(beforePath, afterPath, W, H, B, /*withNoData=*/true);

    std::vector<std::vector<float>> beforeBands, afterBands;
    readMaskedBands(beforePath, B, beforeBands);
    readMaskedBands(afterPath, B, afterBands);
    std::vector<const float *> bPtrs, aPtrs;
    maskedBandPtrs(beforeBands, bPtrs);
    maskedBandPtrs(afterBands, aPtrs);
    const size_t pixels = static_cast<size_t>(W) * H;
    std::vector<float> oracle(pixels);
    QString err;
    REQUIRE(irMadChange(bPtrs.data(), aPtrs.data(), B, pixels, oracle.data(),
                        20, 1e-4, &err));

    sicnu::operators::rs::RsChangeIrMadOperator op;
    sicnu::operators::RSOperatorContext ctx;
    Json::Value params(Json::objectValue);
    params["before"] = beforePath.toStdString();
    params["after"] = afterPath.toStdString();
    params["output"] = (tmp.path() + "/irmad_out.tif").toStdString();
    params["maxIterations"] = 20;
    params["convThreshold"] = 1e-4;
    Json::Value res = op.run(params, ctx);
    CHECK(res["method"].asString() == "irmad");
    CHECK(res["width"].asInt() == W);
    CHECK(res["height"].asInt() == H);

    // The streamed iteration performs the same accumulations in the same
    // pixel order as the kernel (weights carried as double), so this is
    // near-exact; the margin only covers FP associativity.
    checkStreamingMatchesOracle(readAtomOutput(res), oracle, 1e-3, 1e-4);
    checkResultStats(res, oracle);
}

TEST_CASE("Change atoms stream across 256-tile boundaries (520x300) matching the kernel",
          "[operators][change_primitives][streaming][multi_tile]") {
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // 520 = 2 x 256 + 8, 300 = 256 + 44: partial tiles on both axes.
    constexpr int W = 520, H = 300, B = 3;
    const QString beforePath = tmp.path() + "/tile_before.tif";
    const QString afterPath = tmp.path() + "/tile_after.tif";
    writeAtomPair(beforePath, afterPath, W, H, B, /*withNoData=*/true);

    std::vector<std::vector<float>> beforeBands, afterBands;
    readMaskedBands(beforePath, B, beforeBands);
    readMaskedBands(afterPath, B, afterBands);
    std::vector<const float *> bPtrs, aPtrs;
    maskedBandPtrs(beforeBands, bPtrs);
    maskedBandPtrs(afterBands, aPtrs);
    const size_t pixels = static_cast<size_t>(W) * H;
    sicnu::operators::RSOperatorContext ctx;

    SECTION("cva_angle") {
        std::vector<float> oracleMag(pixels), oracle(pixels);
        QString err;
        REQUIRE(cvaMagnitudeAndAngle(beforeBands[0].data(), beforeBands[1].data(),
                                     afterBands[0].data(), afterBands[1].data(),
                                     pixels, oracleMag.data(), oracle.data(), &err));
        sicnu::operators::rs::RsChangeCvaAngleOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/tile_cva.tif").toStdString();
        Json::Value res = op.run(params, ctx);
        CHECK(res["width"].asInt() == W);
        CHECK(res["height"].asInt() == H);
        checkStreamingMatchesOracle(readAtomOutput(res), oracle, 1e-6, 1e-7);
    }

    SECTION("sam") {
        std::vector<float> oracle(pixels);
        QString err;
        REQUIRE(samChangeAngle(bPtrs.data(), aPtrs.data(), B, pixels, oracle.data(), &err));
        sicnu::operators::rs::RsChangeSamOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/tile_sam.tif").toStdString();
        Json::Value res = op.run(params, ctx);
        checkStreamingMatchesOracle(readAtomOutput(res), oracle, 1e-6, 1e-7);
        checkResultStats(res, oracle);
    }

    SECTION("log_ratio") {
        std::vector<float> oracle(pixels);
        REQUIRE(logRatio(beforeBands[0].data(), afterBands[0].data(),
                         oracle.data(), pixels, 1e-4f));
        sicnu::operators::rs::RsChangeLogRatioOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/tile_lr.tif").toStdString();
        Json::Value res = op.run(params, ctx);
        checkStreamingMatchesOracle(readAtomOutput(res), oracle, 1e-6, 1e-7);
        checkResultStats(res, oracle);
    }

    SECTION("irmad") {
        std::vector<float> oracle(pixels);
        QString err;
        REQUIRE(irMadChange(bPtrs.data(), aPtrs.data(), B, pixels, oracle.data(),
                            20, 1e-4, &err));
        sicnu::operators::rs::RsChangeIrMadOperator op;
        Json::Value params(Json::objectValue);
        params["before"] = beforePath.toStdString();
        params["after"] = afterPath.toStdString();
        params["output"] = (tmp.path() + "/tile_irmad.tif").toStdString();
        params["maxIterations"] = 20;
        params["convThreshold"] = 1e-4;
        Json::Value res = op.run(params, ctx);
        checkStreamingMatchesOracle(readAtomOutput(res), oracle, 1e-3, 1e-4);
        checkResultStats(res, oracle);
    }
}
