// tests/test_sar_network_inversion.cpp — small-baseline linear network
// inversion (Advanced InSAR 11.0, package F).
//
// Independent oracle: displacement samples are CONSTRUCTED from a known
// epoch-displacement table through d = u_{m} − u_{s}, so the solve has an
// exact closed-form target (consistent overdetermined system → the unique
// exact solution; weighted variants checked against deliberately
// perturbed rows). Missing-data, per-pixel connectivity, pattern bounds
// and the weight contract are all exercised against hand-derived
// expectations.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "processing/algorithms/sar/sar_network_inversion.h"

#include <cmath>
#include <vector>

using namespace sicnu::sar;
using Catch::Approx;

namespace
{
constexpr double kMm = 1e-3;

/// 4 epochs, 4 pairs: (0,1) (0,2) (1,3) (2,3) — connected, overdetermined.
NetworkInversionProblem makeProblem()
{
    NetworkInversionProblem problem;
    problem.epochCount = 4;
    problem.pairCount = 4;
    problem.pairMasterEpoch = { 1, 2, 3, 3 };
    problem.pairSlaveEpoch = { 0, 0, 1, 2 };
    problem.pairTemporalYears = { 1.0, 2.0, 2.0, 1.0 };
    return problem;
}
} // namespace

TEST_CASE( "A consistent network recovers the exact epoch displacements "
           "and velocity", "[sar][network-inversion][insar11]" )
{
    const NetworkInversionProblem problem = makeProblem();
    REQUIRE( problem.isValid() );
    NetworkInversionSolver solver( problem );

    // Truth: u = [0, 2, 4, 6] mm (linear 2 mm/epoch).
    const double d[] = { 2.0 * kMm, 4.0 * kMm, 4.0 * kMm, 2.0 * kMm };
    const double temporalYears[] = { 0.0, 1.0, 2.0, 3.0 };

    std::vector<double> u( 4, 0.0 );
    double velocity = 0.0;
    double rms = 0.0;
    int solvedEpochs = -1;
    int droppedPairs = -1;
    QString error;
    REQUIRE( solver.solvePixel( d, temporalYears, u.data(), &velocity, &rms,
                                &solvedEpochs, &droppedPairs, &error ) );
    REQUIRE( u[0] == Approx( 0.0 ).margin( 1e-15 ) );
    REQUIRE( u[1] == Approx( 2.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( u[2] == Approx( 4.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( u[3] == Approx( 6.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( velocity == Approx( 2.0 * kMm ).margin( 1e-12 ) ); // m/year
    REQUIRE( rms < 1e-15 );                                     // consistent system
    REQUIRE( solvedEpochs == 3 );
    REQUIRE( droppedPairs == 0 );
}

TEST_CASE( "Missing rows degrade the pixel honestly and keep the rest exact",
           "[sar][network-inversion][insar11]" )
{
    const NetworkInversionProblem problem = makeProblem();
    NetworkInversionSolver solver( problem );
    const double temporalYears[] = { 0.0, 1.0, 2.0, 3.0 };

    // Pair 3 missing (NaN): the remaining 3 pairs still determine u exactly.
    double d[] = { 2.0 * kMm, 4.0 * kMm, 4.0 * kMm, std::numeric_limits<double>::quiet_NaN() };
    std::vector<double> u( 4, 0.0 );
    int solvedEpochs = -1;
    int droppedPairs = -1;
    QString error;
    REQUIRE( solver.solvePixel( d, temporalYears, u.data(), nullptr, nullptr,
                                &solvedEpochs, &droppedPairs, &error ) );
    REQUIRE( u[1] == Approx( 2.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( u[2] == Approx( 4.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( u[3] == Approx( 6.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( droppedPairs == 1 );

    // Drop BOTH pairs touching epoch 3 → epoch 3 leaves the reference
    // component: NaN displacement, reduced velocity base.
    double dIsolated[] = { 2.0 * kMm, 4.0 * kMm,
                           std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::quiet_NaN() };
    double velocity = 0.0;
    REQUIRE( solver.solvePixel( dIsolated, temporalYears, u.data(), &velocity, nullptr,
                                &solvedEpochs, &droppedPairs, &error ) );
    REQUIRE( std::isnan( u[3] ) );
    REQUIRE( u[1] == Approx( 2.0 * kMm ).margin( 1e-12 ) );
    REQUIRE( solvedEpochs == 2 );
    REQUIRE( droppedPairs == 2 );
    // Velocity over epochs {1, 2} with the temporal base → 2 mm/year.
    REQUIRE( velocity == Approx( 2.0 * kMm ).margin( 1e-12 ) );
}

TEST_CASE( "Pair-level weights steer the solution; scalar-quality weights "
           "cannot", "[sar][network-inversion][insar11]" )
{
    // Perturb pair 0 (+1 mm). Unweighted: the inconsistency spreads. With
    // pair 0's weight crushed, the solution ignores it → exact truth from
    // the remaining consistent pairs.
    NetworkInversionProblem problem = makeProblem();
    NetworkInversionSolver unweighted( problem );
    const double temporalYears[] = { 0.0, 1.0, 2.0, 3.0 };
    double d[] = { 3.0 * kMm, 4.0 * kMm, 4.0 * kMm, 2.0 * kMm };

    std::vector<double> u( 4, 0.0 );
    QString error;
    REQUIRE( unweighted.solvePixel( d, temporalYears, u.data(), nullptr, nullptr, nullptr,
                                    nullptr, &error ) );
    const double unweightedU3 = u[3];

    problem.pairWeights = { 1e-6, 1.0, 1.0, 1.0 };
    NetworkInversionSolver weighted( problem );
    REQUIRE( weighted.solvePixel( d, temporalYears, u.data(), nullptr, nullptr, nullptr,
                                  nullptr, &error ) );
    REQUIRE( u[3] == Approx( 6.0 * kMm ).margin( 1e-9 ) );
    REQUIRE( std::abs( u[3] - 6.0 * kMm ) < std::abs( unweightedU3 - 6.0 * kMm ) );

    // Non-positive / non-finite weights are a problem-contract refusal.
    NetworkInversionProblem badWeights = makeProblem();
    badWeights.pairWeights = { 1.0, 0.0, 1.0, 1.0 };
    REQUIRE_FALSE( badWeights.isValid() );
}

TEST_CASE( "Pattern cache bound and invalid problems are typed refusals",
           "[sar][network-inversion][insar11]" )
{
    const NetworkInversionProblem problem = makeProblem();
    const double temporalYears[] = { 0.0, 1.0, 2.0, 3.0 };
    QString error;

    // Three distinct NaN patterns vs a 2-pattern cache → blowup refusal.
    NetworkInversionSolver tight( problem, 2 );
    std::vector<double> u( 4, 0.0 );
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double dAll[] = { 2.0 * kMm, 4.0 * kMm, 4.0 * kMm, 2.0 * kMm };
    double dMissing1[] = { nan, 4.0 * kMm, 4.0 * kMm, 2.0 * kMm };
    double dMissing2[] = { 2.0 * kMm, nan, 4.0 * kMm, 2.0 * kMm };
    REQUIRE( tight.solvePixel( dAll, temporalYears, u.data(), nullptr, nullptr, nullptr,
                               nullptr, &error ) );
    REQUIRE( tight.solvePixel( dMissing1, temporalYears, u.data(), nullptr, nullptr,
                               nullptr, nullptr, &error ) );
    REQUIRE_FALSE( tight.solvePixel( dMissing2, temporalYears, u.data(), nullptr, nullptr,
                                     nullptr, nullptr, &error ) );
    REQUIRE( error.contains( "NETWORK_INVERSION_PATTERN_BLOWUP" ) );
    REQUIRE( tight.distinctPatterns() == 2 );

    // Contract violations.
    NetworkInversionProblem bad = makeProblem();
    bad.epochCount = 1;
    REQUIRE_FALSE( bad.isValid() );
    bad = makeProblem();
    bad.pairMasterEpoch = { 0, 2, 3, 3 }; // master 0 (== reference slave role) invalid
    REQUIRE_FALSE( bad.isValid() );
    bad = makeProblem();
    bad.pairCount = 65; // u64 pattern-mask bound
    bad.pairMasterEpoch.assign( 65, 1 );
    bad.pairSlaveEpoch.assign( 65, 0 );
    bad.pairTemporalYears.assign( 65, 1.0 );
    REQUIRE_FALSE( bad.isValid() );

    // Epoch outside the reference component from the start (no pairs touch
    // epoch 3... already covered above) — null buffers refuse.
    NetworkInversionSolver solver( problem );
    REQUIRE_FALSE( solver.solvePixel( nullptr, temporalYears, u.data(), nullptr, nullptr,
                                      nullptr, nullptr, &error ) );
}
