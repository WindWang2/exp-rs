// tests/test_fast_6s_atmospheric.cpp — 6S-style LUT & atmospheric inversion tests (D13)
//
// Independent truths: the mission's closed-form worked example (0.20/0.852),
// hand-worked DOS2 arithmetic, and qualitative radiative-transfer physics
// laws (aerosol loading ↑ ⇒ path reflectance ↑; water-vapor column ↑ ⇒
// 940 nm transmittance ↓). Nothing is recomputed from the implementation.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analysis/atmospheric/fast_6s_lookup.h"

#include <cmath>
#include <vector>

using exp_radiometric::Atmosphere6sParams;
using exp_radiometric::Fast6sLookup;
using exp_radiometric::Lut6sEntry;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("6S Analytical Inversion vs Math Ground Truth", "[atmospheric][6s]")
{
    // Mission closed form: rho* = 0.25, rho_a = 0.05, Ts = 0.90, Tv = 0.92, S = 0.12
    // → (0.25 - 0.05) / (0.90·0.92 + 0.12·0.20) = 0.20 / 0.852 = 0.23474178
    float toa = 0.25f;
    float boa = 0.0f;
    Lut6sEntry lut{0.05f, 0.90f, 0.92f, 0.12f};
    REQUIRE(Fast6sLookup::invertBoaReflectance(&toa, &boa, 1, lut));
    REQUIRE_THAT(boa, WithinRel(0.23474178f, 1e-5f));
}

TEST_CASE("6S inversion clamps unphysical reflectance to [0,1]", "[atmospheric][6s]")
{
    const Lut6sEntry lut{0.05f, 0.90f, 0.92f, 0.12f};

    float toaBelow = 0.02f; // below path reflectance → negative root clamps to 0
    float boa = 1.0f;
    REQUIRE(Fast6sLookup::invertBoaReflectance(&toaBelow, &boa, 1, lut));
    REQUIRE(boa == 0.0f);

    float toaBright = 1.0f; // raw root 1.0085 exceeds the physical unit bound
    REQUIRE(Fast6sLookup::invertBoaReflectance(&toaBright, &boa, 1, lut));
    REQUIRE(boa <= 1.0f);
    REQUIRE(boa > 0.99f);

    float toaPath = 0.05f; // exactly the path level: zero surface signal
    REQUIRE(Fast6sLookup::invertBoaReflectance(&toaPath, &boa, 1, lut));
    REQUIRE_THAT(boa, WithinAbs(0.0f, 1e-7f));
}

TEST_CASE("6S inversion sentinel and degenerate-denominator discipline", "[atmospheric][6s]")
{
    const Lut6sEntry lut{0.05f, 0.90f, 0.92f, 0.12f};

    // NoData passes through; NaN input stays NaN; null/empty buffers refuse.
    const float inputs[] = {-9999.0f, std::numeric_limits<float>::quiet_NaN()};
    float outs[2] = {0.f, 0.f};
    REQUIRE(Fast6sLookup::invertBoaReflectance(inputs, outs, 2, lut));
    REQUIRE(outs[0] == -9999.0f);
    REQUIRE(std::isnan(outs[1]));

    float boa = 0.f;
    REQUIRE_FALSE(Fast6sLookup::invertBoaReflectance(nullptr, &boa, 1, lut));
    REQUIRE_FALSE(Fast6sLookup::invertBoaReflectance(inputs, &boa, 0, lut));

    // Zero transmittance cannot be inverted — refuse, not divide-by-zero.
    const Lut6sEntry dead{0.05f, 0.0f, 0.0f, 0.12f};
    float toa = 0.25f;
    REQUIRE_FALSE(Fast6sLookup::invertBoaReflectance(&toa, &boa, 1, dead));
}

TEST_CASE("6S LUT honors radiative-transfer physics invariants", "[atmospheric][6s][lut]")
{
    // Higher aerosol loading must increase path reflectance at 550 nm.
    Atmosphere6sParams clean;
    clean.aod550 = 0.1;
    Atmosphere6sParams hazy;
    hazy.aod550 = 1.0;
    const Lut6sEntry cleanEntry = Fast6sLookup::interpolateCoefficients(550.0, clean);
    const Lut6sEntry hazyEntry = Fast6sLookup::interpolateCoefficients(550.0, hazy);
    REQUIRE(hazyEntry.atmosphericReflectance > cleanEntry.atmosphericReflectance);

    // More water vapor must deepen the 940 nm absorption band (lower T_down),
    // while leaving the 550 nm window (no water band) essentially untouched.
    Atmosphere6sParams dry;
    dry.waterVaporGcm2 = 0.2;
    Atmosphere6sParams humid;
    humid.waterVaporGcm2 = 5.0;
    const Lut6sEntry dry940 = Fast6sLookup::interpolateCoefficients(940.0, dry);
    const Lut6sEntry humid940 = Fast6sLookup::interpolateCoefficients(940.0, humid);
    REQUIRE(humid940.downwardTransmittance < dry940.downwardTransmittance);
    const Lut6sEntry dry550 = Fast6sLookup::interpolateCoefficients(550.0, dry);
    const Lut6sEntry humid550 = Fast6sLookup::interpolateCoefficients(550.0, humid);
    REQUIRE(std::abs(humid550.downwardTransmittance - dry550.downwardTransmittance) < 1e-3);

    // Oblique sun paths cross more atmosphere: 75° zenith transmits less than 0°.
    Atmosphere6sParams overhead;
    overhead.solarZenithDeg = 0.0;
    Atmosphere6sParams grazing;
    grazing.solarZenithDeg = 75.0;
    REQUIRE(Fast6sLookup::interpolateCoefficients(650.0, grazing).downwardTransmittance <
            Fast6sLookup::interpolateCoefficients(650.0, overhead).downwardTransmittance);

    // Every coefficient stays in its physical range across a parameter sweep.
    for (double wl = 400.0; wl <= 2500.0; wl += 137.0)
    {
        for (double aod : {0.01, 0.3, 2.0})
        {
            for (double wv : {0.2, 3.0, 5.0})
            {
                Atmosphere6sParams p;
                p.aod550 = aod;
                p.waterVaporGcm2 = wv;
                p.solarZenithDeg = 45.0;
                p.viewZenithDeg = 30.0;
                p.relAzimuthDeg = 90.0;
                const Lut6sEntry e = Fast6sLookup::interpolateCoefficients(wl, p);
                INFO("wl=" << wl << " aod=" << aod << " wv=" << wv);
                REQUIRE(e.atmosphericReflectance > 0.0f);
                REQUIRE(e.atmosphericReflectance < 1.0f);
                REQUIRE(e.downwardTransmittance > 0.0f);
                REQUIRE(e.downwardTransmittance <= 1.0f);
                REQUIRE(e.upwardTransmittance > 0.0f);
                REQUIRE(e.upwardTransmittance <= 1.0f);
                REQUIRE(e.sphericalAlbedo >= 0.0f);
                REQUIRE(e.sphericalAlbedo <= 0.5f);
            }
        }
    }
}

TEST_CASE("6S LUT clamps out-of-domain inputs to the grid boundary", "[atmospheric][6s][lut]")
{
    Atmosphere6sParams p;
    // Wavelength far below/above the grid must clamp without NaN or UB.
    const Lut6sEntry below = Fast6sLookup::interpolateCoefficients(100.0, p);
    const Lut6sEntry above = Fast6sLookup::interpolateCoefficients(5000.0, p);
    const Lut6sEntry lowEdge = Fast6sLookup::interpolateCoefficients(350.0, p);
    const Lut6sEntry highEdge = Fast6sLookup::interpolateCoefficients(2500.0, p);
    REQUIRE(below.atmosphericReflectance == lowEdge.atmosphericReflectance);
    REQUIRE(above.atmosphericReflectance == highEdge.atmosphericReflectance);

    // Parameter over-range clamps to the last grid axis value.
    Atmosphere6sParams extreme;
    extreme.aod550 = 99.0;
    extreme.waterVaporGcm2 = -5.0;
    Atmosphere6sParams edge;
    edge.aod550 = 2.0;
    edge.waterVaporGcm2 = 0.2;
    const Lut6sEntry clamped = Fast6sLookup::interpolateCoefficients(550.0, extreme);
    const Lut6sEntry atEdge = Fast6sLookup::interpolateCoefficients(550.0, edge);
    REQUIRE(clamped.atmosphericReflectance == atEdge.atmosphericReflectance);
}

TEST_CASE("DOS2 hand-worked arithmetic", "[atmospheric][dos2]")
{
    // Hand-worked: L = 0.3, L_path = 0.1, elevation 90°, esun = 2π, d = 1:
    // rho = π·(0.3 - 0.1) / (2π·1·1) = 0.1
    const float radiance = 0.3f;
    float boa = 0.0f;
    REQUIRE(Fast6sLookup::executeDos2(&radiance, &boa, 1, 0.1f, 90.0, 2.0 * 3.14159265358979323846, 1.0));
    REQUIRE_THAT(boa, WithinRel(0.1f, 1e-5f));
}

TEST_CASE("DOS2 dark-object floor, sentinels and guards", "[atmospheric][dos2]")
{
    const float radiances[] = {0.3f, 0.1f, 0.05f, -9999.0f, std::numeric_limits<float>::quiet_NaN()};
    float boa[5] = {0.f, 0.f, 0.f, 0.f, 0.f};
    REQUIRE(Fast6sLookup::executeDos2(radiances, boa, 5, 0.1f, 60.0, 1500.0, 1.0));
    REQUIRE(boa[1] == 0.0f);      // at the dark-object floor → zero reflectance
    REQUIRE(boa[2] == 0.0f);      // below the floor → clamped zero, never negative
    REQUIRE(boa[3] == -9999.0f);  // NoData passthrough
    REQUIRE(std::isnan(boa[4]));  // NaN input stays NaN
    REQUIRE(boa[0] > 0.0f);       // above the floor → positive reflectance

    float out = 0.f;
    const float r = 0.3f;
    REQUIRE_FALSE(Fast6sLookup::executeDos2(nullptr, &out, 1, 0.1f, 45.0, 1500.0, 1.0));
    REQUIRE_FALSE(Fast6sLookup::executeDos2(&r, &out, 0, 0.1f, 45.0, 1500.0, 1.0));
    REQUIRE_FALSE(Fast6sLookup::executeDos2(&r, &out, 1, 0.1f, 45.0, 0.0, 1.0));   // esun <= 0
    REQUIRE_FALSE(Fast6sLookup::executeDos2(&r, &out, 1, 0.1f, -20.0, 1500.0, 1.0)); // night
}
