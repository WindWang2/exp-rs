// test_spectral_tcimf.cpp — Spectral Intelligence 13.0 work package A: TCIMF
// target-detector kernel (closed forms, exact constraints, typed refusals,
// CEM degeneracy, shared accumulator).
//
// Derivations (independent of the implementation; the closed forms below were
// solved by hand from the Lagrange conditions min wᵀRw s.t. wᵀt = 1, Sᵀw = 0):
//
//   R = diag(4,1), t = (1,1), S = {(1,−1)}:
//     R⁻¹ = diag(1/4, 1),  u = R⁻¹t = (1/4, 1)
//     A = SᵀR⁻¹S = 5/4,    b = SᵀR⁻¹t = −3/4,   z = A⁻¹b = −3/5
//     p = t − S z = (8/5, 2/5),   v = R⁻¹p = (2/5, 2/5)
//     denom = tᵀv = 4/5   ⇒  w = (1/2, 1/2)
//   R = diag(2,3,5), t = (1,2,3), S = {(1,0,0), (0,1,0)}:
//     A = diag(1/2, 1/3), b = (1/2, 2/3), z = (1, 2)
//     p = (0, 0, 3), v = (0, 0, 3/5), denom = 9/5  ⇒  w = (0, 0, 1/3)
//   R = diag(4,1), t = (1,1), S = {(1,−1)}, loading = 1:
//     R' = diag(6.5, 3.5), u = (1/6.5, 1/3.5), A = 40/91, b = −12/91, z = −3/10
//     p = (13/10, 7/10), v = (1/5, 1/5), denom = 2/5  ⇒  w = (1/2, 1/2)

#include "processing/algorithms/spectral_cem.h"
#include "processing/algorithms/spectral_tcimf.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

using namespace SpectralTcimf;

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

void requireNear( const std::vector<double> &got, const std::vector<double> &expected )
{
    REQUIRE( got.size() == expected.size() );
    for ( size_t i = 0; i < got.size(); ++i )
        REQUIRE( got[i] == Catch::Approx( expected[i] ).margin( 1e-12 ) );
}

double dot( const std::vector<double> &a, const std::vector<double> &b )
{
    double s = 0.0;
    for ( size_t i = 0; i < a.size(); ++i )
        s += a[i] * b[i];
    return s;
}
} // namespace

TEST_CASE( "TCIMF filter: hand-computed closed form with one interference",
           "[spectral][detection][tcimf]" )
{
    const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
    const float t[2] = { 1.0f, 1.0f };
    const std::vector<std::vector<float>> interference{ { 1.0f, -1.0f } };

    Filter filter;
    QString error;
    REQUIRE( buildFilter( t, 2, interference, corr, 0.0, &filter, &error ) );
    requireNear( filter.weight, { 0.5, 0.5 } );

    // Both constraints hold exactly (they are exact by construction).
    const std::vector<double> td{ 1.0, 1.0 }, sd{ 1.0, -1.0 };
    REQUIRE( dot( filter.weight, td ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
    REQUIRE( dot( filter.weight, sd ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );

    std::vector<double> scratch( 2, 0.0 );
    // The target scores exactly 1; the interference spectrum exactly 0.
    REQUIRE( tcimfScore( t, filter, 2, &scratch ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
    const float s[2] = { 1.0f, -1.0f };
    REQUIRE( tcimfScore( s, filter, 2, &scratch ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    // Closed form: 0.5·3 + 0.5·1 = 2.
    const float x[2] = { 3.0f, 1.0f };
    REQUIRE( tcimfScore( x, filter, 2, &scratch ) == Catch::Approx( 2.0 ).margin( 1e-12 ) );
    // Signed, like CEM: (1,−1)-orthogonal direction scores 0.
    const float other[2] = { 1.0f, -3.0f };
    REQUIRE( tcimfScore( other, filter, 2, &scratch ) == Catch::Approx( -1.0 ).margin( 1e-12 ) );
    // Non-finite band → NaN.
    const float bad[2] = { 1.0f, kNaN };
    REQUIRE( std::isnan( tcimfScore( bad, filter, 2, &scratch ) ) );
}

TEST_CASE( "TCIMF filter: two interference signatures in three bands",
           "[spectral][detection][tcimf]" )
{
    const std::vector<double> corr{ 2.0, 0.0, 0.0, 0.0, 3.0, 0.0, 0.0, 0.0, 5.0 };
    const float t[3] = { 1.0f, 2.0f, 3.0f };
    const std::vector<std::vector<float>> interference{ { 1.0f, 0.0f, 0.0f },
                                                        { 0.0f, 1.0f, 0.0f } };

    Filter filter;
    QString error;
    REQUIRE( buildFilter( t, 3, interference, corr, 0.0, &filter, &error ) );
    requireNear( filter.weight, { 0.0, 0.0, 1.0 / 3.0 } );

    const std::vector<double> td{ 1.0, 2.0, 3.0 };
    REQUIRE( dot( filter.weight, td ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );

    std::vector<double> scratch( 3, 0.0 );
    const float s1[3] = { 1.0f, 0.0f, 0.0f };
    const float s2[3] = { 0.0f, 1.0f, 0.0f };
    REQUIRE( tcimfScore( s1, filter, 3, &scratch ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( tcimfScore( s2, filter, 3, &scratch ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
    REQUIRE( tcimfScore( t, filter, 3, &scratch ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
}

TEST_CASE( "TCIMF degenerates to CEM when the interference matrix is empty",
           "[spectral][detection][tcimf]" )
{
    // Same correlation and loading on both sides: the TCIMF filter must equal
    // the CEM filter bit-for-bit (k = 0 is the CEM special case) and the
    // target must score exactly 1.
    const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
    const float t[2] = { 1.0f, 1.0f };

    for ( double loading : { 0.0, 1.0 } )
    {
        SpectralCem::Filter cem;
        REQUIRE( SpectralCem::buildFilter( t, 2, corr, loading, &cem ) );
        Filter tcimf;
        QString error;
        REQUIRE( buildFilter( t, 2, {}, corr, loading, &tcimf, &error ) );
        REQUIRE( tcimf.weight == cem.weight ); // bit-exact, not approximate

        std::vector<double> scratch( 2, 0.0 );
        REQUIRE( tcimfScore( t, tcimf, 2, &scratch ) ==
                 Catch::Approx( 1.0 ).margin( 1e-12 ) );
    }
}

TEST_CASE( "TCIMF loading escape hatch keeps the closed form",
           "[spectral][detection][tcimf]" )
{
    // R = diag(4,1), loading = 1 → R' = diag(6.5, 3.5); see header derivation:
    // w = (1/2, 1/2).
    const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
    const float t[2] = { 1.0f, 1.0f };
    const std::vector<std::vector<float>> interference{ { 1.0f, -1.0f } };

    Filter filter;
    QString error;
    REQUIRE( buildFilter( t, 2, interference, corr, 1.0, &filter, &error ) );
    requireNear( filter.weight, { 0.5, 0.5 } );
    REQUIRE( dot( filter.weight, { 1.0, 1.0 } ) == Catch::Approx( 1.0 ).margin( 1e-12 ) );
    REQUIRE( dot( filter.weight, { 1.0, -1.0 } ) == Catch::Approx( 0.0 ).margin( 1e-12 ) );
}

TEST_CASE( "TCIMF refuses degenerate interference and targets",
           "[spectral][detection][tcimf]" )
{
    const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
    const float t[2] = { 1.0f, 1.0f };
    Filter filter;
    QString error;

    // Duplicate interference columns → SᵀR⁻¹S singular.
    error.clear();
    REQUIRE_FALSE( buildFilter( t, 2, { { 1.0f, -1.0f }, { 1.0f, -1.0f } }, corr, 0.0,
                                &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "linearly dependent" ) ) );

    // Collinear (non-identical) interference columns → singular Gram.
    error.clear();
    REQUIRE_FALSE( buildFilter( t, 2, { { 1.0f, 0.0f }, { 2.0f, 0.0f } }, corr, 0.0,
                                &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "linearly dependent" ) ) );

    // Zero interference spectrum.
    error.clear();
    REQUIRE_FALSE( buildFilter( t, 2, { { 0.0f, 0.0f } }, corr, 0.0, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "zero spectrum" ) ) );

    // Non-finite interference value.
    error.clear();
    REQUIRE_FALSE( buildFilter( t, 2, { { 1.0f, kNaN } }, corr, 0.0, &filter, &error ) );
    REQUIRE( error.contains( QStringLiteral( "non-finite" ) ) );

    // Wrong band count.
    error.clear();
    REQUIRE_FALSE( buildFilter( t, 2, { { 1.0f, 2.0f, 3.0f } }, corr, 0.0, &filter,
                                &error ) );
    REQUIRE( error.contains( QStringLiteral( "expected 2" ) ) );

    // Target inside the interference span: t = (1,1), s = (1,1).
    error.clear();
    const float tInSpan[2] = { 1.0f, 1.0f };
    REQUIRE_FALSE( buildFilter( tInSpan, 2, { { 1.0f, 1.0f } }, corr, 0.0, &filter,
                                &error ) );
    REQUIRE( error.contains( QStringLiteral( "inside the interference span" ) ) );

    // Structural refusals shared with CEM.
    REQUIRE_FALSE( buildFilter( t, 2, {}, { 1.0, 0.0, 0.0, -1.0 }, 0.0, &filter ) );
    REQUIRE_FALSE( buildFilter( t, 2, {}, corr, -1.0, &filter ) );
    const float badTarget[2] = { 1.0f, kNaN };
    REQUIRE_FALSE( buildFilter( badTarget, 2, {}, corr, 0.0, &filter ) );
    REQUIRE_FALSE( buildFilter( nullptr, 2, {}, corr, 0.0, &filter ) );
}

TEST_CASE( "TCIMF shares the CEM streaming accumulator",
           "[spectral][detection][tcimf]" )
{
    // The driver accumulates the scene correlation once and can build either
    // detector: accumulate a small scene through SpectralCem's accumulator,
    // finalize, then build the TCIMF filter and score against a kernel-level
    // reference computed from the same accumulated matrix.
    const int bands = 2;
    const std::vector<float> pixels = {
        1.0f, 0.5f, 2.0f, 1.5f, 0.5f, 0.25f, 3.0f, 2.0f, 1.5f, 0.75f, 2.5f, 1.25f,
    };
    CorrelationStats stats;
    accumulateCorrelation( pixels.data(), pixels.size() / bands, bands, &stats );
    REQUIRE( stats.count == 6 );
    finalizeCorrelation( &stats );

    const float t[2] = { 1.0f, 1.0f };
    Filter filter;
    QString error;
    REQUIRE( buildFilter( t, bands, { { 1.0f, -1.0f } }, stats.correlation, 0.0, &filter,
                          &error ) );

    // Independent reference: recompute the closed form from the finalized
    // matrix with a straightforward (independent) 2×2 inverse.
    const double a = stats.correlation[0], b = stats.correlation[1],
                 d = stats.correlation[3];
    const double det = a * d - b * b;
    REQUIRE( det > 0.0 );
    const std::vector<double> invCorr = { d / det, -b / det, -b / det, a / det };
    const std::vector<double> u = { invCorr[0] * 1.0 + invCorr[1] * 1.0,
                                    invCorr[2] * 1.0 + invCorr[3] * 1.0 };
    const double A = 1.0 * ( invCorr[0] - invCorr[1] ) + ( -1.0 ) * ( invCorr[2] - invCorr[3] );
    const double rhs = 1.0 * u[0] + ( -1.0 ) * u[1];
    const double z = rhs / A;
    const std::vector<double> p = { 1.0 - z, 1.0 + z };
    const std::vector<double> v = { invCorr[0] * p[0] + invCorr[1] * p[1],
                                    invCorr[2] * p[0] + invCorr[3] * p[1] };
    const double denom = 1.0 * v[0] + 1.0 * v[1];
    const std::vector<double> expected = { v[0] / denom, v[1] / denom };
    requireNear( filter.weight, expected );

    // Min-sample floor is the CEM floor (same estimator).
    REQUIRE( minSamplesRequired( bands, false ) == 2 * bands + 2 );
    REQUIRE( minSamplesRequired( bands, true ) == bands + 1 );
}

TEST_CASE( "TCIMF conditioning diagnostic separates healthy and near-collinear sets",
           "[spectral][detection][tcimf]" )
{
    // buildFilter reports λmax/λmin of the interference Gram matrix SᵀR'⁻¹S.
    // Orthogonal-under-R⁻¹ signatures are perfectly conditioned (cond == 1);
    // a near-collinear pair has cond ≈ 1/eps².
    const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
    const float t[2] = { 1.0f, 1.0f };

    Filter healthy;
    double healthyCond = -1.0;
    REQUIRE( buildFilter( t, 2, { { 1.0f, -1.0f } }, corr, 0.0, &healthy, nullptr,
                          &healthyCond ) );
    // Single interference signature: the Gram is 1×1 → cond == 1 exactly.
    REQUIRE( healthyCond == Catch::Approx( 1.0 ).margin( 1e-9 ) );

    const double eps = 1e-4;
    Filter ill;
    double illCond = -1.0;
    REQUIRE( buildFilter( t, 2,
                          { { 1.0f, 0.0f },
                            { static_cast<float>( std::cos( eps ) ),
                              static_cast<float>( std::sin( eps ) ) } },
                          corr, 0.0, &ill, nullptr, &illCond ) );
    REQUIRE( illCond > 1.0e6 );

    // The null constraints are exact in exact arithmetic; the REALIZED
    // residual scales with the conditioning (cancellation in t − S z). For a
    // well-conditioned set it is at round-off; for the near-collinear set it
    // is bounded by the documented ~1e-12·cond degradation.
    std::vector<double> scratch( 2, 0.0 );
    const float healthyS[2] = { 1.0f, -1.0f }; // the healthy filter's interference
    const float illS2[2] = { static_cast<float>( std::cos( eps ) ),
                             static_cast<float>( std::sin( eps ) ) };
    REQUIRE( std::fabs( tcimfScore( healthyS, healthy, 2, &scratch ) ) <= 1e-12 );
    REQUIRE( std::fabs( tcimfScore( illS2, ill, 2, &scratch ) ) <= 1e-12 * illCond );

    // No interference → the diagnostic is "unknown" (-1), not a fake 1.
    Filter none;
    double noneCond = 0.0;
    REQUIRE( buildFilter( t, 2, {}, corr, 0.0, &none, nullptr, &noneCond ) );
    REQUIRE( noneCond == -1.0 );
}

TEST_CASE( "TCIMF guards against invalid call shapes",
           "[spectral][detection][tcimf]" )
{
    const std::vector<double> corr{ 4.0, 0.0, 0.0, 1.0 };
    const float t[2] = { 1.0f, 1.0f };
    Filter filter;

    REQUIRE_FALSE( buildFilter( t, 0, {}, corr, 0.0, &filter ) );
    REQUIRE_FALSE( buildFilter( t, 2, {}, { 1.0, 0.0 }, 0.0, &filter ) );
    REQUIRE_FALSE( buildFilter( t, 2, {}, corr, 0.0, nullptr ) );

    std::vector<double> scratch( 1, 0.0 );
    Filter good;
    REQUIRE( buildFilter( t, 2, {}, corr, 0.0, &good ) );
    // Scratch too small / wrong filter width → NaN, not garbage.
    REQUIRE( std::isnan( tcimfScore( t, good, 2, &scratch ) ) );
    Filter wrongWidth;
    wrongWidth.weight = { 1.0 };
    REQUIRE( std::isnan( tcimfScore( t, wrongWidth, 2, &scratch ) ) );
    REQUIRE( std::isnan( tcimfScore( nullptr, good, 2, &scratch ) ) );
}
