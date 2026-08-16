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

    // All threads must succeed since the synthetic raster is valid.
    CHECK(successCount.load() == threadCount);
    CHECK(failCount.load() == 0);
}

TEST_CASE("GdalDatasetWrapper concurrent open and read on distinct files", "[gdal][thread]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const int fileCount = 4;
    std::vector<QString> paths;
    paths.reserve(fileCount);

    GDALDriverH driver = GDALGetDriverByName("GTiff");
    REQUIRE(driver != nullptr);

    // Create 4 distinct rasters, each filled with a unique sentinel value
    for (int i = 0; i < fileCount; ++i) {
        QString p = dir.filePath(QString("raster_%1.tif").arg(i));
        GDALDatasetH ds = GDALCreate(driver, p.toUtf8().constData(), 16, 16, 1, GDT_Float32, nullptr);
        REQUIRE(ds != nullptr);
        GDALRasterBandH band = GDALGetRasterBand(ds, 1);
        std::vector<float> data(16 * 16, static_cast<float>(100.0f + i * 10.0f));
        CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, 16, 16, data.data(), 16, 16, GDT_Float32, 0, 0);
        REQUIRE(err == CE_None);
        GDALClose(ds);
        paths.push_back(p);
    }

    std::atomic<int> successCount(0);
    std::atomic<int> matchCount(0);
    std::vector<std::thread> threads;
    threads.reserve(fileCount);

    for (int i = 0; i < fileCount; ++i) {
        threads.emplace_back([i, &paths, &successCount, &matchCount]() {
            GdalDatasetWrapper ds;
            if (ds.open(paths[i])) {
                successCount++;
                std::vector<float> buf(16 * 16, 0.0f);
                if (ds.readBandData(1, buf.data(), 16, 16)) {
                    float expected = 100.0f + static_cast<float>(i * 10.0f);
                    bool allMatch = true;
                    for (float v : buf) {
                        if (v != expected) {
                            allMatch = false;
                            break;
                        }
                    }
                    if (allMatch)
                        matchCount++;
                }
            }
        });
    }

    for (auto &t : threads)
        t.join();

    CHECK(successCount.load() == fileCount);
    CHECK(matchCount.load() == fileCount);
}
