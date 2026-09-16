// tests/test_spectral_unmixing_fcls.cpp — FCLS unmixing & endmember extraction tests (D13)
//
// Independent truths: hand-computed linear mixtures with known abundance
// vectors (y = Σ fᵢ·mᵢ exactly satisfies Σf = 1, f ≥ 0), geometric simplex
// facts for PPI/VCA, and rank/argument refusal laws.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/spectral_unmixing.h"

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace exp_spectral;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    // 4-band, 3-endmember ground truth from the mission contract.
    const std::vector<float> kEndmembers = {
        0.10f, 0.20f, 0.30f, 0.40f, // E1 (water/soil)
        0.05f, 0.10f, 0.60f, 0.70f, // E2 (vegetation)
        0.50f, 0.50f, 0.50f, 0.50f, // E3 (impervious)
    };

    // y = 0.5·E1 + 0.3·E2 + 0.2·E3 = [0.165, 0.230, 0.430, 0.510]
    const std::vector<float> kMixedPixel = { 0.165f, 0.230f, 0.430f, 0.510f };

    /// Builds a small cube: pure E1/E2/E3 pixels followed by pairwise mixtures.
    std::vector<float> buildCube()
    {
        auto mix = []( float f1, float f2, float f3 ) {
            std::vector<float> y( 4 );
            for ( int b = 0; b < 4; ++b )
                y[b] = f1 * kEndmembers[b] + f2 * kEndmembers[4 + b] + f3 * kEndmembers[8 + b];
            return y;
        };
        std::vector<float> cube;
        const auto append = [&cube]( const std::vector<float> &v ) {
            cube.insert( cube.end(), v.begin(), v.end() );
        };
        append( { kEndmembers.begin(), kEndmembers.begin() + 4 } );  // pure E1
        append( { kEndmembers.begin() + 4, kEndmembers.begin() + 8 } ); // pure E2
        append( { kEndmembers.begin() + 8, kEndmembers.end() } );    // pure E3
        append( mix( 0.5f, 0.5f, 0.0f ) );
        append( mix( 0.0f, 0.5f, 0.5f ) );
        append( mix( 0.2f, 0.5f, 0.3f ) );
        return cube;
    }
} // namespace

TEST_CASE("FCLS Unmixing Pure Synthetic Ground Truth", "[spectral][unmixing]")
{
    const int bands = 4;
    const int p = 3;
    std::vector<float> pixel = kMixedPixel;

    UnmixingResult result;
    bool ok = exp_spectral::SpectralUnmixing::unmixFcls(pixel.data(), 1, bands, kEndmembers.data(), p, &result);
    REQUIRE(ok);

    // Invariant 1: sum to one (measured QA metric agrees).
    float sumF = result.abundances[0] + result.abundances[1] + result.abundances[2];
    REQUIRE_THAT(sumF, WithinAbs(1.0f, 1e-5f));
    REQUIRE(result.meanSumConstraintViolation < 1e-5);

    // Invariant 2: non-negativity (ANC holds exactly).
    for (int e = 0; e < p; ++e)
        REQUIRE(result.abundances[e] >= 0.0f);

    // Invariant 3: recovers the true mixture.
    REQUIRE_THAT(result.abundances[0], WithinRel(0.50f, 1e-4f));
    REQUIRE_THAT(result.abundances[1], WithinRel(0.30f, 1e-4f));
    REQUIRE_THAT(result.abundances[2], WithinRel(0.20f, 1e-4f));

    // Invariant 4: the noiseless mixture reconstructs to machine precision.
    REQUIRE(result.reconstructionError[0] < 1e-5f);
}

TEST_CASE("FCLS Unmixing batch recovers every pixel independently", "[spectral][unmixing]")
{
    const std::vector<float> cube = buildCube();
    const size_t pixels = cube.size() / 4;
    UnmixingResult result;
    REQUIRE(exp_spectral::SpectralUnmixing::unmixFcls(cube.data(), pixels, 4, kEndmembers.data(), 3, &result));

    REQUIRE(result.abundances.size() == pixels * 3);
    REQUIRE(result.reconstructionError.size() == pixels);

    const std::vector<std::array<float, 3>> truth = {
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
          { 0.5f, 0.5f, 0.0f }, { 0.0f, 0.5f, 0.5f }, { 0.2f, 0.5f, 0.3f } } };
    for (size_t pIx = 0; pIx < pixels; ++pIx)
    {
        INFO("pixel " << pIx);
        float sum = 0.0f;
        for (int e = 0; e < 3; ++e)
        {
            const float got = result.abundances[pIx * 3 + e];
            REQUIRE(got >= 0.0f);
            sum += got;
            REQUIRE_THAT(got, WithinAbs(truth[pIx][e], 1e-4f));
        }
        REQUIRE_THAT(sum, WithinAbs(1.0f, 1e-4f));
        REQUIRE(result.reconstructionError[pIx] < 1e-5f);
    }
}

TEST_CASE("FCLS enforces non-negativity when the true mixture is out of the simplex", "[spectral][unmixing]")
{
    // y = 2·E3 - 1·E1 is outside the physical simplex; FCLS must still return
    // a feasible point (f >= 0, Σ = 1) rather than the infeasible optimum.
    std::vector<float> pixel( 4 );
    for (int b = 0; b < 4; ++b)
        pixel[b] = 2.0f * kEndmembers[8 + b] - kEndmembers[b];

    UnmixingResult result;
    REQUIRE(exp_spectral::SpectralUnmixing::unmixFcls(pixel.data(), 1, 4, kEndmembers.data(), 3, &result));
    float sum = 0.0f;
    for (int e = 0; e < 3; ++e)
    {
        REQUIRE(result.abundances[e] >= 0.0f);
        sum += result.abundances[e];
    }
    REQUIRE_THAT(sum, WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("FCLS refuses degenerate endmember sets with named errors", "[spectral][unmixing]")
{
    // Rank-deficient: E2 duplicates E1 → the sum constraint is unidentifiable.
    std::vector<float> collinear = {
        0.10f, 0.20f, 0.30f, 0.40f,
        0.10f, 0.20f, 0.30f, 0.40f,
        0.50f, 0.50f, 0.50f, 0.50f,
    };
    UnmixingResult result;
    QString error;
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::unmixFcls(kMixedPixel.data(), 1, 4, collinear.data(), 3, &result, &error));
    REQUIRE(!error.isEmpty());

    // Zero-norm endmember carries no information → refusal, not garbage.
    std::vector<float> withZero = kEndmembers;
    withZero[4] = withZero[5] = withZero[6] = withZero[7] = 0.0f;
    QString zeroError;
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::unmixFcls(kMixedPixel.data(), 1, 4, withZero.data(), 3, &result, &zeroError));
    REQUIRE(!zeroError.isEmpty());

    // Argument guards.
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::unmixFcls(nullptr, 1, 4, kEndmembers.data(), 3, &result));
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::unmixFcls(kMixedPixel.data(), 0, 4, kEndmembers.data(), 3, &result));
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::unmixFcls(kMixedPixel.data(), 1, 4, kEndmembers.data(), 0, &result));
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::unmixFcls(kMixedPixel.data(), 1, 4, kEndmembers.data(), 3, nullptr));
}

TEST_CASE("PPI endmember extraction recovers pure spectra as a set", "[spectral][unmixing][ppi]")
{
    const std::vector<float> cube = buildCube();
    std::vector<float> endmembers;
    REQUIRE(exp_spectral::SpectralUnmixing::extractEndmembers(cube.data(), cube.size() / 4, 4, 3,
                                                EndmemberExtractionMethod::PixelPurityIndex,
                                                &endmembers));
    REQUIRE(endmembers.size() == 3 * 4);

    // Order is unspecified: every pure spectrum must appear (set equality).
    auto sameSpectrum = [&]( const float *a, const float *b ) {
        for (int i = 0; i < 4; ++i)
            if (std::abs(a[i] - b[i]) > 1e-6f)
                return false;
        return true;
    };
    for (int e = 0; e < 3; ++e)
    {
        const float *found = endmembers.data() + e * 4;
        const bool matched = sameSpectrum(found, kEndmembers.data())
                             || sameSpectrum(found, kEndmembers.data() + 4)
                             || sameSpectrum(found, kEndmembers.data() + 8);
        INFO("endmember slot " << e);
        REQUIRE(matched);
    }
}

TEST_CASE("VCA endmember extraction recovers simplex vertices", "[spectral][unmixing][vca]")
{
    const std::vector<float> cube = buildCube();
    std::vector<float> endmembers;
    REQUIRE(exp_spectral::SpectralUnmixing::extractEndmembers(cube.data(), cube.size() / 4, 4, 3,
                                                EndmemberExtractionMethod::VertexComponentAnalysis,
                                                &endmembers));
    REQUIRE(endmembers.size() == 3 * 4);

    auto sameSpectrum = [&]( const float *a, const float *b ) {
        for (int i = 0; i < 4; ++i)
            if (std::abs(a[i] - b[i]) > 1e-5f)
                return false;
        return true;
    };
    for (int e = 0; e < 3; ++e)
    {
        const float *found = endmembers.data() + e * 4;
        const bool matched = sameSpectrum(found, kEndmembers.data())
                             || sameSpectrum(found, kEndmembers.data() + 4)
                             || sameSpectrum(found, kEndmembers.data() + 8);
        INFO("endmember slot " << e);
        REQUIRE(matched);
    }

    // Degenerate cube (all pixels identical): fewer vertices exist than asked.
    std::vector<float> flat(4 * 6, 0.25f);
    std::vector<float> degenerate;
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::extractEndmembers(flat.data(), 6, 4, 3,
                                                      EndmemberExtractionMethod::VertexComponentAnalysis,
                                                      &degenerate));

    // Argument guards.
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::extractEndmembers(nullptr, 6, 4, 3,
                                                      EndmemberExtractionMethod::VertexComponentAnalysis,
                                                      &degenerate));
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::extractEndmembers(cube.data(), cube.size() / 4, 4, 0,
                                                      EndmemberExtractionMethod::VertexComponentAnalysis,
                                                      &degenerate));
    REQUIRE_FALSE(exp_spectral::SpectralUnmixing::extractEndmembers(cube.data(), cube.size() / 4, 4, 3,
                                                      EndmemberExtractionMethod::VertexComponentAnalysis,
                                                      nullptr));
}
