// tests/test_gdal_thread_safety.cpp — TDD for GDAL init thread safety
#include <catch2/catch_test_macros.hpp>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <thread>
#include <vector>
#include <atomic>
#include <QFileInfo>

TEST_CASE("GdalDatasetWrapper concurrent open does not crash", "[gdal][thread]")
{
    // Open the same file from multiple threads concurrently
    // This exercises ensureGdalInit() under concurrent access
    QString path = QFileInfo(__FILE__).absolutePath() + "/../data/sample_crops.tif";

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
    QString path1 = QFileInfo(__FILE__).absolutePath() + "/../data/sample_crops.tif";
    QString path2 = QFileInfo(__FILE__).absolutePath() + "/../data/phr_xs.tif";

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
