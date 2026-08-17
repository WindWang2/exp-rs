// tests/test_gdal_null_band.cpp — TDD for GDAL null band handle checks
#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_error.h>

#include <vector>

static bool gdalRegistered = false;
static void ensureGdalRegistered()
{
    if (!gdalRegistered) {
        GDALAllRegister();
        gdalRegistered = true;
    }
}

TEST_CASE("GDALGetRasterBand returns NULL for zero-band dataset", "[gdal][nullband]")
{
    ensureGdalRegistered();
    GDALDriverH driver = GDALGetDriverByName("MEM");
    REQUIRE(driver != nullptr);

    // Create a dataset with 0 bands
    GDALDatasetH ds = GDALCreate(driver, "", 4, 4, 0, GDT_Byte, nullptr);
    REQUIRE(ds != nullptr);

    // GDALGetRasterBand should return NULL for band 1 on a zero-band dataset
    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    REQUIRE(band == nullptr);

    GDALClose(ds);
}

TEST_CASE("Null band handle causes GDALRasterIO to fail safely", "[gdal][nullband]")
{
    ensureGdalRegistered();
    GDALDriverH driver = GDALGetDriverByName("MEM");
    REQUIRE(driver != nullptr);

    GDALDatasetH ds = GDALCreate(driver, "", 4, 4, 0, GDT_Byte, nullptr);
    REQUIRE(ds != nullptr);

    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    REQUIRE(band == nullptr);

    // Calling GDALRasterIO with NULL band should not crash — it returns CE_Failure
    std::vector<GByte> buf(16);
    CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, 4, 4,
                               buf.data(), 4, 4, GDT_Byte, 0, 0);
    REQUIRE(err != CE_None);

    GDALClose(ds);
}

TEST_CASE("Valid band handle works correctly", "[gdal][nullband]")
{
    ensureGdalRegistered();
    GDALDriverH driver = GDALGetDriverByName("MEM");
    REQUIRE(driver != nullptr);

    // Create a dataset with 1 band — same as dialog code
    GDALDatasetH ds = GDALCreate(driver, "", 4, 4, 1, GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);

    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    REQUIRE(band != nullptr);

    std::vector<float> data(16, 42.0f);
    CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, 4, 4,
                               data.data(), 4, 4, GDT_Float32, 0, 0);
    REQUIRE(err == CE_None);

    GDALClose(ds);
}

#include "processing/gdal/gdal_dataset_wrapper.h"

TEST_CASE("Null band guard prevents write to invalid band", "[gdal][nullband]")
{
    ensureGdalRegistered();
    // This test demonstrates the pattern the dialog code should use:
    // check for null band before calling GDALRasterIO
    GDALDriverH driver = GDALGetDriverByName("MEM");
    REQUIRE(driver != nullptr);

    GDALDatasetH ds = GDALCreate(driver, "", 4, 4, 0, GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);

    GDALRasterBandH band = GDALGetRasterBand(ds, 1);

    // The guard pattern: check for null before writing
    bool wroteOk = false;
    if (band) {
        std::vector<float> data(16, 1.0f);
        CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, 4, 4,
                                   data.data(), 4, 4, GDT_Float32, 0, 0);
        wroteOk = (err == CE_None);
    }

    // Should not have written because band was null
    REQUIRE_FALSE(wroteOk);

    GDALClose(ds);
}

TEST_CASE("GdalDatasetWrapper handles out-of-bounds bands safely", "[gdal][nullband]")
{
    GdalDatasetWrapper wrapper;
    // Closed wrapper operations return false safely
    std::vector<float> buf(16, 0.0f);
    REQUIRE_FALSE(wrapper.readBandData(1, buf.data(), 4, 4));
    REQUIRE_FALSE(wrapper.readBandWindow(1, 0, 0, 4, 4, buf.data()));
    REQUIRE_FALSE(wrapper.writeBandWindow(1, 0, 0, 4, 4, buf.data()));

    bool hasNodata = true;
    double nd = wrapper.bandNoDataValue(1, &hasNodata);
    REQUIRE_FALSE(hasNodata);
}
