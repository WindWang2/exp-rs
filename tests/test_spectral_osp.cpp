// test_spectral_osp.cpp — Spectral Intelligence 13.0 work package A: OSP
// target-detector kernel (hand-computed projectors, exact null constraints,
// idempotence, typed refusals, scale semantics).
//
// Derivations (independent of the implementation):
//
//   d = (1,0), s = (1,1):  G = sᵀs = 2, Uᵀd = 1, z = 1/2
//     w = d − z·s = (1/2, −1/2)
//     P = I − ssᵀ/2 = [[1/2,−1/2],[−1/2,1/2]];  P² = P;  Pd = (1/2,−1/2) = w
//   d = (1,2,3), s1 = (1,0,0), s2 = (0,1,0):
//     G = I₂, Uᵀd = (1,2), z = (1,2)
//     w = (1,2,3) − (1,0,0) − (0,2,0) = (0,0,3)

#include "processing/algorithms/spectral_osp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace SpectralOsp;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

void requireNear( const std::vector<double> &got, const std::vector<double> &expected )
{
    REQUIRE( got.size() == expected.size() );
    for ( size_t i = 0; i < got.size(); ++i )
        REQUIRE( got[i] == Catch::Approx( expected[i] ).margin( 1e-12 ) );
}
} // namespace

TEST_CASE( "OSP filter: hand-computed projector in two bands",
           "[spectral][detection][osp]" )
{
    const float d[2] = { 1.0f, 0.0f };
    const std::vector<std::vector<float>> interference{ { 1.0f, 1.0f } };

    Filter filter;
    QString error;
    REQUIRE( buildFilter( d, 2, interference, &filter, &error ) );
    requireNear( filter.weight, { 0.5, -0.5 } );

    std::vector<double> scratch( 2, 0.0 );
    // The undesired signature scores exactly 0 (the suppression property).
    const float s[2] = { 1.0f, 1.0f };
    REQUIRE( ospScore( s, filter, 2, &scratch ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    // Closed-form scores: wᵀ(3,1) = 1, wᵀ(0,2) = −1.
    const float x1[2] = { 3.0f, 1.0f };
    REQUIRE( ospScore( x1, filter, 2, &scratch ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
    const float x2[2] = { 0.0f, 2.0f };
    REQUIRE( ospScore( x2, filter, 2, &scratch ) == Catch::Approx( -1.0 ).margin( 1e-12 ) );
    // Non-finite band → NaN.
    const float bad[2] = { kNaN, 1.0f };
    REQUIRE( std::isnan( ospScore( bad, filter, 2, &scratch ) ) );
}

TEST_CASE( "OSP projector is idempotent and equals the hand-computed matrix",
           "[spectral][detection][osp]" )
{
    // P = I − ssᵀ/2 for s = (1,1): [[1/2,−1/2],[−1/2,1/2]].
    const float d[2] = { 1.0f, 0.0f };
    const std::vector<std::vector<float>> interference{ { 1.0f, 1.0f } };
    Filter filter;
    REQUIRE( buildFilter( d, 2, interference, &filter ) );

    // P·d = w (already checked) and P·w = w (idempotence applied to the output).
    const double P[2][2] = { { 0.5, -0.5 }, { -0.5, 0.5 } };
    for ( int i = 0; i < 2; ++i )
    {
        const double pd = P[i][0] * 1.0 + P[i][1] * 0.0;
        REQUIRE( pd == Catch::Approx( filter.weight[static_cast<size_t>( i )] ).margin( 1e-12 ) );
        const double pw = P[i][0] * filter.weight[0] + P[i][1] * filter.weight[1];
        REQUIRE( pw == Catch::Approx( filter.weight[static_cast<size_t>( i )] ).margin( 1e-12 ) );
    }
    // P annihilates the interference span exactly.
    const double ps0 = P[0][0] * 1.0 + P[0][1] * 1.0;
    const double ps1 = P[1][0] * 1.0 + P[1][1] * 1.0;
    REQUIRE( ps0 == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( ps1 == Catch::Approx( 0.0 ).margin( 1e-12 ) );
}

TEST_CASE( "OSP filter: two undesired signatures in three bands",
           "[spectral][detection][osp]" )
{
    const float d[3] = { 1.0f, 2.0f, 3.0f };
    const std::vector<std::vector<float>> interference{ { 1.0f, 0.0f, 0.0f },
                                                        { 0.0f, 1.0f, 0.0f } };
    Filter filter;
    QString error;
    REQUIRE( buildFilter( d, 3, interference, &filter, &error ) );
    requireNear( filter.weight, { 0.0, 0.0, 3.0 } );

    std::vector<double> scratch( 3, 0.0 );
    const float s1[3] = { 1.0f, 0.0f, 0.0f };
    const float s2[3] = { 0.0f, 1.0f, 0.0f };
    REQUIRE( ospScore( s1, filter, 3, &scratch ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( ospScore( s2, filter, 3, &scratch ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    // Only the third band survives the projection: score = 3·x₃.
    const float x[3] = { 5.0f, 7.0f, 2.0f };
    REQUIRE( ospScore( x, filter, 3, &scratch ) == Catch::Approx( 6.0 ).margin( 1e-12 ) );
}

TEST_CASE( "OSP score scales with the target magnitude (documented semantics)",
           "[spectral][detection][osp]" )
{
    const std::vector<std::vector<float>> interference{ { 1.0f, 1.0f } };
    const float d[2] = { 1.0f, 0.0f };
    const float d2[2] = { 2.0f, 0.0f };

    Filter f1, f2;
    REQUIRE( buildFilter( d, 2, interference, &f1 ) );
    REQUIRE( buildFilter( d2, 2, interference, &f2 ) );

    std::vector<double> scratch( 2, 0.0 );
    const float x[2] = { 3.0f, 1.0f };
    const double s1 = ospScore( x, f1, 2, &scratch );
    const double s2 = ospScore( x, f2, 2, &scratch );
    REQUIRE( s2 == Catch::Approx( 2.0 * s1 ).margin( 1e-12 ) );
    // The weights scale exactly (same projection direction).
    REQUIRE( f2.weight[0] == Catch::Approx( 2.0 * f1.weight[0] ).margin( 1e-12 ) );
    REQUIRE( f2.weight[1] == Catch::Approx( 2.0 * f1.weight[1] ).margin( 1e-12 ) );
}

TEST_CASE( "OSP refuses degenerate signatures and empty interference",
           "[spectral][detection][osp]" )
{
    const float d[2] = { 1.0f, 0.0f };
    Filter filter;
    QString error;

    // Empty interference: OSP has no suppression semantics without at least
    // one undesired signature (unlike TCIMF, which degenerates to CEM).
    error.clear();
    REQUIRE_FALSE( buildFilter( d, 2, {}, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "at least one undesired signature" ) ) );

    // Duplicate columns → UᵀU singular.
    error.clear();
    REQUIRE_FALSE( buildFilter( d, 2, { { 1.0f, 0.0f }, { 1.0f, 0.0f } }, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "linearly dependent" ) ) );

    // Collinear columns → UᵀU singular.
    error.clear();
    REQUIRE_FALSE( buildFilter( d, 2, { { 1.0f, 0.0f }, { 2.0f, 0.0f } }, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "linearly dependent" ) ) );

    // Zero spectrum.
    error.clear();
    REQUIRE_FALSE( buildFilter( d, 2, { { 0.0f, 0.0f } }, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "zero spectrum" ) ) );

    // Non-finite interference value.
    error.clear();
    REQUIRE_FALSE( buildFilter( d, 2, { { 1.0f, kNaN } }, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "non-finite" ) ) );

    // Wrong band count.
    error.clear();
    REQUIRE_FALSE( buildFilter( d, 2, { { 1.0f, 2.0f, 3.0f } }, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "expected 2" ) ) );

    // Target inside the undesired subspace: d = (1,1), s = (1,1) → w = 0.
    error.clear();
    const float dInSpan[2] = { 1.0f, 1.0f };
    REQUIRE_FALSE( buildFilter( dInSpan, 2, { { 1.0f, 1.0f } }, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "inside the undesired subspace" ) ) );

    // Zero target.
    const float zero[2] = { 0.0f, 0.0f };
    error.clear();
    REQUIRE_FALSE( buildFilter( zero, 2, { { 1.0f, 1.0f } }, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "zero spectrum" ) ) );

    // Structural refusals.
    const float badTarget[2] = { 1.0f, kNaN };
    REQUIRE_FALSE( buildFilter( badTarget, 2, { { 1.0f, 1.0f } }, &filter ) );
    REQUIRE_FALSE( buildFilter( d, 0, { { 1.0f, 1.0f } }, &filter ) );
    REQUIRE_FALSE( buildFilter( d, 2, { { 1.0f, 1.0f } }, nullptr ) );
}

TEST_CASE( "OSP guards invalid call shapes on the score path",
           "[spectral][detection][osp]" )
{
    const float d[2] = { 1.0f, 0.0f };
    Filter filter;
    REQUIRE( buildFilter( d, 2, { { 1.0f, 1.0f } }, &filter ) );

    std::vector<double> scratch( 1, 0.0 );
    REQUIRE( std::isnan( ospScore( d, filter, 2, &scratch ) ) );
    Filter wrongWidth;
    wrongWidth.weight = { 1.0 };
    REQUIRE( std::isnan( ospScore( d, wrongWidth, 2, &scratch ) ) );
    REQUIRE( std::isnan( ospScore( nullptr, filter, 2, &scratch ) ) );
}

TEST_CASE( "OSP conditioning diagnostic separates healthy and near-collinear sets",
           "[spectral][detection][osp]" )
{
    // buildFilter reports λmax/λmin of the interference Gram matrix UᵀU. An
    // orthogonal pair is perfectly conditioned (cond == 1); a near-collinear
    // pair (angle ~1e-4 rad) has cond ≈ 1/eps² ≈ 4e8.
    const float d[3] = { 1.0f, 2.0f, 3.0f };
    const double eps = 1e-4;

    Filter healthy;
    double healthyCond = -1.0;
    REQUIRE( buildFilter( d, 3, { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, &healthy,
                          nullptr, &healthyCond ) );
    REQUIRE( healthyCond == Catch::Approx( 1.0 ).margin( 1e-9 ) );

    Filter ill;
    double illCond = -1.0;
    REQUIRE( buildFilter( d, 3,
                          { { 1.0f, 0.0f, 0.0f },
                            { static_cast<float>( std::cos( eps ) ), 0.0f,
                              static_cast<float>( std::sin( eps ) ) } },
                          &ill, nullptr, &illCond ) );
    REQUIRE( illCond > 1.0e6 );
}
