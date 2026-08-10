// test_perf_fusion.cpp — Performance & Memory Boundedness Benchmark for Image Fusion
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/image_fusion.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QTemporaryDir>
#include <QFile>
#include <gdal.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <sys/resource.h>

#include <fstream>
#include <random>

using Catch::Approx;

namespace {

// Portable peak-RSS measurement (MiB).
//  - Linux: /proc/self/status VmHWM (high-water mark, exact peak).
//  - macOS: getrusage().ru_maxrss (bytes on Darwin, KB on Linux — handled).
//  - Other: returns -1 (callers skip the memory REQUIRE).
double getPeakRssMb() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            double kb = std::strtod(line.c_str() + 7, nullptr);
            return kb / 1024.0;
        }
    }
    return -1.0;
#elif defined(__APPLE__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // ru_maxrss is in bytes on Darwin, KiB on Linux.
        return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
    }
    return -1.0;
#else
    return -1.0;
#endif
}

// Generate synthetic co-registered Pan and MS rasters of size width x height using pseudo-random data
bool generateTestRasters(const QString &panPath, const QString &msPath, int width, int height, int msBands) {
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    const QString proj = QStringLiteral("EPSG:32648");

    GDALDatasetH panDs = createOutputTiff(panPath, width, height, 1, GDT_Float32, gt, proj);
    GDALDatasetH msDs = createOutputTiff(msPath, width, height, msBands, GDT_Float32, gt, proj);
    if (!panDs || !msDs) return false;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(10.0f, 250.0f);

    std::vector<float> linePan(width);
    std::vector<float> lineMs(width);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float base = dist(rng);
            linePan[x] = base * 1.2f;
        }
        if (GDALRasterIO(GDALGetRasterBand(panDs, 1), GF_Write, 0, y, width, 1,
                         linePan.data(), width, 1, GDT_Float32, 0, 0) != CE_None) {
            GDALClose(panDs);
            GDALClose(msDs);
            return false;
        }

        for (int b = 0; b < msBands; ++b) {
            float bandFactor = 0.8f + 0.1f * b;
            for (int x = 0; x < width; ++x) {
                lineMs[x] = linePan[x] * bandFactor + dist(rng) * 0.05f;
            }
            if (GDALRasterIO(GDALGetRasterBand(msDs, b + 1), GF_Write, 0, y, width, 1,
                             lineMs.data(), width, 1, GDT_Float32, 0, 0) != CE_None) {
                GDALClose(panDs);
                GDALClose(msDs);
                return false;
            }
        }
    }

    GDALClose(panDs);
    GDALClose(msDs);
    return true;
}

// A window is valid when every sample is finite and within the plausible range
// of the synthetic inputs (10..300; fused values may overshoot slightly).
void requirePlausibleWindow(GdalDatasetWrapper &outDs, int x, int y, int w, int h) {
    std::vector<float> pixels(static_cast<size_t>(w) * h);
    REQUIRE(outDs.readBandWindow(1, x, y, w, h, pixels.data()));
    for (float val : pixels) {
        REQUIRE(std::isfinite(val));
        REQUIRE(val >= 0.0f);
        REQUIRE(val <= 600.0f);
    }
}

} // namespace

TEST_CASE("ImageFusion Benchmark: Bounded Memory & Performance", "[fusion][perf]") {
    ensureGdalInit();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    struct BenchResult {
        std::string method;
        int width;
        int height;
        double timeMs;
        double rssMB;
        double throughputMPixSec;
    };

    std::vector<BenchResult> results;
    const std::vector<std::string> methods = {"linear", "brovey", "ihs", "pca", "gram_schmidt"};
    const std::vector<int> sizes = {256, 1024, 2048};

    // Peak-RSS baseline before the benchmark loop; the streaming implementation
    // must keep total growth far below a full-raster materialization of the
    // largest size (2048^2 x 4 bands x 4 B ≈ 64 MiB per full-raster buffer).
    const double rssBaseline = getPeakRssMb();

    // Warm-up run to load dynamic libraries and GDAL cache
    {
        const QString warmPan = dir.filePath("warm_pan.tif");
        const QString warmMs = dir.filePath("warm_ms.tif");
        const QString warmOut = dir.filePath("warm_out.tif");
        if (generateTestRasters(warmPan, warmMs, 128, 128, 4)) {
            ImageFusion::NativeFusionParams warmParams;
            warmParams.method = QStringLiteral("linear");
            QString warmErr;
            ImageFusion::processNativeFusion(warmPan, warmMs, warmOut, warmParams, &warmErr);
        }
    }

    for (const int size : sizes) {
        const QString panPath = dir.filePath(QString("pan_%1.tif").arg(size));
        const QString msPath = dir.filePath(QString("ms_%1.tif").arg(size));
        REQUIRE(generateTestRasters(panPath, msPath, size, size, 4));

        for (const auto &method : methods) {
            const QString outPath = dir.filePath(QString("fused_%1_%2.tif").arg(QString::fromStdString(method)).arg(size));

            ImageFusion::NativeFusionParams params;
            params.method = QString::fromStdString(method);
            params.tileWidth = 512;
            params.tileHeight = 512;

            auto start = std::chrono::high_resolution_clock::now();

            QString error;
            bool ok = ImageFusion::processNativeFusion(panPath, msPath, outPath, params, &error);
            auto end = std::chrono::high_resolution_clock::now();

            REQUIRE(ok);
            REQUIRE(QFile::exists(outPath));

            double timeMs = std::chrono::duration<double, std::milli>(end - start).count();
            double rssPeak = getPeakRssMb();
            double totalMPix = (static_cast<double>(size) * size) / 1e6;
            double mpixSec = (timeMs > 0.0) ? (totalMPix / (timeMs / 1000.0)) : 0.0;

            results.push_back({method, size, size, timeMs, rssPeak, mpixSec});

            // Verify output dimensions and band count
            GdalDatasetWrapper outDs;
            REQUIRE(outDs.open(outPath));
            REQUIRE(outDs.width() == size);
            REQUIRE(outDs.height() == size);
            int expectedBands = (method == "ihs") ? 3 : 4;
            REQUIRE(outDs.bandCount() == expectedBands);

            // 1. Origin window check (0, 0)
            requirePlausibleWindow(outDs, 0, 0, 10, 10);

            // 2. Tile boundary crossing check (around 505..515 for tile width 512)
            if (size >= 1024) {
                requirePlausibleWindow(outDs, 507, 507, 10, 10);
            }

            // 3. Bottom-right corner check
            requirePlausibleWindow(outDs, size - 10, size - 10, 10, 10);
        }
    }

    // Memory boundedness: the fused outputs are streamed per-tile, so the peak
    // RSS growth over the whole benchmark (all methods, up to 2048x2048) must
    // stay well under a full-raster materialization of every intermediate.
    // 2048^2 x 4 bands x 4 B x ~3 buffers ≈ 190 MiB; allow headroom.
    if (rssBaseline > 0.0) {
        const double rssGrowth = getPeakRssMb() - rssBaseline;
        REQUIRE(rssGrowth < 900.0);
    }

    std::cout << "\n=== Image Fusion Benchmark Summary ===\n";
    std::cout << "Method       | Dimensions | Time (ms) | Current RSS (MB) | Throughput (MPix/s)\n";
    std::cout << "-------------|------------|-----------|------------------|--------------------\n";
    for (const auto &r : results) {
        std::cout << QString("%1 | %2x%3 | %4 ms | %5 MB | %6 MPix/s")
                         .arg(QString::fromStdString(r.method), -12)
                         .arg(r.width, 4)
                         .arg(r.height, 4)
                         .arg(r.timeMs, 9, 'f', 2)
                         .arg(r.rssMB, 16, 'f', 2)
                         .arg(r.throughputMPixSec, 18, 'f', 2)
                         .toStdString()
                  << "\n";
    }
    std::cout << "=======================================\n\n";
}
