// test_dialog_edge_cases.cpp — Real edge case tests for processing dialog validators and parameter models
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QString>
#include <QTemporaryDir>
#include <QFile>

#include "processing/framework/input_validator.h"
#include "processing/algorithms/band_math.h"

// ---- InputValidator Edge Cases ----

TEST_CASE("InputValidator raster dimensions validation", "[dialog][validator]")
{
    QString error;
    SECTION("Valid positive dimensions")
    {
        REQUIRE(InputValidator::validateRasterDimensions(100, 100, error));
        REQUIRE(error.isEmpty());
    }

    SECTION("Zero width is invalid")
    {
        REQUIRE_FALSE(InputValidator::validateRasterDimensions(0, 100, error));
        REQUIRE_FALSE(error.isEmpty());
    }

    SECTION("Negative height is invalid")
    {
        REQUIRE_FALSE(InputValidator::validateRasterDimensions(100, -5, error));
        REQUIRE_FALSE(error.isEmpty());
    }
}

TEST_CASE("InputValidator band index validation", "[dialog][validator]")
{
    QString error;
    SECTION("1-based band index within max bands is valid")
    {
        REQUIRE(InputValidator::validateBandIndex(1, 4, error));
        REQUIRE(InputValidator::validateBandIndex(4, 4, error));
        REQUIRE(error.isEmpty());
    }

    SECTION("0 is invalid (1-based bands)")
    {
        REQUIRE_FALSE(InputValidator::validateBandIndex(0, 4, error));
        REQUIRE_FALSE(error.isEmpty());
    }

    SECTION("Band exceeding max bands is invalid")
    {
        REQUIRE_FALSE(InputValidator::validateBandIndex(5, 4, error));
        REQUIRE_FALSE(error.isEmpty());
    }
}

TEST_CASE("InputValidator numeric range validation", "[dialog][validator]")
{
    QString error;
    SECTION("Value in range is valid")
    {
        REQUIRE(InputValidator::validateNumericRange(0.5, 0.0, 1.0, error));
        REQUIRE(error.isEmpty());
    }

    SECTION("Value below min is invalid")
    {
        REQUIRE_FALSE(InputValidator::validateNumericRange(-0.1, 0.0, 1.0, error));
        REQUIRE_FALSE(error.isEmpty());
    }

    SECTION("Value above max is invalid")
    {
        REQUIRE_FALSE(InputValidator::validateNumericRange(1.1, 0.0, 1.0, error));
        REQUIRE_FALSE(error.isEmpty());
    }

    SECTION("NaN value is invalid")
    {
        double nanVal = std::numeric_limits<double>::quiet_NaN();
        REQUIRE_FALSE(InputValidator::validateNumericRange(nanVal, 0.0, 1.0, error));
        REQUIRE_FALSE(error.isEmpty());
    }
}

TEST_CASE("InputValidator kernel size validation", "[dialog][validator]")
{
    QString error;
    SECTION("Odd positive kernel size is valid")
    {
        REQUIRE(InputValidator::validateKernelSize(3, error));
        REQUIRE(InputValidator::validateKernelSize(5, error));
        REQUIRE(InputValidator::validateKernelSize(7, error));
        REQUIRE(error.isEmpty());
    }

    SECTION("Even kernel size is invalid")
    {
        REQUIRE_FALSE(InputValidator::validateKernelSize(2, error));
        REQUIRE_FALSE(InputValidator::validateKernelSize(4, error));
        REQUIRE_FALSE(error.isEmpty());
    }

    SECTION("Zero or negative kernel size is invalid")
    {
        REQUIRE_FALSE(InputValidator::validateKernelSize(0, error));
        REQUIRE_FALSE(InputValidator::validateKernelSize(-3, error));
        REQUIRE_FALSE(error.isEmpty());
    }
}

TEST_CASE("InputValidator output path validation", "[dialog][validator]")
{
    QString error;
    SECTION("Empty output path is invalid")
    {
        REQUIRE_FALSE(InputValidator::validateOutputPath(QString(), error));
        REQUIRE_FALSE(error.isEmpty());
    }

    SECTION("Valid writable path passes")
    {
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        QString outPath = dir.filePath("test_out.tif");
        REQUIRE(InputValidator::validateOutputPath(outPath, error));
        REQUIRE(error.isEmpty());
    }
}

// ---- Band Math Expression Edge Cases ----

TEST_CASE("Band Math expression execution edge cases", "[dialog][band_math]")
{
    BandMath::BandData bands;
    bands[1] = {10.0f, 20.0f, 30.0f, 0.0f};
    bands[2] = {5.0f,  10.0f, 0.0f,  0.0f};
    std::vector<float> output(4, 0.0f);

    SECTION("Empty expression returns false")
    {
        REQUIRE_FALSE(BandMath::evaluate(QString(), bands, output.data(), 4));
    }

    SECTION("NDVI normalized difference calculation")
    {
        // (b1 - b2) / (b1 + b2)
        // [0]: (10-5)/(10+5) = 5/15 = 0.33333
        // [1]: (20-10)/(20+10) = 10/30 = 0.33333
        // [2]: (30-0)/(30+0) = 1.0
        // [3]: 0 / 0 -> NaN
        REQUIRE(BandMath::evaluate("(b1 - b2) / (b1 + b2)", bands, output.data(), 4));
        REQUIRE(output[0] == Catch::Approx(0.333333f).margin(1e-4f));
        REQUIRE(output[1] == Catch::Approx(0.333333f).margin(1e-4f));
        REQUIRE(output[2] == Catch::Approx(1.0f).margin(1e-4f));
        REQUIRE(std::isnan(output[3]));
    }

    SECTION("Missing referenced band returns false")
    {
        REQUIRE_FALSE(BandMath::evaluate("b3 * 2", bands, output.data(), 4));
    }
}
