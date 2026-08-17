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
    SECTION("gdalSafeOpen non-existent file returns nullptr")
    {
        GDALDatasetH ds = gdalSafeOpen("/nonexistent/file.tif", GA_ReadOnly);
        REQUIRE(ds == nullptr);
    }

    SECTION("gdalSafeClose safely handles nullptr and valid handles")
    {
        GDALDatasetH nullDs = nullptr;
        gdalSafeClose(nullDs);
        REQUIRE(nullDs == nullptr);

        GDALAllRegister();
        GDALDriverH memDriver = GDALGetDriverByName("MEM");
        if (memDriver) {
            GDALDatasetH validDs = GDALCreate(memDriver, "", 2, 2, 1, GDT_Byte, nullptr);
            REQUIRE(validDs != nullptr);
            gdalSafeClose(validDs);
            REQUIRE(validDs == nullptr);
        }
    }

    SECTION("GdalDatasetGuard manages dataset lifecycle via RAII")
    {
        GDALAllRegister();
        GDALDriverH memDriver = GDALGetDriverByName("MEM");
        if (memDriver) {
            GDALDatasetH ds = GDALCreate(memDriver, "", 2, 2, 1, GDT_Byte, nullptr);
            {
                GdalDatasetGuard guard(ds);
                REQUIRE(guard.get() == ds);
                REQUIRE(static_cast<bool>(guard));
            } // Destructor closes ds

            // Move semantics
            GDALDatasetH ds2 = GDALCreate(memDriver, "", 2, 2, 1, GDT_Byte, nullptr);
            GdalDatasetGuard guard1(ds2);
            GdalDatasetGuard guard2(std::move(guard1));
            REQUIRE(guard1.get() == nullptr);
            REQUIRE(guard2.get() == ds2);
        }
    }
}
