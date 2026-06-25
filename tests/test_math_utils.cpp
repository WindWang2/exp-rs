// test_math_utils.cpp — Tests for MathUtils shared utilities
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/math_utils.h"

#include <cmath>
#include <limits>
#include <vector>

static constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

TEST_CASE("MathUtils::safeDiv", "[math_utils]")
{
    SECTION("normal division")
    {
        float result = MathUtils::safeDiv(10.0f, 2.0f);
        REQUIRE(result == Catch::Approx(5.0f));
    }

    SECTION("division by zero returns NaN")
    {
        float result = MathUtils::safeDiv(10.0f, 0.0f);
        REQUIRE(std::isnan(result));
    }

    SECTION("negative division")
    {
        float result = MathUtils::safeDiv(-10.0f, 2.0f);
        REQUIRE(result == Catch::Approx(-5.0f));
    }

    SECTION("zero numerator")
    {
        float result = MathUtils::safeDiv(0.0f, 5.0f);
        REQUIRE(result == Catch::Approx(0.0f));
    }
}

TEST_CASE("MathUtils::computeStats", "[math_utils]")
{
    SECTION("empty array returns zero stats")
    {
        MathUtils::Stats stats = MathUtils::computeStats(nullptr, 0);
        REQUIRE(stats.count == 0);
        REQUIRE(stats.validCount == 0);
        REQUIRE(stats.min == 0.0f);
        REQUIRE(stats.max == 0.0f);
        REQUIRE(stats.mean == 0.0f);
        REQUIRE(stats.stddev == 0.0f);
    }

    SECTION("single value")
    {
        float data[] = {5.0f};
        MathUtils::Stats stats = MathUtils::computeStats(data, 1);
        REQUIRE(stats.count == 1);
        REQUIRE(stats.validCount == 1);
        REQUIRE(stats.min == Catch::Approx(5.0f));
        REQUIRE(stats.max == Catch::Approx(5.0f));
        REQUIRE(stats.mean == Catch::Approx(5.0f));
        REQUIRE(stats.stddev == Catch::Approx(0.0f));
    }

    SECTION("multiple values")
    {
        float data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        MathUtils::Stats stats = MathUtils::computeStats(data, 5);
        REQUIRE(stats.count == 5);
        REQUIRE(stats.validCount == 5);
        REQUIRE(stats.min == Catch::Approx(1.0f));
        REQUIRE(stats.max == Catch::Approx(5.0f));
        REQUIRE(stats.mean == Catch::Approx(3.0f));
        REQUIRE(stats.stddev == Catch::Approx(1.5811f).margin(0.001f));
    }

    SECTION("NaN values are skipped")
    {
        float data[] = {1.0f, NaN, 3.0f, NaN, 5.0f};
        MathUtils::Stats stats = MathUtils::computeStats(data, 5);
        REQUIRE(stats.count == 5);
        REQUIRE(stats.validCount == 3);
        REQUIRE(stats.min == Catch::Approx(1.0f));
        REQUIRE(stats.max == Catch::Approx(5.0f));
        REQUIRE(stats.mean == Catch::Approx(3.0f));
    }

    SECTION("all NaN values")
    {
        float data[] = {NaN, NaN, NaN};
        MathUtils::Stats stats = MathUtils::computeStats(data, 3);
        REQUIRE(stats.count == 3);
        REQUIRE(stats.validCount == 0);
        REQUIRE(stats.min == 0.0f);
        REQUIRE(stats.max == 0.0f);
        REQUIRE(stats.mean == 0.0f);
        REQUIRE(stats.stddev == 0.0f);
    }

    SECTION("negative values")
    {
        float data[] = {-3.0f, -1.0f, 1.0f, 3.0f};
        MathUtils::Stats stats = MathUtils::computeStats(data, 4);
        REQUIRE(stats.count == 4);
        REQUIRE(stats.validCount == 4);
        REQUIRE(stats.min == Catch::Approx(-3.0f));
        REQUIRE(stats.max == Catch::Approx(3.0f));
        REQUIRE(stats.mean == Catch::Approx(0.0f));
        REQUIRE(stats.stddev == Catch::Approx(2.582f).margin(0.001f));
    }
}

TEST_CASE("MathUtils::normalizedDifference", "[math_utils]")
{
    SECTION("basic normalized difference")
    {
        float a[] = {0.8f, 0.5f, 0.3f};
        float b[] = {0.2f, 0.5f, 0.7f};
        float out[3];

        bool ok = MathUtils::normalizedDifference(a, b, out, 3);
        REQUIRE(ok);
        REQUIRE(out[0] == Catch::Approx(0.6f / 1.0f));  // (0.8-0.2)/(0.8+0.2)
        REQUIRE(out[1] == Catch::Approx(0.0f / 1.0f));   // (0.5-0.5)/(0.5+0.5)
        REQUIRE(out[2] == Catch::Approx(-0.4f / 1.0f));  // (0.3-0.7)/(0.3+0.7)
    }

    SECTION("zero denominator returns NaN")
    {
        float a[] = {0.0f};
        float b[] = {0.0f};
        float out[1];

        bool ok = MathUtils::normalizedDifference(a, b, out, 1);
        REQUIRE(ok);
        REQUIRE(std::isnan(out[0]));
    }

    SECTION("null pointers return false")
    {
        float b[] = {1.0f};
        float out[1];

        REQUIRE_FALSE(MathUtils::normalizedDifference(nullptr, b, out, 1));
        REQUIRE_FALSE(MathUtils::normalizedDifference(b, nullptr, out, 1));
        REQUIRE_FALSE(MathUtils::normalizedDifference(b, b, nullptr, 1));
    }

    SECTION("zero count returns false")
    {
        float a[] = {1.0f};
        float out[1];

        REQUIRE_FALSE(MathUtils::normalizedDifference(a, a, out, 0));
    }
}

TEST_CASE("MathUtils::safeDivDouble", "[math_utils]")
{
    SECTION("normal division")
    {
        double result = MathUtils::safeDivDouble(10.0, 2.0);
        REQUIRE(result == Catch::Approx(5.0));
    }

    SECTION("division by zero returns 0.0")
    {
        double result = MathUtils::safeDivDouble(10.0, 0.0);
        REQUIRE(result == Catch::Approx(0.0));
    }

    SECTION("negative division")
    {
        double result = MathUtils::safeDivDouble(-10.0, 2.0);
        REQUIRE(result == Catch::Approx(-5.0));
    }
}

TEST_CASE("MathUtils::computeStatsWithNodata", "[math_utils]")
{
    SECTION("nodata values are skipped")
    {
        float data[] = {1.0f, -9999.0f, 3.0f, -9999.0f, 5.0f};
        MathUtils::Stats stats = MathUtils::computeStatsWithNodata(data, 5, -9999.0f);
        REQUIRE(stats.count == 5);
        REQUIRE(stats.validCount == 3);
        REQUIRE(stats.min == Catch::Approx(1.0f));
        REQUIRE(stats.max == Catch::Approx(5.0f));
        REQUIRE(stats.mean == Catch::Approx(3.0f));
    }

    SECTION("NaN and nodata both skipped")
    {
        float data[] = {1.0f, NaN, 3.0f, -9999.0f, 5.0f};
        MathUtils::Stats stats = MathUtils::computeStatsWithNodata(data, 5, -9999.0f);
        REQUIRE(stats.count == 5);
        REQUIRE(stats.validCount == 3);
        REQUIRE(stats.min == Catch::Approx(1.0f));
        REQUIRE(stats.max == Catch::Approx(5.0f));
    }

    SECTION("all nodata returns zero stats")
    {
        float data[] = {-9999.0f, -9999.0f, -9999.0f};
        MathUtils::Stats stats = MathUtils::computeStatsWithNodata(data, 3, -9999.0f);
        REQUIRE(stats.count == 3);
        REQUIRE(stats.validCount == 0);
        REQUIRE(stats.min == 0.0f);
        REQUIRE(stats.max == 0.0f);
    }
}

TEST_CASE("MathUtils::computeStatsFromAccumulators", "[math_utils]")
{
    SECTION("basic accumulator stats")
    {
        // Simulate: values = {2, 4, 4, 4, 5, 5, 7, 9}
        // count=8, sum=40, sumSq=232, min=2, max=9
        MathUtils::AccumulatorStats acc;
        acc.count = 8;
        acc.sum = 40.0;
        acc.sumSq = 232.0;
        acc.min = 2.0f;
        acc.max = 9.0f;

        MathUtils::Stats stats = MathUtils::computeStatsFromAccumulators(acc);
        REQUIRE(stats.count == 8);
        REQUIRE(stats.validCount == 8);
        REQUIRE(stats.min == Catch::Approx(2.0f));
        REQUIRE(stats.max == Catch::Approx(9.0f));
        REQUIRE(stats.mean == Catch::Approx(5.0f));
        // Population variance = 232/8 - 25 = 29 - 25 = 4
        REQUIRE(stats.stddev == Catch::Approx(2.0f));
    }

    SECTION("empty accumulator")
    {
        MathUtils::AccumulatorStats acc;
        acc.count = 0;

        MathUtils::Stats stats = MathUtils::computeStatsFromAccumulators(acc);
        REQUIRE(stats.count == 0);
        REQUIRE(stats.mean == Catch::Approx(0.0f));
        REQUIRE(stats.stddev == Catch::Approx(0.0f));
    }

    SECTION("single value")
    {
        MathUtils::AccumulatorStats acc;
        acc.count = 1;
        acc.sum = 42.0;
        acc.sumSq = 1764.0;
        acc.min = 42.0f;
        acc.max = 42.0f;

        MathUtils::Stats stats = MathUtils::computeStatsFromAccumulators(acc);
        REQUIRE(stats.count == 1);
        REQUIRE(stats.mean == Catch::Approx(42.0f));
        REQUIRE(stats.stddev == Catch::Approx(0.0f));
    }
}
