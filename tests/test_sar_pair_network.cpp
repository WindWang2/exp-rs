// tests/test_sar_pair_network.cpp — multi-temporal pair network
// (Advanced InSAR 11.0, package E).
//
// Known answers on the equatorial circular-orbit family: the tangentially
// shifted orbit gives a screening B⊥ ≈ |1 − R/a|·δ (analytic, see
// test_sar_baseline.cpp), so constraint filters have exact, predictable
// effects. Fail-closed semantics (Oracle 2) are exercised per branch.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/sar/sar_pair_network.h"

#include <cmath>

using namespace sicnu::sar;
using Catch::Approx;

namespace
{
constexpr double kOrbitRadius = 7000000.0;
constexpr double kOmega = 2.0 * M_PI / 5880.0;
constexpr double kWavelengthUm = 55500.0;

OrbitSegment makeCircularOrbit( double tangentialShift = 0.0 )
{
    const double phase0 = kOmega * 30.0;
    OrbitSegment orbit;
    for ( int i = 0; i <= 6; ++i )
    {
        const double t = 10.0 * i;
        const double phase = kOmega * t;
        OrbitStateVector s;
        s.t = t;
        s.x = kOrbitRadius * std::cos( phase ) - tangentialShift * std::sin( phase0 );
        s.y = kOrbitRadius * std::sin( phase ) + tangentialShift * std::cos( phase0 );
        s.z = 0.0;
        s.vx = -kOrbitRadius * kOmega * std::sin( phase );
        s.vy = kOrbitRadius * kOmega * std::cos( phase );
        s.vz = 0.0;
        orbit.states.push_back( s );
    }
    return orbit;
}

InSarSceneTruth makeScene( double utcDays, double shift, double wavelengthUm )
{
    InSarSceneTruth scene;
    scene.acquisitionUtcSec = 1700000000.0 + utcDays * 86400.0;
    scene.wavelengthUm = wavelengthUm;
    scene.orbit = makeCircularOrbit( shift );
    return scene;
}
} // namespace

TEST_CASE( "All-pairs network of three scenes is connected with exact "
           "temporal baselines", "[sar][pair-network][insar11]" )
{
    const std::vector<InSarSceneTruth> scenes = {
        makeScene( 0.0, 0.0, kWavelengthUm ),
        makeScene( 12.0, 500.0, kWavelengthUm ),
        makeScene( 24.0, 1000.0, kWavelengthUm ),
    };
    PairNetworkParams params; // AllPairs, unconstrained, reference 0
    PairNetworkResult result;
    QString error;
    REQUIRE( buildPairNetwork( scenes, params, &result, &error ) );
    REQUIRE( result.pairs.size() == 3 );
    REQUIRE( result.connected );
    REQUIRE( result.componentCount == 1 );
    REQUIRE( result.referenceIdx == 0 );
    for ( const auto &scene : result.componentOfScene )
        REQUIRE( scene == 0 );

    // Temporal baselines are exact signed day differences.
    REQUIRE( result.pairs[0].masterIdx == 0 );
    REQUIRE( result.pairs[0].slaveIdx == 1 );
    REQUIRE( result.pairs[0].temporalDays == Approx( 12.0 ).margin( 1e-9 ) );
    REQUIRE( result.pairs[1].temporalDays == Approx( 24.0 ).margin( 1e-9 ) );
    REQUIRE( result.pairs[2].temporalDays == Approx( 12.0 ).margin( 1e-9 ) );
    // Screening B⊥ grows with the shift (all three pairs share master 0's
    // mid-time; ordering by construction: (0,1) then (0,2) then (1,2)).
    REQUIRE( result.pairs[1].perpendicularM > result.pairs[0].perpendicularM );
}

TEST_CASE( "Perpendicular and temporal constraints filter pairs visibly",
           "[sar][pair-network][insar11]" )
{
    const std::vector<InSarSceneTruth> scenes = {
        makeScene( 0.0, 0.0, kWavelengthUm ),
        makeScene( 12.0, 500.0, kWavelengthUm ),
        makeScene( 24.0, 2000.0, kWavelengthUm ),
    };
    PairNetworkParams params;
    // The screening B⊥ evaluates SIMULTANEOUS sensor positions at the
    // master's mid-time, so it equals the tangential shift itself:
    // pairs carry 500 / 2000 / 1500 m. The bound keeps the two smaller
    // pairs and drops exactly the 2000 m one — visible, exact filtering.
    params.maxPerpendicularM = 1600.0;
    PairNetworkResult result;
    QString error;
    REQUIRE( buildPairNetwork( scenes, params, &result, &error ) );
    REQUIRE( result.pairs.size() == 2 );
    REQUIRE( result.maxPerpendicularSeenM <= 1600.0 );

    PairNetworkParams temporal;
    temporal.maxTemporalDays = 13.0;
    REQUIRE( buildPairNetwork( scenes, temporal, &result, &error ) );
    REQUIRE( result.pairs.size() == 2 );
    REQUIRE( result.maxTemporalSeenDays <= 13.0 );

    // Consecutive: each scene only to its successor.
    PairNetworkParams consecutive;
    consecutive.strategy = PairStrategy::Consecutive;
    REQUIRE( buildPairNetwork( scenes, consecutive, &result, &error ) );
    REQUIRE( result.pairs.size() == 2 );
    REQUIRE( result.pairs[0].slaveIdx == 1 );
    REQUIRE( result.pairs[1].masterIdx == 1 );
}

TEST_CASE( "Disconnected graphs refuse unless explicitly allowed",
           "[sar][pair-network][insar11]" )
{
    // Scenes 0–1 at day 0/12, scene 2 at day 400 with a tight temporal
    // bound → two components.
    const std::vector<InSarSceneTruth> scenes = {
        makeScene( 0.0, 0.0, kWavelengthUm ),
        makeScene( 12.0, 500.0, kWavelengthUm ),
        makeScene( 400.0, 0.0, kWavelengthUm ),
    };
    PairNetworkParams params;
    params.maxTemporalDays = 30.0;
    PairNetworkResult result;
    QString error;
    REQUIRE_FALSE( buildPairNetwork( scenes, params, &result, &error ) );
    REQUIRE( error.contains( "PAIR_GRAPH_DISCONNECTED" ) );

    params.allowDisconnected = true;
    REQUIRE( buildPairNetwork( scenes, params, &result, &error ) );
    REQUIRE_FALSE( result.connected );
    REQUIRE( result.componentCount == 2 );
    REQUIRE( result.componentOfScene[0] == result.componentOfScene[1] );
    REQUIRE( result.componentOfScene[2] != result.componentOfScene[0] );
}

TEST_CASE( "Metadata failures are typed refusals, never silent filtering",
           "[sar][pair-network][insar11]" )
{
    QString error;
    PairNetworkResult result;
    const auto scenes = std::vector<InSarSceneTruth>{
        makeScene( 0.0, 0.0, kWavelengthUm ),
        makeScene( 12.0, 500.0, kWavelengthUm ),
    };

    // Wavelength disagreement anywhere in the stack.
    auto mixedLambda = scenes;
    mixedLambda[1].wavelengthUm = 54000.0;
    REQUIRE_FALSE( buildPairNetwork( mixedLambda, PairNetworkParams{}, &result, &error ) );
    REQUIRE( error.contains( "WAVELENGTH_INCOMPATIBLE" ) );

    // Invalid scene truth (empty orbit).
    auto badOrbit = scenes;
    badOrbit[1].orbit.states.clear();
    REQUIRE_FALSE( buildPairNetwork( badOrbit, PairNetworkParams{}, &result, &error ) );
    REQUIRE( error.contains( "ORBIT_SEGMENT_INVALID" ) );

    // Disjoint absolute orbit windows.
    auto disjoint = scenes;
    disjoint[0].azimuthStartUtcSec = 0.0;
    disjoint[1].azimuthStartUtcSec = 1000000.0;
    REQUIRE_FALSE( buildPairNetwork( disjoint, PairNetworkParams{}, &result, &error ) );
    REQUIRE( error.contains( "ORBIT_EPOCH_MISMATCH" ) );

    // Out-of-range reference.
    REQUIRE_FALSE( buildPairNetwork( scenes, PairNetworkParams{ .referenceIdx = 5 },
                                     &result, &error ) );
    REQUIRE( error.contains( "SCENE_TRUTH_INVALID" ) );

    // Degenerate stacks.
    REQUIRE_FALSE( buildPairNetwork( { scenes[0] }, PairNetworkParams{}, &result, &error ) );
    REQUIRE( error.contains( "SCENE_TRUTH_INVALID" ) );
}
