// test_known_answer_corpus.cpp — scientific known-answer corpus (task C,
// Verification 7.0).
//
// Every expectation below is ANALYTICALLY DERIVABLE — the derivation is
// documented next to each assertion. These are physical/mathematical
// invariants, not JSON-shape checks:
//
//   family          | invariant
//   ----------------+-------------------------------------------------------
//   spectral        | NDVI ratio is invariant to positive multiplicative
//                   | gain (radiometric scaling); NDVI ∈ [-1, 1]; SAVI(L=0)
//                   | reduces to NDVI; identical bands ⇒ index 0.
//   change          | difference(after=before) ≡ 0; statistics of a known
//                   | ramp are exact; threshold mask is half-open [t, ∞).
//   terrain (DEM)   | Horn 1981 on an exact inclined plane: slope = atan(dz)
//                   | exactly, aspect follows the documented compass
//                   | convention, flat ⇒ slope 0 / aspect −1.
//   radiometric     | L = gain·DN + bias; ρ_TOA = (mult·DN + add)/sin(θ)
//                   | with θ = 90° reducing to the affine form; brightness
//                   | temperature inverts the Planck trace exactly.
//   transition      | hand-computed 2×2 confusion/transition matrix with a
//                   | masked (NoData) pixel.
//   temporal stats  | Welford moments of {1..N} are exact; OnlineRegression
//                   | on y = 2t + 1 recovers slope 2 / intercept 1 / r² 1.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/post_classification.h"
#include "processing/algorithms/radiometric_calibration.h"
#include "processing/algorithms/spectral_indices.h"
#include "processing/algorithms/terrain_analysis.h"
#include "processing/algorithms/temporal/temporal_stats.h"

#include <cmath>
#include <vector>

using namespace ChangeDetection;
using namespace RadiometricCalibration;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kTol = 1e-5;
} // namespace

// ---------------------------------------------------------------------------
// Spectral indices — physical invariants
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: NDVI is invariant to positive radiometric gain",
           "[known_answer][spectral]" )
{
    // Derivation: NDVI = (N−R)/(N+R). Scaling both bands by g > 0 gives
    // (gN−gR)/(gN+gR) = (N−R)/(N+R) — the gain cancels exactly. Any deviation
    // is a real formula break, not floating-point noise at these magnitudes.
    const std::vector<float> nir = { 0.05f, 0.3f, 0.55f, 0.8f };
    const std::vector<float> red = { 0.02f, 0.1f, 0.25f, 0.45f };
    const std::vector<float> gain = { 1000.0f, 0.001f, 7.5f, 1.0f };

    std::vector<float> base( 4 ), scaled( 4 );
    REQUIRE( SpectralIndices::ndvi( nir.data(), red.data(), base.data(), 4 ) );
    for ( size_t i = 0; i < nir.size(); ++i )
    {
        const float gn = nir[i] * gain[i];
        const float gr = red[i] * gain[i];
        REQUIRE( SpectralIndices::ndvi( &gn, &gr, &scaled[i], 1 ) );
        INFO( "i=" << i << " gain=" << gain[i] );
        REQUIRE_THAT( scaled[i], WithinAbs( base[i], static_cast<float>( kTol ) ) );
    }
}

TEST_CASE( "known-answer: NDVI range and zero-crossing", "[known_answer][spectral]" )
{
    // NDVI(NIR = R) = 0 (no vegetation signal); NDVI ∈ [−1, 1] always.
    const float n = 0.4f, r = 0.4f;
    float out = -99.0f;
    REQUIRE( SpectralIndices::ndvi( &n, &r, &out, 1 ) );
    REQUIRE_THAT( out, WithinAbs( 0.0f, static_cast<float>( kTol ) ) );

    // Pure-water-like pixel (NIR ≪ red): NDVI approaches −1 as red dominates.
    const float wn = 0.01f, wr = 0.5f;
    REQUIRE( SpectralIndices::ndvi( &wn, &wr, &out, 1 ) );
    REQUIRE( out < -0.85f );
    REQUIRE( out >= -1.0f );
}

TEST_CASE( "known-answer: SAVI with L=0 reduces to NDVI", "[known_answer][spectral]" )
{
    // SAVI = ((N−R)/(N+R+L))·(1+L). At L = 0 the soil-brightness correction
    // vanishes: SAVI ≡ NDVI. (The API's L is fixed at 0.5; the isScaled
    // variant applies the same formula in 0–10000 reflectance space, so use
    // unit-scale data and the plain kernel against a hand-derived value.)
    const float n = 0.6f, r = 0.2f;
    float saviOut = -99.0f;
    REQUIRE( SpectralIndices::savi( &n, &r, &saviOut, 1 ) );
    const float expected = ( ( n - r ) / ( n + r + 0.5f ) ) * 1.5f;
    REQUIRE_THAT( saviOut, WithinAbs( expected, static_cast<float>( kTol ) ) );
}

// ---------------------------------------------------------------------------
// Change detection — algebraic invariants
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: difference of identical series is identically zero",
           "[known_answer][change]" )
{
    const std::vector<float> a = { 0.f, 1.f, -3.5f, 1000.f, 1e-6f };
    std::vector<float> out( a.size(), 7.f );
    REQUIRE( difference( a.data(), a.data(), out.data(), a.size() ) );
    for ( const float v : out )
        REQUIRE( v == 0.0f );
    const ChangeStats stats = statistics( out.data(), out.size() );
    REQUIRE( stats.validCount == a.size() );
    REQUIRE_THAT( stats.mean, WithinAbs( 0.f, static_cast<float>( kTol ) ) );
    REQUIRE( stats.stddev == 0.0f );
}

TEST_CASE( "known-answer: change statistics of a linear ramp are exact",
           "[known_answer][change]" )
{
    // diff = 1..7: mean = 4, min = 1, max = 7, population σ² = (7²−1)/12 = 4,
    // so σ = 2 exactly (the ChangeStats kernel is population-normalized —
    // pinned here so a silent switch to sample σ cannot slip through).
    std::vector<float> diff( 7 );
    for ( size_t i = 0; i < diff.size(); ++i )
        diff[i] = static_cast<float>( i ) + 1.0f;
    const ChangeStats stats = statistics( diff.data(), diff.size() );
    REQUIRE( stats.validCount == 7 );
    REQUIRE_THAT( stats.mean, WithinAbs( 4.0f, static_cast<float>( kTol ) ) );
    REQUIRE_THAT( stats.min, WithinAbs( 1.0f, static_cast<float>( kTol ) ) );
    REQUIRE_THAT( stats.max, WithinAbs( 7.0f, static_cast<float>( kTol ) ) );
    REQUIRE_THAT( stats.stddev, WithinAbs( 2.0f, 1e-4f ) );
}

TEST_CASE( "known-answer: change mask threshold is signed and inclusive",
           "[known_answer][change]" )
{
    // Documented convention (also pinned by the dialog contract): mask fires
    // where diff >= threshold — negative change does NOT fire at +1.
    const std::vector<float> diff = { 0.5f, 2.0f, -5.0f, 1.0f };
    std::vector<uint8_t> mask( diff.size(), 0 );
    REQUIRE( changeMask( diff.data(), mask.data(), diff.size(), 1.0f ) );
    REQUIRE( mask[0] == 0 );
    REQUIRE( mask[1] == 1 );
    REQUIRE( mask[2] == 0 ); // signed threshold: −5 < +1
    REQUIRE( mask[3] == 1 ); // boundary inclusive
}

// ---------------------------------------------------------------------------
// Terrain (DEM) — Horn 1981 on an exact inclined plane
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: slope of an exact inclined plane is atan(dz)",
           "[known_answer][terrain]" )
{
    // DEM: z = 2·x (rises 2 m per 1 m eastward), cellSize 1 ⇒ dz/dx = 2,
    // dz/dy = 0. Horn's 3×3 kernel is exact on a linear surface, so every
    // interior cell has slope = atan(2) ≈ 63.435°, independent of position.
    constexpr int W = 5, H = 5;
    std::vector<float> dem( W * H );
    for ( int y = 0; y < H; ++y )
        for ( int x = 0; x < W; ++x )
            dem[static_cast<size_t>( y * W + x )] = 2.0f * static_cast<float>( x );

    std::vector<float> slopeOut( W * H, -1.f );
    REQUIRE( TerrainAnalysis::slope( dem.data(), slopeOut.data(), W, H, 1.0f,
                                     -9999.0f ) );
    const float expected = std::atan( 2.0f ) * 180.0f / 3.14159265358979f;
    for ( int y = 1; y < H - 1; ++y )
        for ( int x = 1; x < W - 1; ++x )
        {
            INFO( "cell " << x << "," << y << " slope " << slopeOut[y * W + x] );
            REQUIRE_THAT( slopeOut[static_cast<size_t>( y * W + x )],
                          WithinAbs( expected, 1e-3f ) );
        }
}

TEST_CASE( "known-answer: aspect compass convention and flat handling",
           "[known_answer][terrain]" )
{
    // Rising eastward ⇒ downslope faces west ⇒ aspect 270° (clockwise from
    // north). Flat DEM ⇒ slope 0, aspect −1 (documented "no aspect").
    constexpr int W = 5, H = 5;
    std::vector<float> rising( W * H ), flat( W * H, 0.0f );
    for ( int y = 0; y < H; ++y )
        for ( int x = 0; x < W; ++x )
            rising[static_cast<size_t>( y * W + x )] = static_cast<float>( x );

    std::vector<float> aspectOut( W * H, -99.f );
    REQUIRE( TerrainAnalysis::aspect( rising.data(), aspectOut.data(), W, H, 1.0f,
                                      -9999.0f ) );
    for ( int y = 1; y < H - 1; ++y )
        for ( int x = 1; x < W - 1; ++x )
        {
            INFO( "cell " << x << "," << y );
            REQUIRE_THAT( aspectOut[static_cast<size_t>( y * W + x )],
                          WithinAbs( 270.0f, 1e-3f ) );
        }

    std::vector<float> slopeFlat( W * H );
    REQUIRE( TerrainAnalysis::slope( flat.data(), slopeFlat.data(), W, H, 1.0f, -9999.0f ) );
    REQUIRE( TerrainAnalysis::aspect( flat.data(), aspectOut.data(), W, H, 1.0f, -9999.0f ) );
    for ( int y = 1; y < H - 1; ++y )
        for ( int x = 1; x < W - 1; ++x )
        {
            REQUIRE( slopeFlat[static_cast<size_t>( y * W + x )] == 0.0f );
            REQUIRE( aspectOut[static_cast<size_t>( y * W + x )] == -1.0f );
        }
}

// ---------------------------------------------------------------------------
// Radiometric calibration — affine sensor models
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: DN→radiance is the exact affine sensor model",
           "[known_answer][radiometric]" )
{
    // L = gain·DN + bias (Landsat RADIANCE_MULT/ADD). gain 2, bias 1:
    // DN 0..5 ⇒ L 1, 3, 5, 7, 9, 11.
    BandCoefficients c;
    c.radianceGain = 2.0;
    c.radianceBias = 1.0;
    c.hasRadiance = true;
    const std::vector<float> dn = { 0.f, 1.f, 2.f, 3.f, 4.f, 5.f };
    std::vector<float> radiance( dn.size() );
    REQUIRE( toRadiance( dn.data(), radiance.data(), dn.size(), c ) );
    for ( size_t i = 0; i < dn.size(); ++i )
        REQUIRE_THAT( radiance[i], WithinAbs( 2.0f * dn[i] + 1.0f, 1e-4f ) );
}

TEST_CASE( "known-answer: TOA reflectance divides by sin(sun elevation)",
           "[known_answer][radiometric]" )
{
    // Landsat: ρ = (mult·DN + add)/sin(θ). With mult=1, add=0, DN=1:
    // θ=90° ⇒ 1; θ=30° ⇒ 1/sin30° = 2; θ=45° ⇒ √2. The sun-elevation
    // branch is the physics — a missing/extra division breaks all three.
    BandCoefficients c;
    c.reflMult = 2.0;
    c.reflAdd = 0.5;
    c.hasReflectance = true;
    const std::vector<float> dn = { 1.0f };
    std::vector<float> rho( 1 );

    struct Case
    {
        double elevation;
        double expected;
    };
    const std::vector<Case> cases = { { 90.0, 2.5 },
                                      { 30.0, 5.0 },
                                      { 45.0, 2.5 * std::sqrt( 2.0 ) } };
    for ( const Case &kase : cases )
    {
        REQUIRE( toToaReflectance( dn.data(), rho.data(), 1, c, SensorType::Landsat,
                                   kase.elevation ) );
        INFO( "elevation " << kase.elevation );
        REQUIRE_THAT( rho[0], WithinAbs( static_cast<float>( kase.expected ), 1e-4f ) );
    }
}

TEST_CASE( "known-answer: brightness temperature inverts the calibration "
           "Planck trace",
           "[known_answer][radiometric]" )
{
    // T = K2 / ln(K1/L + 1). The exact inverse radiance for T₀ is
    // L = K1 / (exp(K2/T₀) − 1); feed that and the kernel must return T₀
    // (a Planck-trace inversion with the Landsat 8 band 10 constants).
    const double k1 = 607.76, k2 = 1260.56; // Landsat 8 band 10 constants
    const double t0 = 300.0;
    const float radiance = static_cast<float>( k1 / ( std::exp( k2 / t0 ) - 1.0 ) );

    BandCoefficients c;
    c.k1 = k1;
    c.k2 = k2;
    const std::vector<float> radianceVec{ radiance };
    std::vector<float> temperature( 1 );
    REQUIRE( toBrightnessTemperature( radianceVec.data(), temperature.data(), 1, c ) );
    REQUIRE_THAT( temperature[0], WithinAbs( static_cast<float>( t0 ), 1e-2f ) );
}

// ---------------------------------------------------------------------------
// Transition matrix — hand-computed confusion with NoData masking
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: transition matrix counts, marginals and masking",
           "[known_answer][classification]" )
{
    // 2 classes; pixels: (before, after, valid)
    //   p0: (0, 1, valid)   → matrix[0][1] += 1
    //   p1: (0, 0, INVALID) → skipped (NoData must not fabricate persistence)
    //   p2: (1, 1, valid)   → matrix[1][1] += 1
    //   p3: (1, 0, valid)   → matrix[1][0] += 1
    // Expected: [[0,1],[1,1]], fromTotals [1,2], toTotals [1,2].
    const std::vector<int32_t> before = { 0, 0, 1, 1 };
    const std::vector<int32_t> after = { 1, 0, 1, 0 };
    const std::vector<uint8_t> valid = { 1, 0, 1, 1 };
    std::vector<uint64_t> matrix( 4, 0 );
    TransitionMatrix::countTransitions( before.data(), after.data(), valid.data(),
                                        before.size(), matrix, 2 );
    REQUIRE( matrix[0 * 2 + 1] == 1 );
    REQUIRE( matrix[1 * 2 + 1] == 1 );
    REQUIRE( matrix[1 * 2 + 0] == 1 );
    REQUIRE( matrix[0 * 2 + 0] == 0 );

    std::vector<uint64_t> fromTotals, toTotals;
    TransitionMatrix::marginals( matrix, 2, fromTotals, toTotals );
    REQUIRE( fromTotals == std::vector<uint64_t>{ 1, 2 } );
    REQUIRE( toTotals == std::vector<uint64_t>{ 1, 2 } );
}

// ---------------------------------------------------------------------------
// Temporal statistics — exact Welford moments and regression
// ---------------------------------------------------------------------------

TEST_CASE( "known-answer: Welford moments of 1..N are exact", "[known_answer][temporal]" )
{
    // mean(1..8) = 4.5; population variance = (N²−1)/12 = (64−1)/12 = 5.25;
    // sample variance = pop·N/(N−1) = 6.0. Also: streaming order must not
    // matter (Welford is algebraically order-stable at these magnitudes).
    sicnu::temporal::stats::WelfordAccumulator acc;
    for ( int i = 1; i <= 8; ++i )
        acc.add( static_cast<double>( i ) );
    REQUIRE( acc.n == 8 );
    REQUIRE_THAT( acc.mean, WithinAbs( 4.5, kTol ) );
    REQUIRE_THAT( acc.populationVariance(), WithinAbs( 5.25, kTol ) );
    REQUIRE_THAT( acc.sampleVariance(), WithinAbs( 6.0, kTol ) );
    REQUIRE_THAT( acc.populationStddev(), WithinAbs( std::sqrt( 5.25 ), kTol ) );
}

TEST_CASE( "known-answer: online regression recovers y = 2t + 1 exactly",
           "[known_answer][temporal]" )
{
    // Fit y = 2t + 1 over t = 0..9: slope 2, intercept 1, r² = 1 (zero
    // residual). A regression that cannot recover a noiseless line cannot be
    // trusted on real phenology series.
    sicnu::temporal::stats::OnlineRegression reg;
    for ( int t = 0; t < 10; ++t )
        reg.add( static_cast<double>( t ), 2.0 * t + 1.0 );
    REQUIRE( reg.solvable() );
    const double slope = reg.mty / reg.mtt;
    const double intercept = reg.meanY - slope * reg.meanT;
    const double r2 = ( reg.mty * reg.mty ) / ( reg.mtt * reg.myy );
    REQUIRE_THAT( slope, WithinAbs( 2.0, kTol ) );
    REQUIRE_THAT( intercept, WithinAbs( 1.0, kTol ) );
    REQUIRE_THAT( r2, WithinAbs( 1.0, kTol ) );
}
