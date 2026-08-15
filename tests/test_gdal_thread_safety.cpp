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

static QString createSyntheticRaster(const QString &filename)
{
  static QTemporaryDir dir;
  GDALAllRegister();
  const QString p = dir.path() + QStringLiteral( "/" ) + filename;
  if (!QFile::exists(p)) {
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, p.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    GDALClose( ds );
  }
  return p;
}

static QString validRasterPath()
{
  return createSyntheticRaster(QStringLiteral("sample.tif"));
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

    // All threads must succeed opening the valid raster
    CHECK(failCount.load() == 0);
    CHECK(successCount.load() == threadCount);
}

TEST_CASE("GdalDatasetWrapper concurrent open different files", "[gdal][thread]")
{
    QString path1 = createSyntheticRaster(QStringLiteral("sample1.tif"));
    QString path2 = createSyntheticRaster(QStringLiteral("sample2.tif"));

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

    // Both threads must successfully open their distinct files
    CHECK(failCount.load() == 0);
    CHECK(successCount.load() == 2);
}
