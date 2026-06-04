// tests/test_change_detection.cpp — TDD for change detection algorithm
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/change_detection.h"

#include <vector>
#include <cmath>
#include <limits>

using namespace ChangeDetection;
using Catch::Approx;

TEST_CASE("ChangeDetection difference computes absolute change", "[processing][change_detection]") {
    std::vector<float> before = {10.0f, 20.0f, 30.0f, 40.0f};
    std::vector<float> after  = {12.0f, 18.0f, 30.0f, 50.0f};

    std::vector<float> out(4, 0.0f);

    REQUIRE(difference(before.data(), after.data(), out.data(), 4));

    CHECK(out[0] == Approx(2.0f));
    CHECK(out[1] == Approx(2.0f));
    CHECK(out[2] == Approx(0.0f));
    CHECK(out[3] == Approx(10.0f));
}

TEST_CASE("ChangeDetection difference returns false on null pointers", "[processing][change_detection]") {
    std::vector<float> data(4, 1.0f);
    std::vector<float> out(4, 0.0f);

    CHECK_FALSE(difference(nullptr, data.data(), out.data(), 4));
    CHECK_FALSE(difference(data.data(), nullptr, out.data(), 4));
    CHECK_FALSE(difference(data.data(), data.data(), nullptr, 4));
}

TEST_CASE("ChangeDetection difference returns false on zero count", "[processing][change_detection]") {
    std::vector<float> data(1, 1.0f);
    std::vector<float> out(1, 0.0f);

    CHECK_FALSE(difference(data.data(), data.data(), out.data(), 0));
}

TEST_CASE("ChangeDetection normalizedDifference computes relative change", "[processing][change_detection]") {
    std::vector<float> before = {100.0f, 50.0f, 0.0f, 10.0f};
    std::vector<float> after  = {120.0f, 30.0f, 0.0f, 10.0f};

    std::vector<float> out(4, 0.0f);

    REQUIRE(normalizedDifference(before.data(), after.data(), out.data(), 4));

    // (after - before) / (after + before)
    // (120-100)/(120+100) = 20/220 ≈ 0.0909
    CHECK(out[0] == Approx(20.0f / 220.0f).margin(0.001f));
    // (30-50)/(30+50) = -20/80 = -0.25
    CHECK(out[1] == Approx(-0.25f).margin(0.001f));
    // (0-0)/(0+0) = NaN → should be NaN
    CHECK(std::isnan(out[2]));
    // (10-10)/(10+10) = 0/20 = 0
    CHECK(out[3] == Approx(0.0f));
}

TEST_CASE("ChangeDetection changeMask with threshold", "[processing][change_detection]") {
    std::vector<float> diff = {0.5f, 2.0f, 0.1f, 5.0f, 1.0f};
    std::vector<uint8_t> mask(5, 0);

    REQUIRE(changeMask(diff.data(), mask.data(), 5, 1.0f));

    CHECK(mask[0] == 0);  // 0.5 < 1.0
    CHECK(mask[1] == 1);  // 2.0 >= 1.0
    CHECK(mask[2] == 0);  // 0.1 < 1.0
    CHECK(mask[3] == 1);  // 5.0 >= 1.0
    CHECK(mask[4] == 1);  // 1.0 >= 1.0 (boundary)
}

TEST_CASE("ChangeDetection changeMask returns false on null", "[processing][change_detection]") {
    std::vector<uint8_t> mask(4, 0);
    CHECK_FALSE(changeMask(nullptr, mask.data(), 4, 1.0f));
    CHECK_FALSE(changeMask(nullptr, nullptr, 4, 1.0f));
}

TEST_CASE("ChangeDetection statistics computes change stats", "[processing][change_detection]") {
    std::vector<float> diff = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

    ChangeStats stats = statistics(diff.data(), 5);

    CHECK(stats.count == 5);
    CHECK(stats.mean == Approx(3.0f));
    CHECK(stats.min == Approx(1.0f));
    CHECK(stats.max == Approx(5.0f));
    CHECK(stats.stddev > 0.0f);
}

TEST_CASE("ChangeDetection statistics with single value", "[processing][change_detection]") {
    std::vector<float> diff = {42.0f};

    ChangeStats stats = statistics(diff.data(), 1);

    CHECK(stats.count == 1);
    CHECK(stats.mean == Approx(42.0f));
    CHECK(stats.min == Approx(42.0f));
    CHECK(stats.max == Approx(42.0f));
    CHECK(stats.stddev == Approx(0.0f));
}

TEST_CASE("ChangeDetection statistics returns zero on null/empty", "[processing][change_detection]") {
    ChangeStats stats = statistics(nullptr, 0);
    CHECK(stats.count == 0);
    CHECK(stats.mean == 0.0f);
}
