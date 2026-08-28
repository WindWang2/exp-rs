#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

using namespace Catch;

TEST_CASE("Linear min-max stretch", "[enhancement]") {
    std::vector<float> input = {0, 25, 50, 75, 100};
    std::vector<float> output(5);
    ImageEnhancement::linearStretch(input.data(), output.data(), 5, 0.0f, 100.0f);
    REQUIRE(output[0] == Approx(0.0f));
    REQUIRE(output[2] == Approx(127.5f));
    REQUIRE(output[4] == Approx(255.0f));
}

TEST_CASE("Percentage clip stretch", "[enhancement]") {
    std::vector<float> input(100);
    for (int i = 0; i < 100; i++) input[i] = static_cast<float>(i);
    std::vector<float> output(100);
    ImageEnhancement::percentClipStretch(input.data(), output.data(), 100, 5.0f);
    REQUIRE(output[5] == Approx(0.0f).margin(1.0f));
    REQUIRE(output[94] == Approx(255.0f).margin(1.0f));
}

TEST_CASE("Standard deviation stretch", "[enhancement]") {
    std::vector<float> input = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    std::vector<float> output(10);
    ImageEnhancement::stddevStretch(input.data(), output.data(), 10, 2.0f);
    REQUIRE(output[0] >= 0.0f);
    REQUIRE(output[9] <= 255.0f);
}

TEST_CASE("Histogram equalization", "[enhancement]") {
    std::vector<float> input = {1, 1, 1, 1, 1, 2, 2, 3, 5, 10};
    std::vector<float> output(10);
    ImageEnhancement::histogramEqualize(input.data(), output.data(), 10, 256);
    REQUIRE(output[0] < output[9]);
    REQUIRE(output[0] == output[1]);
    REQUIRE(output[1] == output[4]);
}

TEST_CASE("Contrast stretch preserves nodata", "[enhancement]") {
    float nodata = -9999.0f;
    std::vector<float> input = {10, 20, -9999, 30, 40};
    std::vector<float> output(5);
    ImageEnhancement::linearStretch(input.data(), output.data(), 5, 10.0f, 40.0f, nodata);
    REQUIRE(output[2] == Approx(nodata));
    REQUIRE(output[0] == Approx(0.0f));
    REQUIRE(output[4] == Approx(255.0f));
}

TEST_CASE("Lee filter excludes +-Inf pixels from local statistics", "[enhancement]") {
    // A single +Inf must not poison the summed-area table: before the
    // isfinite() guard every window whose rectangle contained the cell
    // produced Inf/NaN local statistics (#634).
    constexpr int W = 10, H = 10;
    std::vector<float> input(W * H, 1.0f);
    input[5 * W + 5] = std::numeric_limits<float>::infinity();
    std::vector<float> output(W * H, 0.0f);
    ImageEnhancement::leeFilter(input.data(), output.data(), W, H, 3, 0.5f);
    for (float v : output)
        REQUIRE(std::isfinite(v));
}
