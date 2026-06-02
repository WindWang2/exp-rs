#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include <vector>
#include <cmath>

using namespace Catch;

TEST_CASE("PCA on 2-band correlated data", "[pca]") {
    size_t n = 100;
    size_t bands = 2;
    std::vector<std::vector<float>> input(bands, std::vector<float>(n));
    for (size_t i = 0; i < n; i++) {
        input[0][i] = static_cast<float>(i);
        input[1][i] = 2.0f * i + (i % 3 - 1) * 0.1f;
    }

    auto result = ImageEnhancement::pca(input, 2);

    REQUIRE(result.explainedVariance[0] > 0.99f);
    REQUIRE(result.explainedVariance[1] < 0.01f);
}

TEST_CASE("PCA output dimensions", "[pca]") {
    size_t n = 50;
    size_t bands = 3;
    std::vector<std::vector<float>> input(bands, std::vector<float>(n, 1.0f));

    auto result = ImageEnhancement::pca(input, 2);

    REQUIRE(result.output.size() == 2);
    REQUIRE(result.output[0].size() == n);
    REQUIRE(result.output[1].size() == n);
}

TEST_CASE("PCA variance sums to 1", "[pca]") {
    size_t n = 50;
    std::vector<std::vector<float>> input(3, std::vector<float>(n));
    for (size_t i = 0; i < n; i++) {
        input[0][i] = static_cast<float>(i);
        input[1][i] = static_cast<float>(i * 2);
        input[2][i] = static_cast<float>(i * 3);
    }

    auto result = ImageEnhancement::pca(input, 3);

    float totalVariance = 0;
    for (auto v : result.explainedVariance) totalVariance += v;
    REQUIRE(totalVariance == Approx(1.0f).margin(0.01f));
}
