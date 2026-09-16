// tests/test_sar_baseline.cpp — pair-level InSAR input truth (Advanced
// InSAR 11.0, package A).
//
// Independent oracle: the synthetic master is the same analytic equatorial
// circular orbit as test_sar_orbit (P(t) = R·(cos ωt, sin ωt, 0)), where the
// zero-Doppler plane at time t is the meridian of sub-satellite longitude
// ωt. The slave is the master RIGIDLY TRANSLATED by a tangential vector
// Δ = δ·(−sin φ₀, cos φ₀, 0). For a ground point on the equator at
// longitude φ₀ the crossing times and the interferometric baseline then
// have EXACT closed forms derived in this file (analytically, not by
// re-running the implementation's numerics):
//
//   master crossing:  ωt₁ = φ₀                → t₁ = φ₀/ω, r₁ = R − a
//   slave crossing:   a·sin(ωt−φ₀) + δ·cos(ωt−φ₀) = 0
//                     → t₂ = t₁ − arctan(δ/a)/ω
//   Δr = S(t₂) + Δ − S(t₁)   (test evaluates in std::sin/cos directly)
//   B∥ = Δr·ŝ (ŝ radial),  B⊥ = sqrt(|Δr|² − B∥²)
//
// These closed forms share NO code path with the implementation (Hermite
// interpolation + bracketed bisection), so agreement pins the whole pair
// chain, not a copied derivation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/sar/sar_baseline.h"

#include <cmath>

using namespace sicnu::sar;
using Catch::Approx;

namespace
{
constexpr double kOrbitRadius = 7000000.0;     // m
constexpr double kOmega = 2.0 * M_PI / 5880.0; // rad/s
constexpr double kSemiMajor = Wgs84::kSemiMajor;
constexpr double kWavelengthUm = 55500.0;      // ~C-band
constexpr double kMasterUtc = 1e9;             // absolute UTC seconds
constexpr double kPhase0 = kOmega * 30.0;      // master crossing at t₁ = 30 s
constexpr double kDelta = 1000.0;              // tangential slave offset, m

OrbitSegment makeCircularOrbit( double radius = kOrbitRadius, double phaseShift = 0.0 )
{
    OrbitSegment orbit;
    for ( int i = 0; i <= 6; ++i )
    {
        const double t = 10.0 * i;
        const double phase = kOmega * t + phaseShift;
        OrbitStateVector s;
        s.t = t;
        s.x = radius * std::cos( phase );
        s.y = radius * std::sin( phase );
        s.z = 0.0;
        s.vx = -radius * kOmega * std::sin( phase );
        s.vy = radius * kOmega * std::cos( phase );
        s.vz = 0.0;
        orbit.states.push_back( s );
    }
    return orbit;
}

/// Master rigidly translated by the tangential vector Δ at longitude φ₀
/// (positions shifted, velocities identical — a valid orbit of the same
/// shape on a displaced circle).
OrbitSegment makeTangentiallyShiftedOrbit( double delta )
{
    OrbitSegment orbit = makeCircularOrbit();
    for ( OrbitStateVector &s : orbit.states )
    {
        s.x += -delta * std::sin( kPhase0 );
        s.y += delta * std::cos( kPhase0 );
    }
    return orbit;
}

InSarSceneTruth makeScene( const OrbitSegment &orbit, double utc, double wavelengthUm )
{
    InSarSceneTruth scene;
    scene.acquisitionUtcSec = utc;
    scene.wavelengthUm = wavelengthUm;
    scene.orbit = orbit;
    return scene;
}
} // namespace

TEST_CASE( "Scene truth validation refuses unset/invalid fields",
           "[sar][baseline][insar11]" )
{
    const OrbitSegment orbit = makeCircularOrbit();
    QString error;

    InSarSceneTruth scene = makeScene( orbit, kMasterUtc, kWavelengthUm );
    REQUIRE( validateSceneTruth( scene, &error ) );
    REQUIRE( error.isEmpty() );

    // NaN UTC (unset field sentinel, never zero).
    InSarSceneTruth noUtc = scene;
    noUtc.acquisitionUtcSec = kUnset;
    REQUIRE_FALSE( validateSceneTruth( noUtc, &error ) );
    REQUIRE( error.contains( "SCENE_TRUTH_INVALID" ) );

    // Non-positive wavelength.
    InSarSceneTruth badLambda = scene;
    badLambda.wavelengthUm = 0.0;
    REQUIRE_FALSE( validateSceneTruth( badLambda, &error ) );
    badLambda.wavelengthUm = -5.0;
    REQUIRE_FALSE( validateSceneTruth( badLambda, &error ) );

    // Orbit contract violations (delegated refusal, domain-coded).
    InSarSceneTruth badOrbit = scene;
    badOrbit.orbit.states.clear();
    REQUIRE_FALSE( validateSceneTruth( badOrbit, &error ) );
    REQUIRE( error.contains( "ORBIT_SEGMENT_INVALID" ) );

    InSarSceneTruth oneState = scene;
    oneState.orbit.states.resize( 1 );
    REQUIRE_FALSE( validateSceneTruth( oneState, &error ) );
}

TEST_CASE( "Pair truth: temporal baseline, wavelength consistency, absolute "
           "orbit-window check", "[sar][baseline][insar11]" )
{
    const OrbitSegment master = makeCircularOrbit();
    const OrbitSegment slaveOrbit = makeTangentiallyShiftedOrbit( kDelta );
    QString error;

    // Equal wavelengths, slave 12 h after master.
    InSarPairTruth pair;
    REQUIRE( buildPairTruth( makeScene( master, kMasterUtc, kWavelengthUm ),
                             makeScene( slaveOrbit, kMasterUtc + 12.0 * 3600.0,
                                        kWavelengthUm ),
                             &pair, &error ) );
    REQUIRE( pair.temporalDays == Approx( 0.5 ).margin( 1e-12 ) );
    REQUIRE( pair.truthVersion == kInSarTruthVersion );
    // No absolute anchors declared → base sharing stays undeclared, no refusal.
    REQUIRE_FALSE( pair.orbitsShareAbsoluteBase );

    // Wavelength mismatch is a typed refusal, never a silent phase mix.
    REQUIRE_FALSE( buildPairTruth( makeScene( master, kMasterUtc, kWavelengthUm ),
                                   makeScene( slaveOrbit, kMasterUtc, 54000.0 ),
                                   &pair, &error ) );
    REQUIRE( error.contains( "WAVELENGTH_INCOMPATIBLE" ) );

    // Invalid slave truth propagates the scene refusal.
    REQUIRE_FALSE( buildPairTruth( makeScene( master, kMasterUtc, kWavelengthUm ),
                                   makeScene( OrbitSegment{}, kMasterUtc, kWavelengthUm ),
                                   &pair, &error ) );

    // Absolute anchors with disjoint orbit windows: the acquisitions cannot
    // share an imaged area — refuse.
    InSarSceneTruth m = makeScene( master, kMasterUtc, kWavelengthUm );
    m.azimuthStartUtcSec = 0.0; // absolute window [0, 60]
    InSarSceneTruth s = makeScene( slaveOrbit, kMasterUtc, kWavelengthUm );
    s.azimuthStartUtcSec = 100000.0; // absolute window [100000, 100060]
    REQUIRE_FALSE( buildPairTruth( m, s, &pair, &error ) );
    REQUIRE( error.contains( "ORBIT_EPOCH_MISMATCH" ) );

    // Overlapping absolute windows pass and declare the shared base.
    s.azimuthStartUtcSec = 20.0; // [20, 80] overlaps [0, 60]
    REQUIRE( buildPairTruth( m, s, &pair, &error ) );
    REQUIRE( pair.orbitsShareAbsoluteBase );
}

TEST_CASE( "Pair baseline at ground matches the analytic translated-orbit "
           "closed form", "[sar][baseline][insar11]" )
{
    const OrbitSegment master = makeCircularOrbit();
    const OrbitSegment slaveOrbit = makeTangentiallyShiftedOrbit( kDelta );
    InSarPairTruth pair;
    REQUIRE( buildPairTruth( makeScene( master, kMasterUtc, kWavelengthUm ),
                             makeScene( slaveOrbit, kMasterUtc, kWavelengthUm ),
                             &pair ) );

    // Ground point on the equator at sub-master longitude, ellipsoid height.
    GeodeticPoint ground;
    ground.latDeg = 0.0;
    ground.lonDeg = kPhase0 * 180.0 / M_PI;
    ground.heightM = 0.0;

    PairBaselineSample sample;
    QString error;
    REQUIRE( pairBaselineAtGround( pair, ground, &sample, &error ) );

    // --- Independent analytic oracle (see file header) -----------------
    const double t1 = 30.0; // ωt₁ = φ₀ by construction
    const double t2 = t1 - std::atan( kDelta / kSemiMajor ) / kOmega;
    const double s1x = kOrbitRadius * std::cos( kPhase0 );
    const double s1y = kOrbitRadius * std::sin( kPhase0 );
    const double s2x = kOrbitRadius * std::cos( kOmega * t2 ) - kDelta * std::sin( kPhase0 );
    const double s2y = kOrbitRadius * std::sin( kOmega * t2 ) + kDelta * std::cos( kPhase0 );
    const double dxr = s2x - s1x;
    const double dyr = s2y - s1y;
    const double losX = std::cos( kPhase0 );
    const double losY = std::sin( kPhase0 );
    const double bParallel = dxr * losX + dyr * losY;
    const double bPerp = std::sqrt( dxr * dxr + dyr * dyr - bParallel * bParallel );

    REQUIRE( sample.azimuthTimeMaster == Approx( t1 ).margin( 1e-6 ) );
    REQUIRE( sample.azimuthTimeSlave == Approx( t2 ).margin( 1e-6 ) );
    REQUIRE( sample.rangeMasterM == Approx( kOrbitRadius - kSemiMajor ).margin( 1e-3 ) );
    REQUIRE( sample.parallelM == Approx( bParallel ).margin( 1e-2 ) );
    REQUIRE( sample.perpendicularM == Approx( bPerp ).margin( 1e-2 ) );
    REQUIRE( sample.magnitudeM == Approx( std::sqrt( dxr * dxr + dyr * dyr ) ).margin( 1e-2 ) );
    // B∥ is small by construction (tangential offset ⊥ radial LOS) but not
    // exactly zero: the crossing-time shift bends the separation slightly
    // toward the radial — the second-order analytic value the oracle above
    // computes (≈ −0.086 m for these numbers). The sample must agree with
    // the analytic value, not merely "be small".
    REQUIRE( std::abs( bParallel ) < 0.1 );
    REQUIRE( sample.parallelM == Approx( bParallel ).margin( 1e-2 ) );
}

TEST_CASE( "Pair baseline refuses ground points outside both orbit windows",
           "[sar][baseline][insar11]" )
{
    const OrbitSegment master = makeCircularOrbit();
    InSarPairTruth pair;
    REQUIRE( buildPairTruth( makeScene( master, kMasterUtc, kWavelengthUm ),
                             makeScene( makeTangentiallyShiftedOrbit( kDelta ),
                                        kMasterUtc, kWavelengthUm ),
                             &pair ) );

    GeodeticPoint antipode;
    antipode.latDeg = 0.0;
    antipode.lonDeg = ( kPhase0 + M_PI ) * 180.0 / M_PI; // half an orbit away
    antipode.heightM = 0.0;

    PairBaselineSample sample;
    QString error;
    REQUIRE_FALSE( pairBaselineAtGround( pair, antipode, &sample, &error ) );
    REQUIRE( error.contains( "BASELINE_NO_ZERO_DOPPLER_MASTER" ) );
}

TEST_CASE( "Height ambiguity follows the analytic sensitivity formula and "
           "refuses degenerate baselines", "[sar][baseline][insar11]" )
{
    QString error;
    // Plain numbers, exact formula (θ = 30°, r = 800 km, λ = 0.0555 m,
    // B⊥ = 200 m → h_amb = 55.5 m).
    const double hAmb = heightAmbiguityM( 200.0, 800000.0, 30.0, 0.0555, &error );
    REQUIRE( hAmb == Approx( 0.0555 * 800000.0 * 0.5 / 400.0 ).margin( 1e-9 ) );

    // Degenerate B⊥ (no height sensitivity) refuses.
    REQUIRE( std::isnan( heightAmbiguityM( 0.0, 800000.0, 30.0, 0.0555, &error ) ) );
    REQUIRE( error.contains( "SCENE_TRUTH_INVALID" ) );

    // Sign of B⊥ must not matter (magnitude contract of sar_orbit.h).
    REQUIRE( heightAmbiguityM( -200.0, 800000.0, 30.0, 0.0555 )
             == Approx( hAmb ).margin( 1e-12 ) );

    // µm→m contract converts once, here.
    REQUIRE( wavelengthUmToM( kWavelengthUm ) == Approx( 0.0555 ).margin( 1e-12 ) );
}
