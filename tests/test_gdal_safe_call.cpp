// test_gdal_safe_call.cpp — GDAL safe call wrapper tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <processing/gdal/gdal_safe_call.h>
#include <gdal.h>
#include <cpl_error.h>

TEST_CASE("GdalSafeCall success path", "[gdal][robustness]")
{
    SECTION("Succeeds on CE_None")
    {
        bool threw = false;
        try {
            GDAL_SAFE_CALL(CE_None, "Should not fail");
        } catch (...) {
            threw = true;
        }
        REQUIRE_FALSE(threw);
    }
}

TEST_CASE("GdalSafeCall failure path", "[gdal][robustness]")
{
    SECTION("Throws on CE_Failure")
    {
        bool threw = false;
        try {
            GDAL_SAFE_CALL(CE_Failure, "Test error");
        } catch (const std::runtime_error &e) {
            threw = true;
            REQUIRE(std::string(e.what()).find("Test error") != std::string::npos);
        }
        REQUIRE(threw);
    }

    SECTION("Throws on CE_Fatal")
    {
        bool threw = false;
        try {
            GDAL_SAFE_CALL(CE_Fatal, "Fatal error");
        } catch (const std::runtime_error &e) {
            threw = true;
        }
        REQUIRE(threw);
    }
}

TEST_CASE("GdalSafeCall with GDAL operations", "[gdal][robustness]")
{
    SECTION("Open non-existent file returns nullptr")
    {
        GDALDatasetH ds = GDALOpen("/nonexistent/file.tif", GA_ReadOnly);
        REQUIRE(ds == nullptr);
    }

    SECTION("Safe open with error handling")
    {
        bool threw = false;
        try {
            GDALDatasetH ds = GDALOpen("/nonexistent/file.tif", GA_ReadOnly);
            if (!ds) {
                throw std::runtime_error("Failed to open raster");
            }
        } catch (const std::runtime_error &e) {
            threw = true;
        }
        REQUIRE(threw);
    }
}
