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

TEST_CASE("Generic convolution with custom kernel", "[spatial][convolve]") {
    const int W = 5, H = 5;
    std::vector<float> input(W * H, 10.0f);
    std::vector<float> output(W * H, 0.0f);

    // 3x3 identity kernel
    const float identity[9] = {
        0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    ImageEnhancement::convolve(input.data(), output.data(), W, H, identity, 3);
    for (int i = 0; i < W * H; ++i) {
        REQUIRE(output[i] == Approx(10.0f));
    }

    // 3x3 box blur kernel
    const float box[9] = {
        1.0f/9.0f, 1.0f/9.0f, 1.0f/9.0f,
        1.0f/9.0f, 1.0f/9.0f, 1.0f/9.0f,
        1.0f/9.0f, 1.0f/9.0f, 1.0f/9.0f
    };
    std::vector<float> meanOut(W * H, 0.0f);
    ImageEnhancement::convolve(input.data(), output.data(), W, H, box, 3);
    ImageEnhancement::meanFilter(input.data(), meanOut.data(), W, H, 3);
    for (int i = 0; i < W * H; ++i) {
        REQUIRE(output[i] == Approx(meanOut[i]).margin(1e-4f));
    }
}

TEST_CASE("Sobel single-pass filter matches 2D convolution magnitude", "[spatial][sobel]") {
    const int W = 8, H = 8;
    std::vector<float> input(W * H);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            input[y * W + x] = static_cast<float>(y * 10 + x * 2);
        }
    }
    std::vector<float> fastSobel(W * H, 0.0f);
    ImageEnhancement::sobelFilter(input.data(), fastSobel.data(), W, H);

    // Reference 2D convolution
    const float sobelX[9] = { -1, 0, 1, -2, 0, 2, -1, 0, 1 };
    const float sobelY[9] = { -1, -2, -1, 0, 0, 0, 1, 2, 1 };
    std::vector<float> gx(W * H), gy(W * H);
    ImageEnhancement::convolve(input.data(), gx.data(), W, H, sobelX, 3);
    ImageEnhancement::convolve(input.data(), gy.data(), W, H, sobelY, 3);

    for (int i = 0; i < W * H; ++i) {
        float refMag = std::sqrt(gx[i] * gx[i] + gy[i] * gy[i]);
        REQUIRE(fastSobel[i] == Approx(refMag).margin(1e-4f));
    }
}

TEST_CASE("Laplacian single-pass filter matches 2D convolution stencil", "[spatial][laplacian]") {
    const int W = 7, H = 9;
    std::vector<float> input(W * H);
    for (int i = 0; i < W * H; ++i) {
        input[i] = static_cast<float>((i * 37) % 100);
    }
    std::vector<float> fastLaplacian(W * H, 0.0f);
    std::vector<float> refLaplacian(W * H, 0.0f);

    ImageEnhancement::laplacianFilter(input.data(), fastLaplacian.data(), W, H);

    const float lapKernel[9] = { 0, 1, 0, 1, -4, 1, 0, 1, 0 };
    ImageEnhancement::convolve(input.data(), refLaplacian.data(), W, H, lapKernel, 3);

    for (int i = 0; i < W * H; ++i) {
        REQUIRE(fastLaplacian[i] == Approx(refLaplacian[i]).margin(1e-4f));
    }
}

TEST_CASE("Spatial filters on larger kernels 5x5 and 7x7", "[spatial][kernels]") {
    const int W = 16, H = 16;
    std::vector<float> input(W * H, 42.0f);
    std::vector<float> outMean5(W * H, 0.0f), outMean7(W * H, 0.0f);
    std::vector<float> outGauss5(W * H, 0.0f), outGauss7(W * H, 0.0f);
    std::vector<float> outMed5(W * H, 0.0f), outMed7(W * H, 0.0f);

    ImageEnhancement::meanFilter(input.data(), outMean5.data(), W, H, 5);
    ImageEnhancement::meanFilter(input.data(), outMean7.data(), W, H, 7);
    ImageEnhancement::gaussianFilter(input.data(), outGauss5.data(), W, H, 5, 1.5f);
    ImageEnhancement::gaussianFilter(input.data(), outGauss7.data(), W, H, 7, 2.0f);
    ImageEnhancement::medianFilter(input.data(), outMed5.data(), W, H, 5);
    ImageEnhancement::medianFilter(input.data(), outMed7.data(), W, H, 7);

    for (int i = 0; i < W * H; ++i) {
        REQUIRE(outMean5[i] == Approx(42.0f).margin(1e-4f));
        REQUIRE(outMean7[i] == Approx(42.0f).margin(1e-4f));
        REQUIRE(outGauss5[i] == Approx(42.0f).margin(1e-4f));
        REQUIRE(outGauss7[i] == Approx(42.0f).margin(1e-4f));
        REQUIRE(outMed5[i] == Approx(42.0f).margin(1e-4f));
        REQUIRE(outMed7[i] == Approx(42.0f).margin(1e-4f));
    }
}

TEST_CASE("Convolve NoData handling differs for averaging vs derivative kernels (#442)", "[spatial][convolve][nodata]")
{
    // 5x1 row (H=1 -> vertical neighbors clamp to the same row):
    // in = [NaN, 10, 0, 10, 5]
    const int W = 5, H = 1;
    std::vector<float> in = {
        std::numeric_limits<float>::quiet_NaN(), 10.0f, 0.0f, 10.0f, 5.0f,
    };

    std::vector<float> outAvg(W * H), outZero(W * H);
    const float avg[9] = {0, 0, 0, 0, 1.0f, 0, 0, 0, 0}; // identity (sum=1)
    const float lap[9] = {0, 1, 0, 1, -4, 1, 0, 1, 0};   // zero-sum derivative

    ImageEnhancement::convolve(in.data(), outAvg.data(), W, H, avg, 3);
    ImageEnhancement::convolve(in.data(), outZero.data(), W, H, lap, 3);

    // Averaging kernel: NaN pixel propagates; finite pixels keep values.
    REQUIRE(std::isnan(outAvg[0]));
    CHECK(outAvg[2] == Approx(0.0f).margin(1e-5f));

    // Zero-sum kernel: raw sum with NaN neighbors skipped (no renorm NaN):
    // x=1: up(10) + down(10) + right(0) - 4*center(10) = -20  (left is NaN, skipped)
    REQUIRE(std::isnan(outZero[0]));
    CHECK(outZero[1] == Approx(-20.0f).margin(1e-4f));
    // x=2 interior: up(0) + down(0) + left(10) + right(10) - 4*0 = 20
    CHECK(outZero[2] == Approx(20.0f).margin(1e-4f));
}
