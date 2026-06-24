// test_input_validator.cpp — Input validator tests
#include <catch2/catch_test_macros.hpp>

#include <processing/framework/input_validator.h>

TEST_CASE("InputValidator validateRasterDimensions", "[robustness][validator]")
{
    QString error;

    SECTION("Valid dimensions")
    {
        REQUIRE(InputValidator::validateRasterDimensions(100, 100, error));
        REQUIRE(error.isEmpty());
    }

    SECTION("Zero width")
    {
        REQUIRE_FALSE(InputValidator::validateRasterDimensions(0, 100, error));
        REQUIRE(!error.isEmpty());
    }

    SECTION("Zero height")
    {
        REQUIRE_FALSE(InputValidator::validateRasterDimensions(100, 0, error));
        REQUIRE(!error.isEmpty());
    }

    SECTION("Negative width")
    {
        REQUIRE_FALSE(InputValidator::validateRasterDimensions(-1, 100, error));
        REQUIRE(!error.isEmpty());
    }

    SECTION("Negative height")
    {
        REQUIRE_FALSE(InputValidator::validateRasterDimensions(100, -1, error));
        REQUIRE(!error.isEmpty());
    }
}

TEST_CASE("InputValidator validateBandIndex", "[robustness][validator]")
{
    QString error;

    SECTION("Valid band index")
    {
        REQUIRE(InputValidator::validateBandIndex(1, 4, error));
        REQUIRE(InputValidator::validateBandIndex(4, 4, error));
    }

    SECTION("Zero band index")
    {
        REQUIRE_FALSE(InputValidator::validateBandIndex(0, 4, error));
    }

    SECTION("Band index exceeds max")
    {
        REQUIRE_FALSE(InputValidator::validateBandIndex(5, 4, error));
    }

    SECTION("Negative band index")
    {
        REQUIRE_FALSE(InputValidator::validateBandIndex(-1, 4, error));
    }
}

TEST_CASE("InputValidator validateNumericRange", "[robustness][validator]")
{
    QString error;

    SECTION("Valid range")
    {
        REQUIRE(InputValidator::validateNumericRange(50, 0, 100, error));
    }

    SECTION("Below minimum")
    {
        REQUIRE_FALSE(InputValidator::validateNumericRange(-1, 0, 100, error));
    }

    SECTION("Above maximum")
    {
        REQUIRE_FALSE(InputValidator::validateNumericRange(101, 0, 100, error));
    }

    SECTION("NaN value")
    {
        REQUIRE_FALSE(InputValidator::validateNumericRange(std::numeric_limits<double>::quiet_NaN(), 0, 100, error));
    }

    SECTION("Infinity value")
    {
        REQUIRE_FALSE(InputValidator::validateNumericRange(std::numeric_limits<double>::infinity(), 0, 100, error));
    }
}

TEST_CASE("InputValidator validateOutputPath", "[robustness][validator]")
{
    QString error;

    SECTION("Valid path")
    {
        REQUIRE(InputValidator::validateOutputPath("/tmp/output.tif", error));
    }

    SECTION("Empty path")
    {
        REQUIRE_FALSE(InputValidator::validateOutputPath("", error));
    }
}
