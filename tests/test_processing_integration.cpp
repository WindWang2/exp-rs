// tests/test_processing_integration.cpp — Integration tests for multi-step processing workflows
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/band_math.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/mosaic.h"

#include <vector>
#include <cmath>
#include <limits>

using Catch::Approx;

// === Integration: Band Math → Change Detection ===

TEST_CASE("Integration: band math then change detection", "[integration][processing]") {
    // Simulate two time points: before and after
    // Band 1 (Red), Band 2 (NIR)
    std::vector<float> red_before  = {100.0f, 150.0f, 200.0f, 50.0f};
    std::vector<float> nir_before  = {200.0f, 180.0f, 100.0f, 300.0f};
    std::vector<float> red_after   = {120.0f, 130.0f, 200.0f, 80.0f};
    std::vector<float> nir_after   = {250.0f, 200.0f, 110.0f, 280.0f};

    // Step 1: Compute NDVI for both time points using SpectralIndices
    std::vector<float> ndvi_before(4);
    std::vector<float> ndvi_after(4);

    REQUIRE(SpectralIndices::ndvi(nir_before.data(), red_before.data(), ndvi_before.data(), 4));
    REQUIRE(SpectralIndices::ndvi(nir_after.data(), red_after.data(), ndvi_after.data(), 4));

    // Step 2: Compute change in NDVI using ChangeDetection
    std::vector<float> ndvi_change(4);
    REQUIRE(ChangeDetection::difference(ndvi_before.data(), ndvi_after.data(), ndvi_change.data(), 4));

    // Verify: NDVI change should be non-negative (absolute difference)
    for (size_t i = 0; i < 4; ++i) {
        CHECK(ndvi_change[i] >= 0.0f);
    }

    // Verify specific values:
    // Before: NDVI = (200-100)/(200+100) = 100/300 = 0.333
    // After:  NDVI = (250-120)/(250+120) = 130/370 = 0.351
    // Change: |0.351 - 0.333| ≈ 0.018
    CHECK(ndvi_change[0] == Approx(0.018f).margin(0.002f));
}

// === Integration: Spectral Indices → Change Mask ===

TEST_CASE("Integration: spectral index change mask", "[integration][processing]") {
    std::vector<float> nir_before = {200.0f, 180.0f, 100.0f};
    std::vector<float> red_before = {100.0f, 150.0f, 200.0f};
    std::vector<float> nir_after  = {250.0f, 160.0f, 110.0f};
    std::vector<float> red_after  = {120.0f, 140.0f, 190.0f};

    std::vector<float> ndvi_before(3), ndvi_after(3), ndvi_diff(3);
    std::vector<uint8_t> mask(3);

    REQUIRE(SpectralIndices::ndvi(nir_before.data(), red_before.data(), ndvi_before.data(), 3));
    REQUIRE(SpectralIndices::ndvi(nir_after.data(), red_after.data(), ndvi_after.data(), 3));
    REQUIRE(ChangeDetection::difference(ndvi_before.data(), ndvi_after.data(), ndvi_diff.data(), 3));
    REQUIRE(ChangeDetection::changeMask(ndvi_diff.data(), mask.data(), 3, 0.05f));

    // At least one pixel should be flagged as changed
    int changedPixels = 0;
    for (size_t i = 0; i < 3; ++i)
        changedPixels += mask[i];
    CHECK(changedPixels >= 1);
}

// === Integration: Mosaic with Band Math ===

TEST_CASE("Integration: mosaic then band math", "[integration][processing]") {
    // Two tiles side by side, each 2x2
    std::vector<float> tile1 = {10.0f, 20.0f,
                                 30.0f, 40.0f};
    std::vector<float> tile2 = {50.0f, 60.0f,
                                 70.0f, 80.0f};

    // Mosaic into 4x2 output
    std::vector<float> mosaic(8, 0.0f);
    Mosaic::MosaicSource s1{tile1.data(), 2, 2, 0, 0};
    Mosaic::MosaicSource s2{tile2.data(), 2, 2, 2, 0};
    std::vector<Mosaic::MosaicSource> sources = {s1, s2};

    REQUIRE(Mosaic::merge(sources.data(), sources.size(), mosaic.data(), 4, 2));

    // Apply band math: multiply all values by 0.1
    BandMath::BandData bands;
    std::vector<float> band1(mosaic.begin(), mosaic.end());
    bands[1] = band1;

    std::vector<float> result(8);
    REQUIRE(BandMath::evaluate("b1 * 0.1", bands, result.data(), 8));

    // Verify: each value should be original * 0.1
    // Mosaic layout: row0=[10,20,50,60], row1=[30,40,70,80]
    CHECK(result[0] == Approx(1.0f));  // 10*0.1
    CHECK(result[1] == Approx(2.0f));  // 20*0.1
    CHECK(result[4] == Approx(3.0f));  // 30*0.1
    CHECK(result[7] == Approx(8.0f));  // 80*0.1
}

// === Integration: Multi-band Change Detection ===

TEST_CASE("Integration: multi-band change statistics", "[integration][processing]") {
    // Simulate 2-band change: Red and NIR
    std::vector<float> red_before = {100.0f, 120.0f, 140.0f, 160.0f, 180.0f};
    std::vector<float> red_after  = {110.0f, 115.0f, 145.0f, 155.0f, 200.0f};

    std::vector<float> red_diff(5);
    REQUIRE(ChangeDetection::difference(red_before.data(), red_after.data(), red_diff.data(), 5));

    ChangeDetection::ChangeStats stats = ChangeDetection::statistics(red_diff.data(), 5);

    CHECK(stats.count == 5);
    CHECK(stats.mean > 0.0f);
    CHECK(stats.max >= stats.mean);
    CHECK(stats.min <= stats.mean);

    // All differences should be relatively small (< 50)
    CHECK(stats.max < 50.0f);
}

// === Integration: Normalized Difference with Statistics ===

TEST_CASE("Integration: normalized difference statistics", "[integration][processing]") {
    std::vector<float> before = {100.0f, 200.0f, 150.0f, 300.0f, 250.0f};
    std::vector<float> after  = {120.0f, 180.0f, 160.0f, 280.0f, 260.0f};

    std::vector<float> ndiff(5);
    REQUIRE(ChangeDetection::normalizedDifference(before.data(), after.data(), ndiff.data(), 5));

    ChangeDetection::ChangeStats stats = ChangeDetection::statistics(ndiff.data(), 5);

    CHECK(stats.count == 5);
    // Normalized differences should be in [-1, 1]
    CHECK(stats.min >= -1.0f);
    CHECK(stats.max <= 1.0f);
}
