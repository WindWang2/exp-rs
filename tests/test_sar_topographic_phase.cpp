// tests/test_sar_topographic_phase.cpp — DEM/orbit topographic phase
// (Advanced InSAR 11.0, package B).
//
// TWO-TIER INDEPENDENT ORACLE (no code shared with the implementation):
//
// Tier 1 — exact closed forms on the equator for the tangentially-shifted
// circular-orbit pair (see test_sar_baseline.cpp for the geometry): the
// crossing times t₁ = φ₀/ω and t₂ = t₁ − arctan(δ/a)/ω, the ranges, and
// hence φ_topo = wrap(−4π(r_m − r_s)/λ) are all analytic.
//
// Tier 2 — a test-local zero-Doppler solver for arbitrary latitudes: the
// analytic state functions S(t), V(t) of the circular orbit are re-derived
// here and the crossing of dot(S(t)−P, V(t)) = 0 is found by an
// independent scan + bisection. This pins the sign conventions, the
// master/slave asymmetry, and the height sensitivity off-nadir, where no
// closed form is used.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/sar/sar_baseline.h"
#include "processing/algorithms/sar/sar_topographic_phase.h"

#include <cmath>
#include <vector>

using namespace sicnu::sar;
using Catch::Approx;

namespace
{
constexpr double kOrbitRadius = 7000000.0;
constexpr double kOmega = 2.0 * M_PI / 5880.0;
constexpr double kSemiMajor = Wgs84::kSemiMajor;
constexpr double kWavelengthM = 0.0555;
constexpr double kPhase0 = kOmega * 30.0;
constexpr double kDelta = 1000.0;

OrbitSegment makeCircularOrbit()
{
    OrbitSegment orbit;
    for ( int i = 0; i <= 6; ++i )
    {
        const double t = 10.0 * i;
        const double phase = kOmega * t;
        OrbitStateVector s;
        s.t = t;
        s.x = kOrbitRadius * std::cos( phase );
        s.y = kOrbitRadius * std::sin( phase );
        s.z = 0.0;
        s.vx = -kOrbitRadius * kOmega * std::sin( phase );
        s.vy = kOrbitRadius * kOmega * std::cos( phase );
        s.vz = 0.0;
        orbit.states.push_back( s );
    }
    return orbit;
}

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

// --- Tier-2 oracle: independent analytic circular-orbit zero Doppler ----

struct EcefPoint
{
    double x, y, z;
};

EcefPoint oraclePos( double t )
{
    return { kOrbitRadius * std::cos( kOmega * t ), kOrbitRadius * std::sin( kOmega * t ), 0.0 };
}
EcefPoint oracleVel( double t )
{
    return { -kOrbitRadius * kOmega * std::sin( kOmega * t ),
             kOrbitRadius * kOmega * std::cos( kOmega * t ), 0.0 };
}

double oracleDoppler( const EcefPoint &p, const EcefPoint &delta, double t )
{
    const EcefPoint s = oraclePos( t );
    const EcefPoint v = oracleVel( t );
    return ( s.x + delta.x - p.x ) * v.x + ( s.y + delta.y - p.y ) * v.y
           + ( s.z + delta.z - p.z ) * v.z;
}

/// Independent zero-Doppler range: scan a ±quarter period around @a tHint
/// for the sign change of the Doppler derivative, then bisect.
bool oracleRange( const EcefPoint &p, const EcefPoint &delta, double tHint,
                  double *rangeM )
{
    const double halfPeriod = M_PI / kOmega;
    const double lo = tHint - 0.25 * halfPeriod;
    const double hi = tHint + 0.25 * halfPeriod;
    const int scanSteps = 64;
    double fPrev = oracleDoppler( p, delta, lo );
    double a = lo, b = hi;
    bool bracketed = false;
    for ( int i = 1; i <= scanSteps; ++i )
    {
        const double t = lo + ( hi - lo ) * i / scanSteps;
        const double f = oracleDoppler( p, delta, t );
        if ( ( fPrev <= 0.0 && f > 0.0 ) || ( fPrev >= 0.0 && f < 0.0 ) )
        {
            a = lo + ( hi - lo ) * ( i - 1 ) / scanSteps;
            b = t;
            bracketed = true;
            break;
        }
        fPrev = f;
    }
    if ( !bracketed )
        return false;
    for ( int i = 0; i < 80; ++i )
    {
        const double m = 0.5 * ( a + b );
        if ( ( oracleDoppler( p, delta, a ) <= 0.0 )
             == ( oracleDoppler( p, delta, m ) <= 0.0 ) )
            a = m;
        else
            b = m;
    }
    const double t = 0.5 * ( a + b );
    EcefPoint s = oraclePos( t );
    s.x += delta.x;
    s.y += delta.y;
    *rangeM = std::sqrt( ( s.x - p.x ) * ( s.x - p.x ) + ( s.y - p.y ) * ( s.y - p.y )
                         + ( s.z - p.z ) * ( s.z - p.z ) );
    return true;
}

double wrapDiff( double a, double b )
{
    // Wrap-aware comparison distance in (−π, π].
    double d = std::fmod( a - b + M_PI, 2.0 * M_PI );
    if ( d < 0.0 )
        d += 2.0 * M_PI;
    return d - M_PI;
}
} // namespace

TEST_CASE( "wrapPhaseRad maps into the wrapped domain and passes NaN through",
           "[sar][topo][insar11]" )
{
    REQUIRE( wrapPhaseRad( 0.0 ) == Approx( 0.0 ).margin( 1e-15 ) );
    REQUIRE( wrapPhaseRad( 2.0 * M_PI ) == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( wrapPhaseRad( 0.75 * M_PI ) == Approx( 0.75 * M_PI ).margin( 1e-12 ) );
    REQUIRE( wrapPhaseRad( 1.5 * M_PI ) == Approx( -0.5 * M_PI ).margin( 1e-12 ) );
    REQUIRE( wrapPhaseRad( -1.5 * M_PI ) == Approx( 0.5 * M_PI ).margin( 1e-12 ) );
    REQUIRE( std::isnan( wrapPhaseRad( std::numeric_limits<double>::quiet_NaN() ) ) );
}

TEST_CASE( "Topographic phase on the equator matches the analytic closed form",
           "[sar][topo][insar11]" )
{
    const OrbitSegment master = makeCircularOrbit();
    const OrbitSegment slave = makeTangentiallyShiftedOrbit( kDelta );
    QString error;

    // Ground point on the equator at sub-master longitude, several heights.
    GeodeticPoint ground;
    ground.latDeg = 0.0;
    ground.lonDeg = kPhase0 * 180.0 / M_PI;
    for ( const double height : { 0.0, 120.0, 800.0 } )
    {
        ground.heightM = height;
        double phase = 0.0;
        REQUIRE( topographicPhaseAtGround( master, slave, kWavelengthM, ground,
                                           &phase, &error ) );

        // Analytic oracle: r_m = R − (a + h); slave crossing shifted by
        // arctan(δ/(a+h))/ω with r_s from the closed-form crossing.
        const double rMaster = kOrbitRadius - ( kSemiMajor + height );
        const double t2 = 30.0 - std::atan( kDelta / ( kSemiMajor + height ) ) / kOmega;
        const double s2x = kOrbitRadius * std::cos( kOmega * t2 ) - kDelta * std::sin( kPhase0 );
        const double s2y = kOrbitRadius * std::sin( kOmega * t2 ) + kDelta * std::cos( kPhase0 );
        const double px = ( kSemiMajor + height ) * std::cos( kPhase0 );
        const double py = ( kSemiMajor + height ) * std::sin( kPhase0 );
        const double rSlave = std::sqrt( ( s2x - px ) * ( s2x - px )
                                         + ( s2y - py ) * ( s2y - py ) );
        const double expected = wrapPhaseRad( -4.0 * M_PI * ( rMaster - rSlave )
                                              / kWavelengthM );
        // Zero-Doppler range accuracy ~1e-6 m → ~2e-4 rad here; stay above.
        REQUIRE( wrapDiff( phase, expected ) == Approx( 0.0 ).margin( 1e-2 ) );
    }
}

TEST_CASE( "Topographic phase off-nadir matches the independent scan+bisect "
           "oracle", "[sar][topo][insar11]" )
{
    const OrbitSegment master = makeCircularOrbit();
    const OrbitSegment slave = makeTangentiallyShiftedOrbit( kDelta );
    QString error;

    // 20°N: the LOS is no longer radial — this is where height sensitivity
    // lives (at nadir the B⊥ sensitivity degenerates).
    GeodeticPoint ground;
    ground.latDeg = 20.0;
    ground.lonDeg = kPhase0 * 180.0 / M_PI;

    double prevPhase = 0.0;
    (void)prevPhase; // superseded: sensitivity is pinned by the oracle equality
    for ( const double height : { 0.0, 100.0, 500.0 } )
    {
        ground.heightM = height;
        double phase = 0.0;
        REQUIRE( topographicPhaseAtGround( master, slave, kWavelengthM, ground,
                                           &phase, &error ) );

        double px = 0.0, py = 0.0, pz = 0.0;
        Wgs84::geodeticToEcef( ground.latDeg, ground.lonDeg, height, &px, &py, &pz );
        const EcefPoint p{ px, py, pz };
        double rMaster = 0.0, rSlave = 0.0;
        const EcefPoint noDelta{ 0.0, 0.0, 0.0 };
        const EcefPoint slaveDelta{ -kDelta * std::sin( kPhase0 ),
                                    kDelta * std::cos( kPhase0 ), 0.0 };
        REQUIRE( oracleRange( p, noDelta, 30.0, &rMaster ) );
        REQUIRE( oracleRange( p, slaveDelta, 30.0, &rSlave ) );
        const double expected = wrapPhaseRad( -4.0 * M_PI * ( rMaster - rSlave )
                                              / kWavelengthM );
        REQUIRE( wrapDiff( phase, expected ) == Approx( 0.0 ).margin( 1e-2 ) );

        // Height sensitivity needs NO separate assertion: the per-height
        // oracle equality above (h = 0, 100, 500 m) cannot hold unless the
        // kernel phase tracks the DEM height. A wrapped-difference
        // threshold here would be wrong by construction: wrapped
        // differences live modulo 2π, so a large unwrapped sensitivity can
        // legitimately wrap to ~0.
    }
}

TEST_CASE( "Swapping master and slave negates the topographic phase",
           "[sar][topo][insar11]" )
{
    const OrbitSegment master = makeCircularOrbit();
    const OrbitSegment slave = makeTangentiallyShiftedOrbit( kDelta );
    GeodeticPoint ground;
    ground.latDeg = 20.0;
    ground.lonDeg = kPhase0 * 180.0 / M_PI;
    ground.heightM = 250.0;

    QString error;
    double forward = 0.0, reverse = 0.0;
    REQUIRE( topographicPhaseAtGround( master, slave, kWavelengthM, ground,
                                       &forward, &error ) );
    REQUIRE( topographicPhaseAtGround( slave, master, kWavelengthM, ground,
                                       &reverse, &error ) );
    REQUIRE( wrapDiff( forward, -reverse ) == Approx( 0.0 ).margin( 1e-9 ) );
}

TEST_CASE( "removeTopographicPhase subtracts wrapped and propagates NaN",
           "[sar][topo][insar11]" )
{
    // ifg = topo + displacement → residual = displacement (mod 2π).
    const double topo[] = { 3.1, -3.1, 0.0, 1.0 };
    const double displacement[] = { 0.2, -0.2, 0.7, 0.0 };
    double ifg[4];
    for ( int i = 0; i < 4; ++i )
        ifg[i] = wrapPhaseRad( topo[i] + displacement[i] );

    // One invalid DEM sample must poison exactly its output element.
    double topoWithNan[4] = { topo[0], topo[1], topo[2], topo[3] };
    topoWithNan[2] = std::numeric_limits<double>::quiet_NaN();

    double out[4] = { 0.0, 0.0, 0.0, 0.0 };
    long long nanCount = -1;
    removeTopographicPhase( ifg, topoWithNan, 4, out, &nanCount );

    REQUIRE( wrapDiff( out[0], displacement[0] ) == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( wrapDiff( out[1], displacement[1] ) == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( std::isnan( out[2] ) );
    REQUIRE( wrapDiff( out[3], displacement[3] ) == Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( nanCount == 1 );
}

TEST_CASE( "Topographic phase refuses missing/invalid geometry with domain "
           "codes", "[sar][topo][insar11]" )
{
    const OrbitSegment master = makeCircularOrbit();
    const OrbitSegment slave = makeTangentiallyShiftedOrbit( kDelta );
    QString error;

    double phase = 0.0;

    // Missing wavelength.
    GeodeticPoint ground;
    ground.latDeg = 0.0;
    ground.lonDeg = kPhase0 * 180.0 / M_PI;
    ground.heightM = 0.0;
    REQUIRE_FALSE( topographicPhaseAtGround( master, slave, kUnset, ground,
                                             &phase, &error ) );
    REQUIRE( error.contains( "TOPO_PHASE_METADATA_MISSING" ) );

    // Invalid orbit.
    REQUIRE_FALSE( topographicPhaseAtGround( OrbitSegment{}, slave, kWavelengthM,
                                             ground, &phase, &error ) );
    REQUIRE( error.contains( "ORBIT_SEGMENT_INVALID" ) );

    // Ground point outside both windows (antipode).
    ground.lonDeg = ( kPhase0 + M_PI ) * 180.0 / M_PI;
    REQUIRE_FALSE( topographicPhaseAtGround( master, slave, kWavelengthM, ground,
                                             &phase, &error ) );
    REQUIRE( error.contains( "BASELINE_NO_ZERO_DOPPLER_MASTER" ) );

    // Non-finite DEM height: refuse, never guess.
    ground.lonDeg = kPhase0 * 180.0 / M_PI;
    ground.heightM = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE( topographicPhaseAtGround( master, slave, kWavelengthM, ground,
                                             &phase, &error ) );
    REQUIRE( error.contains( "TOPO_PHASE_METADATA_MISSING" ) );
}
