// tests/test_continuum_removal.cpp — continuum removal & absorption feature tests (D13)
//
// Independent truths: closed-form Gaussian absorption math — depth D ≡ A,
// center λ0, FWHM = 2·√(2 ln 2)·σ, trough area = A·σ·√(2π) — plus hand-built
// multi-trough and flat spectra. Nothing is recomputed from the implementation.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analysis/hyperspectral/continuum_removal.h"

#include <cmath>
#include <vector>

using exp_spectral::ContinuumRemoval;
using exp_spectral::SpectralAbsorptionFeature;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    constexpr double kSqrt2Ln2 = 2.3548200450309493; // 2·√(2 ln 2)
    constexpr double kSqrt2Pi = 2.5066282746310002;
} // namespace

TEST_CASE("Continuum Removal Synthetic Gaussian Absorption Ground Truth", "[hyperspectral][continuum]")
{
    const int N = 601;
    std::vector<float> wavelengths(N);
    std::vector<float> reflectance(N);
    std::vector<float> continuum(N);
    std::vector<float> normalized(N);

    for (int i = 0; i < N; ++i)
    {
        float wl = 400.0f + i * 1.0f;
        wavelengths[i] = wl;
        float diff = wl - 670.0f;
        reflectance[i] = 1.0f - 0.60f * std::exp(-(diff * diff) / (2.0f * 20.0f * 20.0f));
    }

    bool ok = ContinuumRemoval::compute(wavelengths.data(), reflectance.data(), N,
                                        continuum.data(), normalized.data());
    REQUIRE(ok);

    // Flat unit continuum over the absorption: endpoints sit exactly on the hull.
    REQUIRE_THAT(normalized[0], WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(normalized[N - 1], WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(continuum[0], WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(continuum[N - 1], WithinAbs(1.0f, 1e-5f));

    auto features = ContinuumRemoval::extractFeatures(wavelengths.data(), normalized.data(), N);
    REQUIRE(features.size() == 1);
    REQUIRE_THAT(features[0].centerWavelengthNm, WithinAbs(670.0, 0.1));
    REQUIRE_THAT(features[0].absorptionDepth, WithinRel(0.60, 1e-4));
    REQUIRE_THAT(features[0].fwhmNm, WithinAbs(kSqrt2Ln2 * 20.0, 0.2)); // 47.09640 nm

    // Closed-form trough area: A·σ·√(2π) = 0.6·20·2.50663 = 30.0795 nm.
    REQUIRE_THAT(features[0].bandArea, WithinRel(0.60 * 20.0 * kSqrt2Pi, 1e-3));

    // A symmetric Gaussian trough is symmetric about its center.
    REQUIRE_THAT(features[0].asymmetry, WithinAbs(1.0, 0.05));

    // Half-depth crossings bracket the center symmetrically.
    REQUIRE_THAT(features[0].centerWavelengthNm - features[0].leftHalfWavelengthNm,
                 WithinAbs(features[0].rightHalfWavelengthNm - features[0].centerWavelengthNm, 0.2));
}

TEST_CASE("Continuum Removal keeps hull vertices at exactly 1.0", "[hyperspectral][continuum]")
{
    // Piecewise-linear spectrum with explicit peaks: the hull must touch the
    // peaks, so Rc is 1.0 there and strictly below in the valleys.
    const std::vector<float> wl = {400.f, 500.f, 600.f, 700.f, 800.f, 900.f, 1000.f};
    const std::vector<float> refl = {0.2f, 0.8f, 0.4f, 0.7f, 0.3f, 0.9f, 0.1f};
    std::vector<float> continuum(7, 0.f), normalized(7, 0.f);
    REQUIRE(ContinuumRemoval::compute(wl.data(), refl.data(), 7, continuum.data(), normalized.data()));

    // Hand-traced upper hull of these points: [400, 500, 900, 1000] — the
    // peaks at 500/900 and both endpoints form the envelope (700 is interior).
    std::vector<size_t> vertices;
    for (size_t i = 0; i < 7; ++i)
        if (std::abs(normalized[i] - 1.0f) < 1e-5)
            vertices.push_back(i);
    REQUIRE(vertices.size() >= 3);
    REQUIRE(std::find(vertices.begin(), vertices.end(), size_t{1}) != vertices.end()); // 500 nm peak
    REQUIRE(std::find(vertices.begin(), vertices.end(), size_t{5}) != vertices.end()); // 900 nm peak
    REQUIRE(std::find(vertices.begin(), vertices.end(), size_t{0}) != vertices.end()); // left endpoint

    // Every normalized value stays in (0, 1].
    for (size_t i = 0; i < 7; ++i)
    {
        REQUIRE(normalized[i] > 0.0f);
        REQUIRE(normalized[i] <= 1.0f + 1e-5f);
    }
}

TEST_CASE("Continuum Removal flat spectrum has no absorption and Rc ≡ 1", "[hyperspectral][continuum]")
{
    const int N = 10;
    std::vector<float> wl(N), refl(N, 0.35f), continuum(N), normalized(N);
    for (int i = 0; i < N; ++i)
        wl[i] = 500.0f + 10.0f * i;

    REQUIRE(ContinuumRemoval::compute(wl.data(), refl.data(), N, continuum.data(), normalized.data()));
    for (int i = 0; i < N; ++i)
        REQUIRE_THAT(normalized[i], WithinAbs(1.0f, 1e-6f));

    // No false absorption extraction on a flat spectrum.
    auto features = ContinuumRemoval::extractFeatures(wl.data(), normalized.data(), N);
    REQUIRE(features.empty());
}

TEST_CASE("Continuum Removal isolates multiple overlapping-free troughs", "[hyperspectral][continuum]")
{
    const int N = 1001; // 400..1400 nm, 1 nm step
    std::vector<float> wl(N), refl(N);
    for (int i = 0; i < N; ++i)
    {
        const float x = 400.0f + i;
        wl[i] = x;
        const float d1 = x - 550.0f; // σ = 25, A = 0.30
        const float d2 = x - 1150.0f; // σ = 25, A = 0.30
        refl[i] = 1.0f - 0.30f * std::exp(-(d1 * d1) / (2.f * 25.f * 25.f))
                        - 0.30f * std::exp(-(d2 * d2) / (2.f * 25.f * 25.f));
    }
    std::vector<float> continuum(N), normalized(N);
    REQUIRE(ContinuumRemoval::compute(wl.data(), refl.data(), N, continuum.data(), normalized.data()));

    auto features = ContinuumRemoval::extractFeatures(wl.data(), normalized.data(), N);
    REQUIRE(features.size() == 2);

    const SpectralAbsorptionFeature &a = features[0].centerWavelengthNm < features[1].centerWavelengthNm
                                           ? features[0] : features[1];
    const SpectralAbsorptionFeature &b = features[0].centerWavelengthNm < features[1].centerWavelengthNm
                                           ? features[1] : features[0];
    REQUIRE_THAT(a.centerWavelengthNm, WithinAbs(550.0, 0.5));
    REQUIRE_THAT(b.centerWavelengthNm, WithinAbs(1150.0, 0.5));
    REQUIRE_THAT(a.absorptionDepth, WithinRel(0.30, 1e-3));
    REQUIRE_THAT(b.absorptionDepth, WithinRel(0.30, 1e-3));
    // Gaussian FWHM truth for σ = 25: 58.87 nm.
    REQUIRE_THAT(a.fwhmNm, WithinAbs(kSqrt2Ln2 * 25.0, 0.5));
    REQUIRE_THAT(b.fwhmNm, WithinAbs(kSqrt2Ln2 * 25.0, 0.5));
}

TEST_CASE("Continuum Removal guards: invalid grids and NoData handling", "[hyperspectral][continuum]")
{
    const float wl[] = {400.f, 500.f, 600.f};
    const float refl[] = {0.5f, 0.4f, 0.5f};
    float continuum[3] = {0.f, 0.f, 0.f};
    float normalized[3] = {0.f, 0.f, 0.f};

    // Argument guards.
    REQUIRE_FALSE(ContinuumRemoval::compute(nullptr, refl, 3, continuum, normalized));
    REQUIRE_FALSE(ContinuumRemoval::compute(wl, nullptr, 3, continuum, normalized));
    REQUIRE_FALSE(ContinuumRemoval::compute(wl, refl, 3, nullptr, normalized));
    REQUIRE_FALSE(ContinuumRemoval::compute(wl, refl, 3, continuum, nullptr));
    REQUIRE_FALSE(ContinuumRemoval::compute(wl, refl, 1, continuum, normalized));

    // Non-increasing wavelength grid refuses.
    const float wlBad[] = {400.f, 400.f, 300.f};
    REQUIRE_FALSE(ContinuumRemoval::compute(wlBad, refl, 3, continuum, normalized));

    // NoData samples are excluded from the hull and flagged in the output.
    const float wlGap[] = {400.f, 500.f, 600.f, 700.f};
    const float reflGap[] = {0.5f, -9999.f, 0.4f, 0.6f};
    float c2[4], n2[4];
    REQUIRE(ContinuumRemoval::compute(wlGap, reflGap, 4, c2, n2));
    REQUIRE(n2[1] == -9999.f);

    auto features = ContinuumRemoval::extractFeatures(wlGap, n2, 4);
    REQUIRE(features.size() == 1); // the single valley between the hull points
}

TEST_CASE("Continuum Removal depth threshold filters shallow ripples", "[hyperspectral][continuum]")
{
    const int N = 201;
    std::vector<float> wl(N), normalized(N);
    for (int i = 0; i < N; ++i)
    {
        wl[i] = 400.f + i;
        normalized[i] = 1.0f - 0.01f; // uniform 1% ripple — below the 2% default
    }
    REQUIRE(ContinuumRemoval::extractFeatures(wl.data(), normalized.data(), N).empty());
    // Raising the ripple above the threshold makes it visible.
    for (int i = 0; i < N; ++i)
        normalized[i] = 1.0f - 0.05f;
    auto features = ContinuumRemoval::extractFeatures(wl.data(), normalized.data(), N);
    REQUIRE(features.size() == 1);
    REQUIRE_THAT(features[0].absorptionDepth, WithinRel(0.05, 1e-3));
}
