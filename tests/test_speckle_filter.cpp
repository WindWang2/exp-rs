#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include <vector>
#include <cmath>
#include <numeric>

using namespace Catch;

// Helper: compute local mean in a window
static float localMean(const float *data, int w, int h, int cx, int cy, int win) {
    int half = win / 2;
    double sum = 0;
    int count = 0;
    for (int dy = -half; dy <= half; dy++) {
        for (int dx = -half; dx <= half; dx++) {
            int x = std::clamp(cx + dx, 0, w - 1);
            int y = std::clamp(cy + dy, 0, h - 1);
            sum += data[y * w + x];
            count++;
        }
    }
    return static_cast<float>(sum / count);
}

// Helper: compute local variance in a window
static float localVariance(const float *data, int w, int h, int cx, int cy, int win) {
    float mean = localMean(data, w, h, cx, cy, win);
    int half = win / 2;
    double sumSq = 0;
    int count = 0;
    for (int dy = -half; dy <= half; dy++) {
        for (int dx = -half; dx <= half; dx++) {
            int x = std::clamp(cx + dx, 0, w - 1);
            int y = std::clamp(cy + dy, 0, h - 1);
            double diff = data[y * w + x] - mean;
            sumSq += diff * diff;
            count++;
        }
    }
    return static_cast<float>(sumSq / count);
}

// ---- Lee Filter Tests ----

TEST_CASE("Lee filter preserves uniform region", "[speckle]") {
    // Uniform image should pass through unchanged
    std::vector<float> input(100, 50.0f);
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::leeFilter(input.data(), output.data(), 10, 10, 5, 1.0f);
    for (int i = 0; i < 100; i++) {
        REQUIRE(output[i] == Approx(50.0f).margin(0.1f));
    }
}

TEST_CASE("Lee filter reduces noise", "[speckle]") {
    // Add speckle noise to a uniform image
    std::vector<float> input(100, 100.0f);
    // Add multiplicative noise: pixel = true_value * (1 + noise)
    std::vector<float> noisy(100);
    for (int i = 0; i < 100; i++) {
        float noise = (i % 2 == 0) ? 0.5f : -0.3f;
        noisy[i] = input[i] * (1.0f + noise);
    }
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::leeFilter(noisy.data(), output.data(), 10, 10, 5, 1.0f);
    // Output should be closer to 100 than input
    float inputDev = std::abs(noisy[55] - 100.0f);
    float outputDev = std::abs(output[55] - 100.0f);
    REQUIRE(outputDev < inputDev);
}

TEST_CASE("Lee filter preserves edges", "[speckle]") {
    // Step edge: left half = 10, right half = 200
    std::vector<float> input(100);
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 10; x++)
            input[y * 10 + x] = (x < 5) ? 10.0f : 200.0f;
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::leeFilter(input.data(), output.data(), 10, 10, 5, 1.0f);
    // Left side should still be low, right side should still be high
    REQUIRE(output[5 * 10 + 2] < 50.0f);
    REQUIRE(output[5 * 10 + 7] > 150.0f);
}

TEST_CASE("Lee filter with zero noise variance returns input", "[speckle]") {
    std::vector<float> input(100, 75.0f);
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::leeFilter(input.data(), output.data(), 10, 10, 5, 0.0f);
    for (int i = 0; i < 100; i++) {
        REQUIRE(output[i] == Approx(75.0f).margin(0.1f));
    }
}

TEST_CASE("Lee filter output size matches input", "[speckle]") {
    std::vector<float> input(64, 50.0f);
    std::vector<float> output(64, 0.0f);
    ImageEnhancement::leeFilter(input.data(), output.data(), 8, 8, 3, 1.0f);
    // Should not crash, output should be valid
    for (int i = 0; i < 64; i++) {
        REQUIRE(std::isfinite(output[i]));
    }
}

// ---- Frost Filter Tests ----

TEST_CASE("Frost filter preserves uniform region", "[speckle]") {
    std::vector<float> input(100, 80.0f);
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::frostFilter(input.data(), output.data(), 10, 10, 5, 1.0f);
    for (int i = 0; i < 100; i++) {
        REQUIRE(output[i] == Approx(80.0f).margin(0.5f));
    }
}

TEST_CASE("Frost filter reduces noise", "[speckle]") {
    std::vector<float> input(100, 100.0f);
    std::vector<float> noisy(100);
    for (int i = 0; i < 100; i++) {
        float noise = (i % 2 == 0) ? 0.6f : -0.4f;
        noisy[i] = input[i] * (1.0f + noise);
    }
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::frostFilter(noisy.data(), output.data(), 10, 10, 5, 2.0f);
    float inputDev = std::abs(noisy[55] - 100.0f);
    float outputDev = std::abs(output[55] - 100.0f);
    REQUIRE(outputDev < inputDev);
}

TEST_CASE("Frost filter damping factor controls smoothing", "[speckle]") {
    // Higher damping = more smoothing
    std::vector<float> input(100);
    for (int i = 0; i < 100; i++)
        input[i] = 100.0f + ((i % 3) - 1) * 20.0f;
    std::vector<float> output1(100, 0.0f);
    std::vector<float> output2(100, 0.0f);
    ImageEnhancement::frostFilter(input.data(), output1.data(), 10, 10, 5, 1.0f);
    ImageEnhancement::frostFilter(input.data(), output2.data(), 10, 10, 5, 4.0f);
    // Higher damping should produce smoother (less variable) output
    float var1 = 0, var2 = 0;
    for (int i = 0; i < 100; i++) {
        var1 += (output1[i] - 100.0f) * (output1[i] - 100.0f);
        var2 += (output2[i] - 100.0f) * (output2[i] - 100.0f);
    }
    REQUIRE(var2 <= var1 + 1.0f);
}

TEST_CASE("Frost filter output is finite", "[speckle]") {
    std::vector<float> input(64, 50.0f);
    std::vector<float> output(64, 0.0f);
    ImageEnhancement::frostFilter(input.data(), output.data(), 8, 8, 3, 2.0f);
    for (int i = 0; i < 64; i++) {
        REQUIRE(std::isfinite(output[i]));
    }
}

// ---- Kuan Filter Tests ----

TEST_CASE("Kuan filter preserves uniform region", "[speckle]") {
    std::vector<float> input(100, 60.0f);
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::kuanFilter(input.data(), output.data(), 10, 10, 5, 1.0f);
    for (int i = 0; i < 100; i++) {
        REQUIRE(output[i] == Approx(60.0f).margin(0.1f));
    }
}

TEST_CASE("Kuan filter reduces noise", "[speckle]") {
    std::vector<float> input(100, 100.0f);
    std::vector<float> noisy(100);
    for (int i = 0; i < 100; i++) {
        float noise = (i % 2 == 0) ? 0.5f : -0.3f;
        noisy[i] = input[i] * (1.0f + noise);
    }
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::kuanFilter(noisy.data(), output.data(), 10, 10, 5, 1.0f);
    float inputDev = std::abs(noisy[55] - 100.0f);
    float outputDev = std::abs(output[55] - 100.0f);
    REQUIRE(outputDev < inputDev);
}

TEST_CASE("Kuan filter output is finite", "[speckle]") {
    std::vector<float> input(64, 50.0f);
    std::vector<float> output(64, 0.0f);
    ImageEnhancement::kuanFilter(input.data(), output.data(), 8, 8, 3, 1.0f);
    for (int i = 0; i < 64; i++) {
        REQUIRE(std::isfinite(output[i]));
    }
}

// ---- Gamma-MAP Filter Tests ----

TEST_CASE("Gamma-MAP filter preserves uniform region", "[speckle]") {
    std::vector<float> input(100, 90.0f);
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::gammaMapFilter(input.data(), output.data(), 10, 10, 5, 1.0f);
    for (int i = 0; i < 100; i++) {
        REQUIRE(output[i] == Approx(90.0f).margin(0.5f));
    }
}

TEST_CASE("Gamma-MAP filter reduces noise", "[speckle]") {
    std::vector<float> input(100, 100.0f);
    std::vector<float> noisy(100);
    for (int i = 0; i < 100; i++) {
        float noise = (i % 2 == 0) ? 0.4f : -0.3f;
        noisy[i] = input[i] * (1.0f + noise);
    }
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::gammaMapFilter(noisy.data(), output.data(), 10, 10, 5, 1.0f);
    float inputDev = std::abs(noisy[55] - 100.0f);
    float outputDev = std::abs(output[55] - 100.0f);
    REQUIRE(outputDev < inputDev);
}

TEST_CASE("Gamma-MAP filter output is non-negative for positive input", "[speckle]") {
    std::vector<float> input(100);
    for (int i = 0; i < 100; i++)
        input[i] = 50.0f + (i % 5) * 10.0f;
    std::vector<float> output(100, 0.0f);
    ImageEnhancement::gammaMapFilter(input.data(), output.data(), 10, 10, 5, 1.0f);
    for (int i = 0; i < 100; i++) {
        REQUIRE(output[i] >= 0.0f);
    }
}

TEST_CASE("Gamma-MAP filter output is finite", "[speckle]") {
    std::vector<float> input(64, 50.0f);
    std::vector<float> output(64, 0.0f);
    ImageEnhancement::gammaMapFilter(input.data(), output.data(), 8, 8, 3, 1.0f);
    for (int i = 0; i < 64; i++) {
        REQUIRE(std::isfinite(output[i]));
    }
}

// ---- Edge cases ----

TEST_CASE("Speckle filters handle small image", "[speckle]") {
    std::vector<float> input(9, 50.0f);
    input[4] = 200.0f; // center spike
    std::vector<float> output(9, 0.0f);

    ImageEnhancement::leeFilter(input.data(), output.data(), 3, 3, 3, 1.0f);
    REQUIRE(std::isfinite(output[4]));

    ImageEnhancement::frostFilter(input.data(), output.data(), 3, 3, 3, 2.0f);
    REQUIRE(std::isfinite(output[4]));

    ImageEnhancement::kuanFilter(input.data(), output.data(), 3, 3, 3, 1.0f);
    REQUIRE(std::isfinite(output[4]));

    ImageEnhancement::gammaMapFilter(input.data(), output.data(), 3, 3, 3, 1.0f);
    REQUIRE(std::isfinite(output[4]));
}

TEST_CASE("Speckle filters with kernel size 3", "[speckle]") {
    std::vector<float> input(100, 50.0f);
    std::vector<float> output(100, 0.0f);

    ImageEnhancement::leeFilter(input.data(), output.data(), 10, 10, 3, 1.0f);
    REQUIRE(output[55] == Approx(50.0f).margin(0.1f));

    ImageEnhancement::frostFilter(input.data(), output.data(), 10, 10, 3, 2.0f);
    REQUIRE(output[55] == Approx(50.0f).margin(0.5f));

    ImageEnhancement::kuanFilter(input.data(), output.data(), 10, 10, 3, 1.0f);
    REQUIRE(output[55] == Approx(50.0f).margin(0.1f));

    ImageEnhancement::gammaMapFilter(input.data(), output.data(), 10, 10, 3, 1.0f);
    REQUIRE(output[55] == Approx(50.0f).margin(0.5f));
}

TEST_CASE("Speckle filters with kernel size 7", "[speckle]") {
    std::vector<float> input(225, 75.0f);
    std::vector<float> output(225, 0.0f);

    ImageEnhancement::leeFilter(input.data(), output.data(), 15, 15, 7, 1.0f);
    REQUIRE(output[112] == Approx(75.0f).margin(0.1f));

    ImageEnhancement::enhancedLeeFilter(input.data(), output.data(), 15, 15, 7, 1.0f, 1.0f);
    REQUIRE(output[112] == Approx(75.0f).margin(0.1f));

    ImageEnhancement::frostFilter(input.data(), output.data(), 15, 15, 7, 2.0f);
    REQUIRE(output[112] == Approx(75.0f).margin(0.5f));
}

TEST_CASE("Enhanced Lee filter: uniform region, point target, and texture", "[speckle][enhanced_lee]") {
    const int W = 10, H = 10;
    std::vector<float> input(W * H, 100.0f);
    std::vector<float> output(W * H, 0.0f);

    // 1. Uniform region: should preserve mean
    ImageEnhancement::enhancedLeeFilter(input.data(), output.data(), W, H, 5, 1.0f, 1.0f);
    REQUIRE(output[55] == Approx(100.0f).margin(0.1f));

    // 2. Point target (isolated extreme spike): should be preserved without blurring
    input[55] = 5000.0f;
    ImageEnhancement::enhancedLeeFilter(input.data(), output.data(), W, H, 5, 0.1f, 1.0f);
    // Local Cl >> Cmax -> point target preserved
    REQUIRE(output[55] == Approx(5000.0f).margin(1.0f));

    // 3. Step edge (heterogeneous region): weighted transition
    std::vector<float> edge(W * H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            edge[y * W + x] = (x < 5) ? 20.0f : 200.0f;
        }
    }
    std::vector<float> edgeOut(W * H, 0.0f);
    ImageEnhancement::enhancedLeeFilter(edge.data(), edgeOut.data(), W, H, 5, 0.5f, 1.0f);
    REQUIRE(edgeOut[50] < 50.0f);   // Left side
    REQUIRE(edgeOut[59] > 150.0f);  // Right side
    REQUIRE(std::isfinite(edgeOut[55]));
}

TEST_CASE("Speckle filters: NaN preservation and mask handling", "[speckle][nodata]") {
    const int W = 8, H = 8;
    std::vector<float> input(W * H, 80.0f);
    // Inject NaNs at center and corner
    input[0] = std::numeric_limits<float>::quiet_NaN();
    input[3 * W + 3] = std::numeric_limits<float>::quiet_NaN();

    std::vector<float> outLee(W * H, 0.0f);
    std::vector<float> outEnhancedLee(W * H, 0.0f);
    std::vector<float> outFrost(W * H, 0.0f);
    std::vector<float> outKuan(W * H, 0.0f);
    std::vector<float> outGamma(W * H, 0.0f);

    ImageEnhancement::leeFilter(input.data(), outLee.data(), W, H, 3, 1.0f);
    ImageEnhancement::enhancedLeeFilter(input.data(), outEnhancedLee.data(), W, H, 3, 1.0f, 1.0f);
    ImageEnhancement::frostFilter(input.data(), outFrost.data(), W, H, 3, 2.0f);
    ImageEnhancement::kuanFilter(input.data(), outKuan.data(), W, H, 3, 1.0f);
    ImageEnhancement::gammaMapFilter(input.data(), outGamma.data(), W, H, 3, 1.0f);

    // Assert center and corner NaNs are strictly preserved
    REQUIRE(std::isnan(outLee[0]));
    REQUIRE(std::isnan(outLee[3 * W + 3]));
    REQUIRE(std::isnan(outEnhancedLee[0]));
    REQUIRE(std::isnan(outEnhancedLee[3 * W + 3]));
    REQUIRE(std::isnan(outFrost[0]));
    REQUIRE(std::isnan(outFrost[3 * W + 3]));
    REQUIRE(std::isnan(outKuan[0]));
    REQUIRE(std::isnan(outKuan[3 * W + 3]));
    REQUIRE(std::isnan(outGamma[0]));
    REQUIRE(std::isnan(outGamma[3 * W + 3]));

    // Assert adjacent valid pixels are finite and computed
    REQUIRE(std::isfinite(outLee[3 * W + 4]));
    REQUIRE(std::isfinite(outEnhancedLee[3 * W + 4]));
    REQUIRE(std::isfinite(outFrost[3 * W + 4]));
    REQUIRE(std::isfinite(outKuan[3 * W + 4]));
    REQUIRE(std::isfinite(outGamma[3 * W + 4]));
}
