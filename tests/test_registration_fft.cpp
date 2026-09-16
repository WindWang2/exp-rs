// test_registration_fft.cpp — F13 Phase 1: FFT primitive contract tests.
// Expected values are analytic: DFT of a constant is a delta at bin 0; DFT of
// cos(2πk·n/N) has magnitude N/2 at bins ±k; a circular shift in space is a
// linear phase in frequency; 2D round-trips are exact to round-off. Nothing
// is derived from the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/registration/fft2d.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace sicnu::registration;

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

TEST_CASE("fft2d power-of-two helpers", "[f13][fft]")
{
    REQUIRE(isPowerOfTwo(1));
    REQUIRE(isPowerOfTwo(64));
    REQUIRE_FALSE(isPowerOfTwo(0));
    REQUIRE_FALSE(isPowerOfTwo(96));

    REQUIRE(nextPowerOfTwo(1) == 1);
    REQUIRE(nextPowerOfTwo(37) == 64);
    REQUIRE(nextPowerOfTwo(128) == 128);
}

TEST_CASE("fft1d forward of constant is delta at bin 0", "[f13][fft]")
{
    std::vector<std::complex<double>> a(8, {2.0, 0.0});
    fft1d(a, /*inverse=*/false);
    for (std::size_t k = 0; k < a.size(); ++k) {
        const double expected = (k == 0) ? 16.0 : 0.0;
        REQUIRE_THAT(a[k].real(), WithinAbs(expected, 1e-9));
        REQUIRE_THAT(a[k].imag(), WithinAbs(0.0, 1e-9));
    }
}

TEST_CASE("fft1d forward of pure cosine puts N/2 at bins k and N-k", "[f13][fft]")
{
    // x(n) = cos(2π·1·n/8); spectrum has magnitude 4 at bins 1 and 7.
    constexpr std::size_t N = 8;
    std::vector<std::complex<double>> a(N);
    for (std::size_t n = 0; n < N; ++n)
        a[n] = std::cos(2.0 * kPi * 1.0 * static_cast<double>(n) / static_cast<double>(N));
    fft1d(a, false);
    for (std::size_t k = 0; k < N; ++k) {
        const double expected = (k == 1 || k == 7) ? 4.0 : 0.0;
        REQUIRE(std::abs(a[k]) < 4.0 + 1e-9);
        REQUIRE_THAT(std::abs(a[k]), WithinAbs(expected, 1e-9));
    }
}

TEST_CASE("fft1d round trip recovers the input", "[f13][fft]")
{
    std::vector<std::complex<double>> a;
    for (std::size_t n = 0; n < 16; ++n)
        a.emplace_back(0.25 * static_cast<double>(n) - 1.0,
                       std::sin(0.3 * static_cast<double>(n)));
    const auto original = a;
    fft1d(a, false);
    fft1d(a, true);
    for (std::size_t n = 0; n < a.size(); ++n) {
        REQUIRE_THAT(a[n].real(), WithinAbs(original[n].real() * 16.0, 1e-8));
        REQUIRE_THAT(a[n].imag(), WithinAbs(original[n].imag() * 16.0, 1e-8));
    }
}

TEST_CASE("fft2d of 2D delta is flat unit MAGNITUDE with linear phase", "[f13][fft]")
{
    // DFT of a delta at (r0, c0): F(u,v) = exp(-2πi(c0·v/C + r0·u/R)) —
    // magnitude exactly 1 everywhere; phase is linear, not zero.
    constexpr std::size_t R = 4, C = 8;
    std::vector<std::complex<double>> a(R * C, {0.0, 0.0});
    a[2 * C + 5] = {1.0, 0.0}; // delta at (row 2, col 5)
    fft2d(a, R, C, false);
    for (const auto& v : a)
        REQUIRE_THAT(std::abs(v), WithinAbs(1.0, 1e-9));
}

TEST_CASE("fft2d circular shift theorem (spatial shift = linear phase)", "[f13][fft]")
{
    // Cross-power spectrum of x(n) and x((n - d) mod N) must be
    // exp(+2πi·k·d/N) at every bin k; its inverse transform is a delta at
    // the shift d. This is the identity phase correlation relies on.
    constexpr int N = 8;
    constexpr int dX = 3, dY = 2;
    const auto shifted = [&](int y, int x) {
        return ((x - dX) % N + N) % N == 0 && ((y - dY) % N + N) % N == 0 ? 1.0 : 0.0;
    };

    std::vector<std::complex<double>> fa(N * N, {0.0, 0.0}), fb(N * N, {0.0, 0.0});
    fa[0] = {1.0, 0.0};
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x)
            fb[y * N + x] = {shifted(y, x), 0.0};

    fft2d(fa, N, N, false);
    fft2d(fb, N, N, false);

    std::vector<std::complex<double>> cross(N * N);
    for (int k = 0; k < N * N; ++k)
        cross[k] = fa[k] * std::conj(fb[k]);

    fft2d(cross, N, N, true);
    normalizeInverse2d(cross, N, N);

    double bestMag = -1.0;
    int bestIdx = -1;
    for (int k = 0; k < N * N; ++k) {
        if (std::abs(cross[k]) > bestMag) {
            bestMag = std::abs(cross[k]);
            bestIdx = k;
        }
    }
    REQUIRE(bestMag > 0.999);
    // cross = FA·conj(FB) with FB = FA·exp(-2πik·d/N) peaks at the WRAPPED
    // -d: (N-dY, N-dX). multimodal_matcher unwraps and negates this peak.
    const int ex = (N - dX) % N;
    const int ey = (N - dY) % N;
    REQUIRE(bestIdx == (ey * N + ex));
}

TEST_CASE("fft2d ignores non-power-of-two or mismatched extents", "[f13][fft]")
{
    std::vector<std::complex<double>> a(12, {1.0, 0.0});
    fft2d(a, 3, 4, false); // 3 is not a power of two: no-op
    for (const auto& v : a) {
        REQUIRE_THAT(v.real(), WithinAbs(1.0, 1e-12));
        REQUIRE_THAT(v.imag(), WithinAbs(0.0, 1e-12));
    }
    fft2d(a, 5, 4, false); // size mismatch: no-op
    for (const auto& v : a) {
        REQUIRE_THAT(v.real(), WithinAbs(1.0, 1e-12));
    }
}
