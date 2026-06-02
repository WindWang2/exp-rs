#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include <vector>
#include <cmath>

using namespace Catch;

TEST_CASE("Band ratio calculation", "[enhancement]") {
    std::vector<float> band1 = {10, 20, 30};
    std::vector<float> band2 = {5, 10, 15};
    std::vector<float> output(3);

    ImageEnhancement::bandRatio(band1.data(), band2.data(), output.data(), 3);

    REQUIRE(output[0] == Approx(2.0f));
    REQUIRE(output[1] == Approx(2.0f));
    REQUIRE(output[2] == Approx(2.0f));
}

TEST_CASE("Band ratio with zero denominator", "[enhancement]") {
    std::vector<float> band1 = {10, 20, 30};
    std::vector<float> band2 = {5, 0, 15};
    std::vector<float> output(3);

    ImageEnhancement::bandRatio(band1.data(), band2.data(), output.data(), 3);

    REQUIRE(output[0] == Approx(2.0f));
    REQUIRE(std::isfinite(output[1]) == false);  // x/0 -> NaN
    REQUIRE(output[2] == Approx(2.0f));
}

TEST_CASE("IHS forward and inverse transform", "[enhancement]") {
    float r = 200, g = 100, b = 50;
    float i, h, s;

    ImageEnhancement::rgbToIhs(r, g, b, i, h, s);

    // Intensity should be average
    REQUIRE(i == Approx((200 + 100 + 50) / 3.0f));

    // Saturation should be in [0, 1]
    REQUIRE(s >= 0.0f);
    REQUIRE(s <= 1.0f);

    // Convert back
    float r2, g2, b2;
    ImageEnhancement::ihsToRgb(i, h, s, r2, g2, b2);

    REQUIRE(r2 == Approx(r).margin(1.0f));
    REQUIRE(g2 == Approx(g).margin(1.0f));
    REQUIRE(b2 == Approx(b).margin(1.0f));
}

TEST_CASE("IHS achromatic (gray)", "[enhancement]") {
    float r = 128, g = 128, b = 128;
    float i, h, s;

    ImageEnhancement::rgbToIhs(r, g, b, i, h, s);

    REQUIRE(i == Approx(128.0f));
    REQUIRE(s == Approx(0.0f));

    float r2, g2, b2;
    ImageEnhancement::ihsToRgb(i, h, s, r2, g2, b2);

    REQUIRE(r2 == Approx(128.0f).margin(1.0f));
    REQUIRE(g2 == Approx(128.0f).margin(1.0f));
    REQUIRE(b2 == Approx(128.0f).margin(1.0f));
}

TEST_CASE("IHS round-trip primary colors", "[enhancement]") {
    // Test pure red
    {
        float r = 255, g = 0, b = 0;
        float i, h, s;
        ImageEnhancement::rgbToIhs(r, g, b, i, h, s);
        float r2, g2, b2;
        ImageEnhancement::ihsToRgb(i, h, s, r2, g2, b2);
        REQUIRE(r2 == Approx(255.0f).margin(1.0f));
        REQUIRE(g2 == Approx(0.0f).margin(1.0f));
        REQUIRE(b2 == Approx(0.0f).margin(1.0f));
    }
    // Test pure green
    {
        float r = 0, g = 255, b = 0;
        float i, h, s;
        ImageEnhancement::rgbToIhs(r, g, b, i, h, s);
        float r2, g2, b2;
        ImageEnhancement::ihsToRgb(i, h, s, r2, g2, b2);
        REQUIRE(r2 == Approx(0.0f).margin(1.0f));
        REQUIRE(g2 == Approx(255.0f).margin(1.0f));
        REQUIRE(b2 == Approx(0.0f).margin(1.0f));
    }
    // Test pure blue
    {
        float r = 0, g = 0, b = 255;
        float i, h, s;
        ImageEnhancement::rgbToIhs(r, g, b, i, h, s);
        float r2, g2, b2;
        ImageEnhancement::ihsToRgb(i, h, s, r2, g2, b2);
        REQUIRE(r2 == Approx(0.0f).margin(1.0f));
        REQUIRE(g2 == Approx(0.0f).margin(1.0f));
        REQUIRE(b2 == Approx(255.0f).margin(1.0f));
    }
}

TEST_CASE("IHS black (zero intensity)", "[enhancement]") {
    float r = 0, g = 0, b = 0;
    float i, h, s;

    ImageEnhancement::rgbToIhs(r, g, b, i, h, s);

    REQUIRE(i == Approx(0.0f));
    REQUIRE(s == Approx(0.0f));

    float r2, g2, b2;
    ImageEnhancement::ihsToRgb(i, h, s, r2, g2, b2);

    REQUIRE(r2 == Approx(0.0f).margin(1.0f));
    REQUIRE(g2 == Approx(0.0f).margin(1.0f));
    REQUIRE(b2 == Approx(0.0f).margin(1.0f));
}
