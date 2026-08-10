// tests/test_change_detection.cpp — TDD for change detection algorithm
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/change_detection.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>
#include <QString>

#include <gdal.h>

#include <algorithm>
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
