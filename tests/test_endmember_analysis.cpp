// test_endmember_analysis.cpp — endmember set analysis kernels: redundancy
// reduction against hand-built clusters, angle matrix symmetry/values from an
// independent computation, and sensor projection known answers on the master
// resampling seam (constant-spectrum invariance, linear interpolation, coverage).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "processing/algorithms/endmember_analysis.h"

using namespace EndmemberAnalysis;
using Catch::Approx;

namespace
{
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
}

TEST_CASE( "Angle matrix is symmetric with zero diagonal and correct values", "[endmember][kernel]" )
{
    const std::vector<float> endmembers = {
        1.0f, 0.0f, // atom 0
        0.0f, 1.0f, // atom 1: orthogonal to 0
        2.0f, 0.0f, // atom 2: parallel to 0
    };
    std::vector<double> matrix;
    QString err;
    REQUIRE( angleMatrix( endmembers.data(), 3, 2, &matrix, &err ) );

    CHECK( matrix[0] == 0.0 );
    CHECK( matrix[4] == 0.0 );
    CHECK( matrix[8] == 0.0 );
    // Independent computation: angle between [1,0] and [0,1] is pi/2.
    CHECK( matrix[1] == Approx( kDeg * 90.0 ).margin( 1e-12 ) );
    CHECK( matrix[3] == matrix[1] );
    // Angle between [1,0] and [2,0] is exactly 0; [0,1] vs [2,0] pi/2.
    CHECK( matrix[2] == Approx( 0.0 ).margin( 1e-12 ) );
    CHECK( matrix[5] == Approx( kDeg * 90.0 ).margin( 1e-12 ) );
    CHECK( matrix[7] == matrix[5] );

    // A zero-norm endmember refuses (angles undefined).
    const std::vector<float> withZero = { 1.0f, 0.0f, 0.0f, 0.0f };
    CHECK_FALSE( angleMatrix( withZero.data(), 2, 2, &matrix, &err ) );
    CHECK( err.contains( "zero norm" ) );
}

TEST_CASE( "Reduction merges near-duplicate atoms and keeps PPI leaders", "[endmember][kernel]" )
{
    // Group A: atoms within ~0.5 deg of [1, 0.02]; Group B: ~1 deg around a
    // rotated direction; groups are >20 deg apart.
    const double a = 0.5 * kDeg;
    const double b = 1.0 * kDeg;
    const double baseB = 30.0 * kDeg;
    const auto dir = []( double angle ) -> std::pair<float, float>
    {
        return { static_cast<float>( std::cos( angle ) ),
                 static_cast<float>( std::sin( angle ) ) };
    };
    const auto [ax, ay] = dir( 0.0 );
    const auto [ax2, ay2] = dir( a );
    const auto [ax3, ay3] = dir( -a );
    const auto [bx, by] = dir( baseB );
    const auto [bx2, by2] = dir( baseB + b );
    std::vector<float> endmembers = {
        ax, ay,
        ax2, ay2,
        ax3, ay3,
        bx, by,
        bx2, by2,
    };
    // PPI counts: within group A, member 1 has the highest count; group B -> 4.
    const std::vector<int> ppiCounts = { 5, 9, 2, 3, 7 };

    ReduceConfig config;
    config.mergeAngleDegrees = 2.0; // merges within groups, never across
    ReduceResult result;
    QString err;
    REQUIRE( reduceEndmembers( endmembers.data(), 5, 2, config, &ppiCounts,
                               &result, &err ) );
    REQUIRE( result.representativeOf.size() == 2 );
    CHECK( result.clusterSizes == std::vector<int>{ 3, 2 } );
    // Representatives are the PPI leaders of their clusters.
    CHECK( result.representativeOf[0] == 1 );
    CHECK( result.representativeOf[1] == 4 );
    // Cluster membership maps every input.
    std::vector<int> seenClusterCount( 2, 0 );
    for ( int e = 0; e < 5; ++e )
    {
        REQUIRE( result.clusterOf[e] >= 0 );
        ++seenClusterCount[result.clusterOf[e]];
    }
    CHECK( seenClusterCount == std::vector<int>{ 3, 2 } );
    // Representative spectra are copies of the winning source rows.
    CHECK( result.endmembers[0] == Approx( endmembers[1 * 2] ).margin( 1e-6 ) );
    CHECK( result.endmembers[1] == Approx( endmembers[1 * 2 + 1] ).margin( 1e-6 ) );
    // Three merges happened (5 atoms -> 2 clusters), all sub-threshold.
    REQUIRE( result.mergeAngles.size() == 3 );
    for ( double angle : result.mergeAngles )
        CHECK( angle < 2.0 * kDeg );

    // Threshold 0 keeps every atom as its own cluster.
    config.mergeAngleDegrees = 0.0;
    REQUIRE( reduceEndmembers( endmembers.data(), 5, 2, config, &ppiCounts,
                               &result, &err ) );
    CHECK( result.representativeOf.size() == 5 );
    CHECK( result.clusterSizes == std::vector<int>( 5, 1 ) );

    // Without PPI counts the lowest index wins the representative role.
    config.mergeAngleDegrees = 2.0;
    REQUIRE( reduceEndmembers( endmembers.data(), 5, 2, config, nullptr,
                               &result, &err ) );
    CHECK( result.representativeOf[0] == 0 );
}

TEST_CASE( "Reduction refuses degenerate inputs", "[endmember][kernel]" )
{
    const std::vector<float> withZero = { 1.0f, 0.0f, 0.0f, 0.0f };
    ReduceConfig config;
    ReduceResult result;
    QString err;
    CHECK_FALSE( reduceEndmembers( withZero.data(), 2, 2, config, nullptr, &result, &err ) );
    CHECK( err.contains( "zero norm" ) );

    const std::vector<float> ok = { 1.0f, 0.0f, 0.0f, 1.0f };
    const std::vector<int> wrongCounts = { 1 };
    CHECK_FALSE( reduceEndmembers( ok.data(), 2, 2, config, &wrongCounts, &result, &err ) );
    CHECK( err.contains( "ppiCounts" ) );

    config.mergeAngleDegrees = -1.0;
    CHECK_FALSE( reduceEndmembers( ok.data(), 2, 2, config, nullptr, &result, &err ) );
}

TEST_CASE( "Linear sensor projection hits exact interpolation values", "[endmember][projection]" )
{
    const std::vector<float> endmembers = { 1.0f, 2.0f, 3.0f };
    const std::vector<float> srcWl = { 100.0f, 200.0f, 300.0f };
    const std::vector<float> dstWl = { 150.0f, 250.0f };
    ProjectionResult result;
    QString err;
    REQUIRE( projectToSensor( endmembers.data(), 1, 3, srcWl.data(),
                              dstWl.data(), nullptr, 2, false, &result, &err ) );
    CHECK( result.spectra[0] == Approx( 1.5 ).margin( 1e-6 ) );
    CHECK( result.spectra[1] == Approx( 2.5 ).margin( 1e-6 ) );
    CHECK( result.fullyCovered[0] == 1 );
    CHECK( result.wavelengthsNm == dstWl );
    CHECK( result.fwhmNm.empty() );
}

TEST_CASE( "Gaussian projection preserves constant spectra and flags gaps", "[endmember][projection]" )
{
    // Constant 0.5 spectrum over a dense grid: Gaussian SRF weights sum to a
    // constant, so every projected value must stay 0.5 (invariance property).
    const int bands = 100;
    std::vector<float> endmembers( bands, 0.5f );
    std::vector<float> srcWl( bands );
    for ( int i = 0; i < bands; ++i )
        srcWl[i] = 400.0f + 10.0f * i; // 400..1390 nm
    const std::vector<float> dstWl = { 560.0f, 665.0f, 840.0f };
    const std::vector<float> dstFwhm = { 30.0f, 30.0f, 130.0f };

    ProjectionResult result;
    QString err;
    REQUIRE( projectToSensor( endmembers.data(), 1, bands, srcWl.data(),
                              dstWl.data(), dstFwhm.data(), 3, false, &result, &err ) );
    for ( int b = 0; b < 3; ++b )
        CHECK( result.spectra[b] == Approx( 0.5 ).margin( 1e-4 ) );

    // Targets beyond the source range produce NaN cells and a coverage flag.
    const std::vector<float> farWl = { 500.0f, 1500.0f, 1600.0f };
    REQUIRE( projectToSensor( endmembers.data(), 1, bands, srcWl.data(),
                              farWl.data(), dstFwhm.data(), 3, false, &result, &err ) );
    CHECK( std::isfinite( result.spectra[0] ) );
    CHECK( std::isnan( result.spectra[1] ) );
    CHECK( std::isnan( result.spectra[2] ) );
    CHECK( result.fullyCovered[0] == 0 );
    CHECK( result.fullyCoveredCount == 0 );

    // requireFull turns the coverage shortfall into a typed refusal.
    CHECK_FALSE( projectToSensor( endmembers.data(), 1, bands, srcWl.data(),
                                  farWl.data(), dstFwhm.data(), 3, true, &result, &err ) );
    CHECK( err.contains( "requireFull" ) );

    // Several endmembers: covered ones count individually.
    std::vector<float> two( 2 * bands, 0.5f );
    std::vector<float> dst3 = { 560.0f, 665.0f, 840.0f };
    const std::vector<float> fwhm3 = { 30.0f, 30.0f, 30.0f };
    REQUIRE( projectToSensor( two.data(), 2, bands, srcWl.data(), dst3.data(),
                              fwhm3.data(), 3, false, &result, &err ) );
    CHECK( result.fullyCoveredCount == 2 );
}

TEST_CASE( "Projection without wavelength metadata refuses", "[endmember][projection]" )
{
    const std::vector<float> endmembers = { 1.0f, 2.0f };
    const std::vector<float> dstWl = { 150.0f };
    ProjectionResult result;
    QString err;
    CHECK_FALSE( projectToSensor( endmembers.data(), 1, 2, nullptr, dstWl.data(),
                                  nullptr, 1, false, &result, &err ) );
    CHECK( err.contains( "wavelength metadata is mandatory" ) );

    const std::vector<float> badSrc = { 300.0f, 100.0f }; // decreasing
    CHECK_FALSE( projectToSensor( endmembers.data(), 1, 2, badSrc.data(),
                                  dstWl.data(), nullptr, 1, false, &result, &err ) );

    const std::vector<float> badFwhm = { -1.0f };
    const std::vector<float> srcWl = { 100.0f, 200.0f };
    CHECK_FALSE( projectToSensor( endmembers.data(), 1, 2, srcWl.data(),
                                  dstWl.data(), badFwhm.data(), 1, false, &result, &err ) );
}
