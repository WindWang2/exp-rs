#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include <vector>
#include <cmath>

using namespace Catch;

TEST_CASE("Mean filter 3x3 on uniform image", "[spatial]") {
    std::vector<float> input(25, 100.0f);
    std::vector<float> output(25, 0.0f);
    ImageEnhancement::meanFilter(input.data(), output.data(), 5, 5, 3);
    REQUIRE(output[6] == Approx(100.0f));
    REQUIRE(output[12] == Approx(100.0f));
}

TEST_CASE("Mean filter 3x3 on step edge", "[spatial]") {
    std::vector<float> input(25);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            input[y * 5 + x] = (x < 2) ? 0.0f : 100.0f;
    std::vector<float> output(25, 0.0f);
    ImageEnhancement::meanFilter(input.data(), output.data(), 5, 5, 3);
    REQUIRE(output[2] > 0.0f);
    REQUIRE(output[2] < 100.0f);
}

TEST_CASE("Median filter removes salt-and-pepper noise", "[spatial]") {
    std::vector<float> input(25, 50.0f);
    input[12] = 999.0f;
    std::vector<float> output(25, 0.0f);
    ImageEnhancement::medianFilter(input.data(), output.data(), 5, 5, 3);
    REQUIRE(output[12] == Approx(50.0f));
}

TEST_CASE("Sobel filter detects horizontal edge", "[spatial]") {
    std::vector<float> input(25);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            input[y * 5 + x] = (y < 2) ? 0.0f : 100.0f;
    std::vector<float> output(25, 0.0f);
    ImageEnhancement::sobelFilter(input.data(), output.data(), 5, 5);
    REQUIRE(output[2 * 5 + 2] > 0.0f);
    REQUIRE(std::abs(output[0]) < 10.0f);
}

TEST_CASE("Laplacian filter detects edges", "[spatial]") {
    std::vector<float> input(25);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            input[y * 5 + x] = (x < 2) ? 0.0f : 100.0f;
    std::vector<float> output(25, 0.0f);
    ImageEnhancement::laplacianFilter(input.data(), output.data(), 5, 5);
    REQUIRE(std::abs(output[2]) > 0.0f);
}

TEST_CASE("Gaussian filter smooths noise", "[spatial]") {
    std::vector<float> input(25, 50.0f);
    input[12] = 200.0f;
    std::vector<float> output(25, 0.0f);
    ImageEnhancement::gaussianFilter(input.data(), output.data(), 5, 5, 3, 1.0f);
    REQUIRE(output[12] < 200.0f);
    REQUIRE(output[12] > 50.0f);
}
