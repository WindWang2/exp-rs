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

TEST_CASE("Lee filter reduces noise on high-magnitude SAR data (#330)", "[speckle]") {
    // SAR amplitude data with mean ~1000 and speckle noise
    const int W = 10, H = 10;
    std::vector<float> input(W * H, 1000.0f);
    std::vector<float> noisy(W * H);
    for (int i = 0; i < W * H; ++i) {
        float noise = (i % 2 == 0) ? 0.3f : -0.2f;
        noisy[i] = input[i] * (1.0f + noise);
    }
    std::vector<float> output(W * H, 0.0f);
    // Relative noiseVariance = 0.1 (Cu^2 ~ 0.1)
    ImageEnhancement::leeFilter(noisy.data(), output.data(), W, H, 5, 0.1f);
    float inputDev = std::abs(noisy[55] - 1000.0f);
    float outputDev = std::abs(output[55] - 1000.0f);
    REQUIRE(outputDev < inputDev * 0.7f);
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

// ---- Lee vs Kuan regression (#678) ----

TEST_CASE("Lee filter uses Lee's own weighting, not Kuan's (#678)", "[speckle][678]") {
    // 3x3 ramp image. The kernels clamp the window rect to the raster
    // (localStats), so pixel [0]'s 3x3 window is the 2x2 corner {1,2,4,5}:
    //   mean = 3, var = (4+1+1+4)/4 = 2.5, Cl^2 = 2.5/9 = 0.27778.
    // noiseVariance = Cu^2 = 0.05 < Cl^2, so the weight is positive and the
    // two formulas diverge:
    //   Lee:  w = 1 - Cu^2/Cl^2            = 0.82
    //   Kuan: w = (1 - Cu^2/Cl^2)/(1+Cu^2) = 0.780952
    // Pixel value 1 -> Lee  = 3 + 0.82*(1-3)     = 1.36
    //                   Kuan = 3 + 0.780952*(1-3) = 1.438095
    std::vector<float> input = { 1.f, 2.f, 3.f,
                                 4.f, 5.f, 6.f,
                                 7.f, 8.f, 9.f };
    std::vector<float> outLee(9, 0.0f), outKuan(9, 0.0f);
    ImageEnhancement::leeFilter(input.data(), outLee.data(), 3, 3, 3, 0.05f);
    ImageEnhancement::kuanFilter(input.data(), outKuan.data(), 3, 3, 3, 0.05f);

    // Golden values (hand-computed above for the clamped corner window).
    CHECK(outLee[0] == Approx(1.36f).margin(1e-3));
    CHECK(outKuan[0] == Approx(1.438095f).margin(1e-3));

    // The two filters must no longer be bit-identical on a divergent window.
    // Corner pixel [0] has exact hand-computed weights (0.82 vs 0.780952):
    // divergence 0.0781. Interior-adjacent pixels whose window variance sits
    // just above Cu^2 legitimately diverge less, so the strong assertion is
    // made where the arithmetic is exact.
    CHECK(std::abs(outLee[0] - outKuan[0]) > 0.01f);
    int differing = 0;
    for (int i = 0; i < 9; ++i) {
        if (i == 4) continue;  // center pixel == mean -> both output the mean
        if (std::abs(outLee[i] - outKuan[i]) > 1e-4f)
            ++differing;
    }
    CHECK(differing >= 7); // Lee != Kuan across the raster, not one fluke pixel

    // Flat region: Cl^2 = 0 <= Cu^2 -> both smooth fully to the mean.
    std::vector<float> flat(9, 5.0f);
    std::vector<float> flatLee(9, 0.0f), flatKuan(9, 0.0f);
    ImageEnhancement::leeFilter(flat.data(), flatLee.data(), 3, 3, 3, 0.05f);
    ImageEnhancement::kuanFilter(flat.data(), flatKuan.data(), 3, 3, 3, 0.05f);
    for (int i = 0; i < 9; ++i) {
        CHECK(flatLee[i] == Approx(5.0f).margin(1e-4));
        CHECK(flatKuan[i] == Approx(5.0f).margin(1e-4));
    }
}

// ---------------------------------------------------------------------------
// Streaming dialog path (#691): the speckle filter dialog now streams each
// band through halo tiles (ImageEnhancementStreaming) instead of materializing
// full frames + an IntegralImage SAT. The full-frame ImageEnhancement kernels
// are the oracle: the tile kernels must reproduce their output up to
// floating-point summation order (last-ULP; tested with a 1e-3 margin).
// ---------------------------------------------------------------------------

#include "processing/algorithms/image_enhancement_streaming.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QTemporaryDir>
#include <QString>

#include <gdal.h>

#include <array>
#include <limits>
#include <optional>

namespace IES = ImageEnhancementStreaming;

namespace
{

// Deterministic SAR-like raster with NaN holes, written to @a path.
std::vector<std::vector<float>> makeSpeckleRaster( const QString &path, int w, int h, int bands )
{
    std::vector<std::vector<float>> data( bands, std::vector<float>( static_cast<size_t>( w ) * h ) );
    for ( int b = 0; b < bands; ++b )
    {
        for ( int y = 0; y < h; ++y )
        {
            for ( int x = 0; x < w; ++x )
            {
                const size_t i = static_cast<size_t>( y ) * w + x;
                float v = 40.0f + 12.0f * static_cast<float>( ( x * 7 + y * 13 + b * 29 ) % 17 )
                        + 5.0f * static_cast<float>( ( x + y + b ) % 3 );
                if ( ( x * 31 + y * 17 + b ) % 53 == 0 )
                    v = std::numeric_limits<float>::quiet_NaN();
                data[b][i] = v;
            }
        }
    }
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    REQUIRE( writeGdalOutput( path, w, h, data, gt, QString(), &err ) );
    return data;
}

std::vector<std::vector<float>> readRasterBands( const QString &path, int w, int h, int bands )
{
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( path ) );
    std::vector<std::vector<float>> out( bands, std::vector<float>( static_cast<size_t>( w ) * h ) );
    for ( int b = 1; b <= bands; ++b )
        REQUIRE( ds.readBandData( b, out[static_cast<size_t>( b - 1 )].data(), w, h ) );
    return out;
}

void requireCloseOrBothNan( float a, float b )
{
    if ( std::isnan( a ) && std::isnan( b ) )
        return;
    REQUIRE( a == Approx( b ).margin( 1e-3 ) );
}

void runSpeckleStreamingCase( int filterIndex, int kernelSize, int tileDim, float param )
{
    ensureGdalInit();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const int w = 37, h = 41, bands = 2;
    const QString srcPath = tmp.path() + QStringLiteral( "/src.tif" );
    const QString dstPath = tmp.path() + QStringLiteral( "/dst.tif" );
    const std::vector<std::vector<float>> input = makeSpeckleRaster( srcPath, w, h, bands );

    // Oracle: the full-frame kernels the dialog used before the conversion.
    std::vector<std::vector<float>> oracle( bands );
    for ( int b = 0; b < bands; ++b )
    {
        oracle[b].assign( static_cast<size_t>( w ) * h, 0.0f );
        switch ( filterIndex )
        {
            case 0: ImageEnhancement::leeFilter( input[b].data(), oracle[b].data(), w, h, kernelSize, param ); break;
            case 1: ImageEnhancement::frostFilter( input[b].data(), oracle[b].data(), w, h, kernelSize, param ); break;
            case 2: ImageEnhancement::kuanFilter( input[b].data(), oracle[b].data(), w, h, kernelSize, param ); break;
            case 3: ImageEnhancement::gammaMapFilter( input[b].data(), oracle[b].data(), w, h, kernelSize, param ); break;
        }
    }

    // Streaming: one band at a time through halo tiles (same loop shape as
    // SpeckleFilterDialog::onRun).
    GdalDatasetWrapper src;
    REQUIRE( src.open( srcPath ) );
    GdalStreamingOutput dst( dstPath, w, h, bands, GDT_Float32, src.geoTransform(), src.projection() );
    REQUIRE( dst.isOpen() );
    for ( int b = 1; b <= bands; ++b )
    {
        IES::WindowedTileFn kernel;
        switch ( filterIndex )
        {
            case 0: kernel = [kernelSize, param]( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                        IES::speckleTileLee( t, buf, core, kernelSize, param ); }; break;
            case 1: kernel = [kernelSize, param]( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                        IES::speckleTileFrost( t, buf, core, kernelSize, param ); }; break;
            case 2: kernel = [kernelSize, param]( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                        IES::speckleTileKuan( t, buf, core, kernelSize, param ); }; break;
            case 3: kernel = [kernelSize, param]( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                        IES::speckleTileGammaMap( t, buf, core, kernelSize, param ); }; break;
        }
        REQUIRE( IES::streamBandWindowed( src, b, dst, tileDim, kernelSize / 2, kernel ) );
    }
    REQUIRE( dst.closeWithError( nullptr ) );

    const std::vector<std::vector<float>> got = readRasterBands( dstPath, w, h, bands );
    for ( int b = 0; b < bands; ++b )
        for ( size_t i = 0; i < got[b].size(); ++i )
            requireCloseOrBothNan( got[b][i], oracle[b][i] );
}

} // namespace

TEST_CASE( "Streaming speckle tiles match full-frame kernels (multi-tile)", "[speckle][streaming]" )
{
    // noiseVariance 0.1 keeps Lee's weight strictly inside (0,1) — with 1.0
    // the weight collapses to 0 and the section passes vacuously (review P0).
    SECTION( "Lee 5x5" )        { runSpeckleStreamingCase( 0, 5, 16, 0.1f ); }
    SECTION( "Frost 5x5" )      { runSpeckleStreamingCase( 1, 5, 16, 2.0f ); }
    SECTION( "Kuan 3x3" )       { runSpeckleStreamingCase( 2, 3, 16, 1.0f ); }
    SECTION( "Gamma-MAP 7x7" )  { runSpeckleStreamingCase( 3, 7, 16, 1.0f ); }
}

TEST_CASE( "Streaming speckle tiles match full-frame kernels (single tile)", "[speckle][streaming]" )
{
    runSpeckleStreamingCase( 0, 5, 256, 0.1f );
}

TEST_CASE( "Streaming speckle preserves NaN mask through tiles", "[speckle][streaming][nodata]" )
{
    ensureGdalInit();
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const int w = 20, h = 20;
    const QString srcPath = tmp.path() + QStringLiteral( "/src.tif" );
    const QString dstPath = tmp.path() + QStringLiteral( "/dst.tif" );
    makeSpeckleRaster( srcPath, w, h, 1 );

    GdalDatasetWrapper src;
    REQUIRE( src.open( srcPath ) );
    GdalStreamingOutput dst( dstPath, w, h, 1, GDT_Float32, src.geoTransform(), src.projection() );
    REQUIRE( dst.isOpen() );
    REQUIRE( IES::streamBandWindowed( src, 1, dst, 8, 1,
                                      []( const GdalBlockStream::Tile &t, const float *buf, float *core ) {
                                          IES::speckleTileLee( t, buf, core, 3, 1.0f );
                                      } ) );
    REQUIRE( dst.closeWithError( nullptr ) );

    const std::vector<std::vector<float>> got = readRasterBands( dstPath, w, h, 1 );
    // The synthetic pattern plants NaN at (x*31 + y*17) % 53 == 0 cells, e.g.
    // (x=3, y=7): 93 + 119 = 212 = 4*53.
    REQUIRE( std::isnan( got[0][static_cast<size_t>( 7 ) * 20 + 3] ) );
    // Generic check: every NaN planted in the input is NaN in the output.
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
            if ( ( x * 31 + y * 17 ) % 53 == 0 )
                REQUIRE( std::isnan( got[0][static_cast<size_t>( y ) * 20 + x] ) );
}
