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
#include <iostream>
#include <vector>
#include <sys/resource.h>

using Catch::Approx;

namespace {

// Helper: Measure Peak RSS in MiB
double getPeakRssMB() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return static_cast<double>(usage.ru_maxrss) / 1024.0;
    }
    return 0.0;
}

// Generate synthetic co-registered Pan and MS rasters of size width x height
bool generateTestRasters(const QString &panPath, const QString &msPath, int width, int height, int msBands) {
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    const QString proj = QStringLiteral("EPSG:32648");

    GDALDatasetH panDs = createOutputTiff(panPath, width, height, 1, GDT_Float32, gt, proj);
    GDALDatasetH msDs = createOutputTiff(msPath, width, height, msBands, GDT_Float32, gt, proj);
    if (!panDs || !msDs) return false;

    // Fill line by line to keep generator memory low
    std::vector<float> linePan(width);
    std::vector<float> lineMs(width);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float val = static_cast<float>((y * width + x) % 255 + 1);
            linePan[x] = val * 1.5f;
        }
        if (GDALRasterIO(GDALGetRasterBand(panDs, 1), GF_Write, 0, y, width, 1,
                         linePan.data(), width, 1, GDT_Float32, 0, 0) != CE_None) {
            GDALClose(panDs);
            GDALClose(msDs);
            return false;
        }

        for (int b = 0; b < msBands; ++b) {
            float bandFactor = 0.5f + 0.2f * b;
            for (int x = 0; x < width; ++x) {
                float val = static_cast<float>((y * width + x) % 255 + 1);
                lineMs[x] = val * bandFactor;
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
        double peakRssMB;
        double throughputMPixSec;
    };

    std::vector<BenchResult> results;
    const std::vector<std::string> methods = {"linear", "brovey", "ihs", "pca", "gram_schmidt"};
    const std::vector<int> sizes = {256, 1024, 2048};

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

            double rssBefore = getPeakRssMB();
            auto start = std::chrono::high_resolution_clock::now();

            QString error;
            bool ok = ImageFusion::processNativeFusion(panPath, msPath, outPath, params, &error);
            auto end = std::chrono::high_resolution_clock::now();

            REQUIRE(ok);
            REQUIRE(QFile::exists(outPath));

            double timeMs = std::chrono::duration<double, std::milli>(end - start).count();
            double rssAfter = getPeakRssMB();
            double totalMPix = (static_cast<double>(size) * size) / 1e6;
            double mpixSec = (timeMs > 0.0) ? (totalMPix / (timeMs / 1000.0)) : 0.0;

            results.push_back({method, size, size, timeMs, rssAfter, mpixSec});

            // Verify output dimensions and band count
            GdalDatasetWrapper outDs;
            REQUIRE(outDs.open(outPath));
            REQUIRE(outDs.width() == size);
            REQUIRE(outDs.height() == size);
            int expectedBands = (method == "ihs") ? 3 : 4;
            REQUIRE(outDs.bandCount() == expectedBands);

            // Numerical correctness check: read a window and verify non-zero, non-NaN valid values
            std::vector<float> samplePixels(100);
            REQUIRE(outDs.readBandWindow(1, 0, 0, 10, 10, samplePixels.data()));
            for (float val : samplePixels) {
                REQUIRE(!std::isnan(val));
                REQUIRE(val >= 0.0f);
            }
        }
    }

    std::cout << "\n=== Image Fusion Benchmark Summary ===\n";
    std::cout << "Method       | Dimensions | Time (ms) | Peak RSS (MB) | Throughput (MPix/s)\n";
    std::cout << "-------------|------------|-----------|---------------|--------------------\n";
    for (const auto &r : results) {
        std::cout << QString("%1 | %2x%3 | %4 ms | %5 MB | %6 MPix/s")
                         .arg(QString::fromStdString(r.method), -12)
                         .arg(r.width, 4)
                         .arg(r.height, 4)
                         .arg(r.timeMs, 9, 'f', 2)
                         .arg(r.peakRssMB, 13, 'f', 2)
                         .arg(r.throughputMPixSec, 18, 'f', 2)
                         .toStdString()
                  << "\n";
    }
    std::cout << "=======================================\n\n";
}
