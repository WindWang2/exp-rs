// test_gdal_utils.cpp — Tests for GDAL utility functions
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>
#include <cpl_conv.h>

#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <array>
#include <vector>

TEST_CASE("extractGeoInfo", "[gdal_utils]")
{
    ensureGdalInit();

    SECTION("null dataset returns default values")
    {
        GeoInfo info = extractGeoInfo(nullptr);
        REQUIRE(info.projection.isEmpty());
        REQUIRE(info.geoTransform[0] == 0.0);
        REQUIRE(info.geoTransform[1] == 1.0);
        REQUIRE(info.geoTransform[2] == 0.0);
        REQUIRE(info.geoTransform[3] == 0.0);
        REQUIRE(info.geoTransform[4] == 0.0);
        REQUIRE(info.geoTransform[5] == 1.0);
    }

    SECTION("valid dataset returns correct info")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());

        QString path = dir.filePath("test.tif");
        std::array<double, 6> gt = {100.0, 0.5, 0.0, 200.0, 0.0, -0.5};
        QString proj = QStringLiteral("GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");

        GDALDatasetH ds = createOutputTiff(path, 10, 10, 1, GDT_Float32, gt, proj);
        REQUIRE(ds != nullptr);

        GeoInfo info = extractGeoInfo(ds);
        REQUIRE(info.projection.contains("WGS 84"));
        REQUIRE(info.geoTransform[0] == Catch::Approx(100.0));
        REQUIRE(info.geoTransform[1] == Catch::Approx(0.5));
        REQUIRE(info.geoTransform[5] == Catch::Approx(-0.5));

        GDALClose(ds);
    }
}

TEST_CASE("writeGdalOutput", "[gdal_utils]")
{
    ensureGdalInit();

    SECTION("empty bands returns false")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        std::vector<std::vector<float>> bands;
        std::array<double, 6> gt = {0, 1, 0, 0, 0, 1};
        QString error;

        bool ok = writeGdalOutput(dir.filePath("test.tif"), 10, 10, bands, gt, "", &error);
        REQUIRE_FALSE(ok);
        REQUIRE(error.contains("No band data"));
    }

    SECTION("writes single band correctly")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());

        QString path = dir.filePath("test.tif");
        std::array<double, 6> gt = {0, 1, 0, 0, 0, 1};

        // Create test data
        std::vector<float> band1(100, 42.0f);
        std::vector<std::vector<float>> bands = {band1};

        QString error;
        bool ok = writeGdalOutput(path, 10, 10, bands, gt, "", &error);
        REQUIRE(ok);
        REQUIRE(error.isEmpty());

        // Verify the output
        GdalDatasetWrapper ds;
        REQUIRE(ds.open(path));
        REQUIRE(ds.width() == 10);
        REQUIRE(ds.height() == 10);
        REQUIRE(ds.bandCount() == 1);

        float value = 0.0f;
        REQUIRE(ds.readPixel(1, 5, 5, &value));
        REQUIRE(value == Catch::Approx(42.0f));
    }

    SECTION("writes multi-band correctly")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());

        QString path = dir.filePath("test.tif");
        std::array<double, 6> gt = {0, 1, 0, 0, 0, 1};

        // Create test data with 3 bands
        std::vector<float> band1(100, 10.0f);
        std::vector<float> band2(100, 20.0f);
        std::vector<float> band3(100, 30.0f);
        std::vector<std::vector<float>> bands = {band1, band2, band3};

        QString error;
        bool ok = writeGdalOutput(path, 10, 10, bands, gt, "", &error);
        REQUIRE(ok);

        // Verify the output
        GdalDatasetWrapper ds;
        REQUIRE(ds.open(path));
        REQUIRE(ds.bandCount() == 3);

        float value = 0.0f;
        REQUIRE(ds.readPixel(1, 0, 0, &value));
        REQUIRE(value == Catch::Approx(10.0f));

        REQUIRE(ds.readPixel(2, 0, 0, &value));
        REQUIRE(value == Catch::Approx(20.0f));

        REQUIRE(ds.readPixel(3, 0, 0, &value));
        REQUIRE(value == Catch::Approx(30.0f));
    }

    SECTION("preserves geotransform and projection")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());

        QString path = dir.filePath("test.tif");
        std::array<double, 6> gt = {100.0, 0.5, 0.0, 200.0, 0.0, -0.5};
        QString proj = QStringLiteral("GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");

        std::vector<float> band1(100, 1.0f);
        std::vector<std::vector<float>> bands = {band1};

        QString error;
        bool ok = writeGdalOutput(path, 10, 10, bands, gt, proj, &error);
        REQUIRE(ok);

        // Verify geotransform and projection
        GdalDatasetWrapper ds;
        REQUIRE(ds.open(path));

        auto outGt = ds.geoTransform();
        REQUIRE(outGt[0] == Catch::Approx(100.0));
        REQUIRE(outGt[1] == Catch::Approx(0.5));
        REQUIRE(outGt[5] == Catch::Approx(-0.5));

        REQUIRE(ds.projection().contains("WGS 84"));
    }
}
