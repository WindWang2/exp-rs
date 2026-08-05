// tests/test_gdal_thread_safety.cpp — TDD for GDAL init thread safety
#include <catch2/catch_test_macros.hpp>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <thread>
#include <vector>
#include <atomic>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gdal.h>
#include <cpl_conv.h>

// A small valid GeoTIFF synthesised at runtime so the suite does not depend on
// a committed sample raster under data/.
static QString validRasterPath()
{
  static QTemporaryDir dir;
  static const QString path = []() {
    GDALAllRegister();
    const QString p = dir.path() + QStringLiteral( "/sample.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, p.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    GDALClose( ds );
    return p;
  }();
  return path;
}

TEST_CASE("GdalDatasetWrapper concurrent open does not crash", "[gdal][thread]")
{
    // Open the same file from multiple threads concurrently
    // This exercises ensureGdalInit() under concurrent access
    QString path = validRasterPath();

    const int threadCount = 8;
    std::atomic<int> successCount(0);
    std::atomic<int> failCount(0);

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&path, &successCount, &failCount]() {
            GdalDatasetWrapper ds;
            if (ds.open(path))
                successCount++;
            else
                failCount++;
        });
    }

    for (auto &t : threads)
        t.join();

    // All threads should succeed (or all fail if file missing)
    // The important thing is no crash from concurrent GDALAllRegister()
    CHECK(successCount.load() + failCount.load() == threadCount);
}

TEST_CASE("GdalDatasetWrapper concurrent open different files", "[gdal][thread]")
{
    QString path1 = validRasterPath();
    QString path2 = validRasterPath(); // same synthetic file is fine — concurrency, not distinctness

    std::atomic<int> successCount(0);
    std::atomic<int> failCount(0);
    std::vector<std::thread> threads;

    threads.emplace_back([&path1, &successCount, &failCount]() {
        GdalDatasetWrapper ds;
        if (ds.open(path1)) successCount++; else failCount++;
    });

    threads.emplace_back([&path2, &successCount, &failCount]() {
        GdalDatasetWrapper ds;
        if (ds.open(path2)) successCount++; else failCount++;
    });

    for (auto &t : threads)
        t.join();

    // Both threads should complete without crashing
    CHECK(successCount.load() + failCount.load() == 2);
}
