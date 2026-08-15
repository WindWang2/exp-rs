// test_histogram_equalize_edge_cases.cpp — Edge case tests for histogramEqualize
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <processing/algorithms/image_enhancement.h>

#include <vector>
#include <limits>
#include <cmath>

TEST_CASE("histogramEqualize bins validation", "[image_enhancement][edge_case]")
{
    SECTION("Zero bins does not crash")
    {
        std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
        std::vector<float> output(4);
        // Should not crash or produce NaN
        ImageEnhancement::histogramEqualize(input.data(), output.data(), 4, 0, 0.0f);
        for (float v : output) {
            REQUIRE(std::isfinite(v));
        }
    }

    SECTION("Negative bins does not crash")
    {
        std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
        std::vector<float> output(4);
        ImageEnhancement::histogramEqualize(input.data(), output.data(), 4, -1, 0.0f);
        for (float v : output) {
            REQUIRE(std::isfinite(v));
        }
    }

    SECTION("Single bin")
    {
        std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
        std::vector<float> output(4);
        ImageEnhancement::histogramEqualize(input.data(), output.data(), 4, 1, 0.0f);
        for (float v : output) {
            REQUIRE(std::isfinite(v));
        }
    }
}

TEST_CASE("histogramEqualize all NaN input", "[image_enhancement][edge_case]")
{
    SECTION("All NaN produces finite output")
    {
        float nan = std::numeric_limits<float>::quiet_NaN();
        std::vector<float> input = {nan, nan, nan, nan};
        std::vector<float> output(4, -999.0f);
        ImageEnhancement::histogramEqualize(input.data(), output.data(), 4, 256, -999.0f);
        for (float v : output) {
            REQUIRE(v == -999.0f); // Should preserve nodata
        }
    }

    SECTION("All nodata produces nodata output")
    {
        std::vector<float> input = {-999.0f, -999.0f, -999.0f};
        std::vector<float> output(3, 0.0f);
        ImageEnhancement::histogramEqualize(input.data(), output.data(), 3, 256, -999.0f);
        for (float v : output) {
            REQUIRE(v == -999.0f);
        }
    }
}

TEST_CASE("histogramEqualize uniform input", "[image_enhancement][edge_case]")
{
    SECTION("All same value produces constant output")
    {
        std::vector<float> input = {5.0f, 5.0f, 5.0f, 5.0f};
        std::vector<float> output(4);
        ImageEnhancement::histogramEqualize(input.data(), output.data(), 4, 256, 0.0f);
        // All outputs should be the same
        for (float v : output) {
            REQUIRE(v == Catch::Approx(output[0]));
        }
    }
}
