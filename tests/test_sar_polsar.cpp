// tests/test_sar_polsar.cpp — PolSAR decomposition kernels
// (Advanced SAR / PolSAR / InSAR 10.0, package B).
//
// Known-answer anchors:
//  * hermitianEigen3 — constructed eigen-pairs (complex unitary similarity
//    transforms), residual/orthonormality/ordering checks.
//  * Pauli powers — canonical single-look targets (sphere, dihedral,
//    dipole) with exact closed forms.
//  * Cloude-Pottier — rank-1 target → H = 0; two-target mixtures with
//    hand-computed exact H/A/α; unitary eigenvalue agreement T vs C.
//  * Freeman-Durden / Yamaguchi — synthesized model covariances recover
//    the generating powers exactly (branch-consistent parameters), and
//    SPAN conservation holds on unclamped cases.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "processing/algorithms/sar/sar_hermitian3.h"
#include "processing/algorithms/sar/sar_polsar.h"

using namespace sicnu::sar;
using Catch::Approx;

namespace
{

using cd = std::complex<double>;

/// Explicit rotation MATRIX (identity except the (p,q) 2×2 unitary block
/// [[c, uPq], [−conj(uPq), c]]) — no in-place index tricks, so the
/// construction cannot get the left/right handedness wrong.
void rotationMatrix( cd M[3][3], double c, cd uPq, int p, int q )
{
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            M[i][j] = ( i == j ) ? cd( 1.0, 0.0 ) : cd( 0.0, 0.0 );
    M[p][p] = M[q][q] = cd( c, 0.0 );
    M[p][q] = uPq;
    M[q][p] = -std::conj( uPq );
}

/// C = A·B (3×3 complex matrix product).
void matMul( const cd A[3][3], const cd B[3][3], cd C[3][3] )
{
    cd R[3][3] = {};
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            for ( int k = 0; k < 3; ++k )
                R[i][j] += A[i][k] * B[k][j];
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            C[i][j] = R[i][j];
}

/// Hermitian matrix with prescribed real eigenvalues and a deterministic
/// unitary mixing (two plane rotations with irrational-ish angles).
void synthHermitian( double l0, double l1, double l2, cd A[3][3] )
{
    cd R01[3][3], R12[3][3], U[3][3], Uh[3][3], D[3][3], T[3][3];
    rotationMatrix( R01, 0.8, { 0.6, 0.0 }, 0, 1 );
    rotationMatrix( R12, 0.28, { 0.0, 0.96 }, 1, 2 );
    matMul( R01, R12, U ); // unitary by construction
    // Uh = U^H
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            Uh[i][j] = std::conj( U[j][i] );
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            D[i][j] = ( i == j ) ? cd( ( i == 0 ? l0 : ( i == 1 ? l1 : l2 ) ), 0.0 )
                                 : cd( 0.0, 0.0 );
    matMul( D, Uh, T );   // D·U^H
    matMul( U, T, A );    // U·D·U^H
    for ( int i = 0; i < 3; ++i )
        for ( int j = i + 1; j < 3; ++j )
            A[j][i] = std::conj( A[i][j] );
}

/// Covariance of a single-look ensemble of one target repeated N times
/// (weights scaled directly: means of u·u^H with the given complex mean
/// amplitudes).
PolEnsemble3 targetCovariance( cd shh, cd shv, cd svv )
{
    PolEnsemble3 ens;
    accumulatePolSample( ens, shh, shv, svv );
    PolEnsemble3 cov;
    REQUIRE( finalizeEnsemble( ens, &cov ) );
    return cov;
}

} // anonymous namespace

TEST_CASE( "hermitianEigen3 — diagonal and zero matrices", "[sar][polsar]" )
{
    HermitianEigen3 e;
    REQUIRE( hermitianEigen3( 3.0, 1.0, 2.0, {}, {}, {}, &e ) );
    REQUIRE( e.lambda[0] == Approx( 3.0 ) );
    REQUIRE( e.lambda[1] == Approx( 2.0 ) );
    REQUIRE( e.lambda[2] == Approx( 1.0 ) );

    REQUIRE( hermitianEigen3( 0.0, 0.0, 0.0, {}, {}, {}, &e ) );
    REQUIRE( e.lambda[0] == 0.0 );
    REQUIRE( e.lambda[2] == 0.0 );

    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE( hermitianEigen3( nan, 1.0, 2.0, {}, {}, {}, &e ) );
    REQUIRE_FALSE( hermitianEigen3( 1.0, 1.0, 2.0, { nan, 0.0 }, {}, {}, &e ) );
}

TEST_CASE( "hermitianEigen3 — constructed eigen-systems", "[sar][polsar]" )
{
    for ( const auto &spec : std::vector<std::array<double, 3>>{ { 4.0, 1.0, 0.0 },
                                                                 { 5.0, 5.0, 1.0 },
                                                                 { 9.0, 0.5, 0.25 },
                                                                 { 2.0, -1.0, -3.0 } } )
    {
        cd A[3][3];
        synthHermitian( spec[0], spec[1], spec[2], A );

        HermitianEigen3 e;
        REQUIRE( hermitianEigen3( A[0][0].real(), A[1][1].real(), A[2][2].real(),
                                  A[0][1], A[0][2], A[1][2], &e ) );

        // Descending order.
        REQUIRE( e.lambda[0] >= e.lambda[1] );
        REQUIRE( e.lambda[1] >= e.lambda[2] );
        // Known eigenvalues (sorted).
        std::vector<double> expected{ spec[0], spec[1], spec[2] };
        std::sort( expected.begin(), expected.end(), std::greater<double>() );
        for ( int i = 0; i < 3; ++i )
            REQUIRE( e.lambda[i] == Approx( expected[i] ).margin( 1e-9 ) );

        // Eigen residual + orthonormality.
        const double residual = hermitianEigen3Residual(
            A[0][0].real(), A[1][1].real(), A[2][2].real(), A[0][1], A[0][2], A[1][2], e );
        REQUIRE( residual < 1e-9 );

        for ( int i = 0; i < 3; ++i )
        {
            cd dot{};
            for ( int k = 0; k < 3; ++k )
                dot += std::conj( e.vec[i][k] ) * e.vec[i][k];
            REQUIRE( std::abs( dot ) == Approx( 1.0 ).margin( 1e-9 ) );
        }
        for ( int i = 0; i < 3; ++i )
            for ( int j = i + 1; j < 3; ++j )
            {
                cd dot{};
                for ( int k = 0; k < 3; ++k )
                    dot += std::conj( e.vec[i][k] ) * e.vec[j][k];
                REQUIRE( std::abs( dot ) < 1e-9 );
            }
    }
}

TEST_CASE( "Pauli powers — canonical single-look targets", "[sar][polsar]" )
{
    // Sphere (trihedral): SHH = SVV = 1 → all odd bounce.
    PauliPowers p = pauliPowers( { 1, 0 }, { 0, 0 }, { 1, 0 } );
    REQUIRE( p.odd == Approx( 2.0 ) );
    REQUIRE( p.doubleBounce == 0.0 );
    REQUIRE( p.volume == 0.0 );
    REQUIRE( p.span == Approx( 2.0 ) );

    // Dihedral: SVV = −1 → all double bounce.
    p = pauliPowers( { 1, 0 }, { 0, 0 }, { -1, 0 } );
    REQUIRE( p.odd == 0.0 );
    REQUIRE( p.doubleBounce == Approx( 2.0 ) );
    REQUIRE( p.span == Approx( 2.0 ) );

    // Dipole (volume): only SHV.
    p = pauliPowers( { 0, 0 }, { 1, 0 }, { 0, 0 } );
    REQUIRE( p.volume == Approx( 2.0 ) );
    REQUIRE( p.odd == 0.0 );
    REQUIRE( p.span == Approx( 2.0 ) );

    // SPAN invariance under a common phase shift (physical: absolute phase
    // of the illuminating wave is unobservable).
    const cd shh{ 0.5, -0.2 };
    const cd shv{ 0.1, 0.3 };
    const cd svv{ -0.4, 0.7 };
    const double span0 = pauliPowers( shh, shv, svv ).span;
    const cd phase = std::exp( cd( 0, 0.7 ) );
    const double spanPhase = pauliPowers( shh * phase, shv * phase, svv * phase ).span;
    REQUIRE( spanPhase == Approx( span0 ) );
}

TEST_CASE( "covariance → coherency: Hermitian, SPAN-carrying, self-consistent",
           "[sar][polsar]" )
{
    const PolEnsemble3 cov = targetCovariance( { 0.5, -0.2 }, { 0.1, 0.3 }, { -0.4, 0.7 } );
    const Coherency3 t = coherencyFromCovariance( cov );

    HermitianEigen3 et;
    REQUIRE( hermitianEigen3( t.t11, t.t22, t.t33, t.t12, t.t13, t.t23, &et ) );
    // Solver self-check on the produced T.
    const double residual = hermitianEigen3Residual( t.t11, t.t22, t.t33, t.t12, t.t13, t.t23, et );
    REQUIRE( residual < 1e-9 );
    // Descending order.
    REQUIRE( et.lambda[0] >= et.lambda[1] );
    REQUIRE( et.lambda[1] >= et.lambda[2] );

    // trace(T) carries the 4-port SPAN (both cross-pol channels): C11 +
    // 2·C22 + C33 — the Pauli basis is unitary on the 4-port space, NOT on
    // the reciprocal 3×3 C (see sar_polsar.h).
    const double span4 = cov.c11 + 2.0 * cov.c22 + cov.c33;
    REQUIRE( et.lambda[0] + et.lambda[1] + et.lambda[2] == Approx( span4 ).margin( 1e-12 ) );

    // Direct closed forms on this sample: SPAN = |a|²+2|c|²+|b|² with
    // a = 0.5−0.2j, c = 0.1+0.3j, b = −0.4+0.7j.
    const double a2 = 0.25 + 0.04;
    const double c2 = 0.01 + 0.09;
    const double b2 = 0.16 + 0.49;
    REQUIRE( et.lambda[0] + et.lambda[1] + et.lambda[2] == Approx( a2 + 2 * c2 + b2 ).margin( 1e-12 ) );
}

TEST_CASE( "cloudePottier — rank-1 and exact mixtures", "[sar][polsar]" )
{
    SECTION( "rank-1 target: H = 0, A undefined (NaN), alpha exact" )
    {
        // Sphere: k = [√2, 0, 0]/... → α = arccos(|v1|) = 0.
        const PolEnsemble3 cov = targetCovariance( { 1, 0 }, { 0, 0 }, { 1, 0 } );
        HAlphaResult r;
        REQUIRE( cloudePottier( cov, &r ) );
        REQUIRE( r.entropy == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.alphaMean == Approx( 0.0 ).margin( 1e-9 ) );
        REQUIRE( std::isnan( r.anisotropy ) );
        REQUIRE( r.dominance == Approx( 1.0 ) );
    }

    SECTION( "dihedral: α = 90°" )
    {
        const PolEnsemble3 cov = targetCovariance( { 1, 0 }, { 0, 0 }, { -1, 0 } );
        HAlphaResult r;
        REQUIRE( cloudePottier( cov, &r ) );
        REQUIRE( r.alphaMean == Approx( 90.0 ).margin( 1e-9 ) );
        REQUIRE( r.entropy == Approx( 0.0 ).margin( 1e-12 ) );
    }

    SECTION( "two-target mixture: exact eigenvalues, H, and alpha" )
    {
        // Ensemble: half sphere (k1 power 2), half dipole (k3 power 2).
        // Mean covariance: c11 = 0.5, c22 = 0.5, c33 = 0.5, c13 = 0.5.
        PolEnsemble3 ens;
        accumulatePolSample( ens, { 1, 0 }, { 0, 0 }, { 1, 0 } );
        accumulatePolSample( ens, { 0, 0 }, { 1, 0 }, { 0, 0 } );
        PolEnsemble3 cov;
        REQUIRE( finalizeEnsemble( ens, &cov ) );

        HAlphaResult r;
        REQUIRE( cloudePottier( cov, &r ) );
        // Eigenvalues {1, 1, 0} → H = log_3(2·(1/2)·ln?) = ln2/ln3.
        REQUIRE( r.lambda[0] == Approx( 1.0 ).margin( 1e-9 ) );
        REQUIRE( r.lambda[1] == Approx( 1.0 ).margin( 1e-9 ) );
        REQUIRE( r.lambda[2] == Approx( 0.0 ).margin( 1e-9 ) );
        REQUIRE( r.entropy == Approx( std::log( 2.0 ) / std::log( 3.0 ) ).margin( 1e-9 ) );
        REQUIRE( r.anisotropy == Approx( 1.0 ).margin( 1e-9 ) );
        // Mean α = 0.5·0° + 0.5·90° = 45°.
        REQUIRE( r.alphaMean == Approx( 45.0 ).margin( 1e-6 ) );
        REQUIRE( r.dominance == Approx( 0.5 ).margin( 1e-9 ) );
    }

    SECTION( "zero power: NaN statistics, not fabricated zeros" )
    {
        PolEnsemble3 ens;
        PolEnsemble3 cov;
        REQUIRE_FALSE( finalizeEnsemble( ens, &cov ) );
    }
}

TEST_CASE( "freemanDurden — exact recovery on synthesized models", "[sar][polsar]" )
{
    SECTION( "pure surface (β ≠ 0)" )
    {
        // S = s·[1, 0, β] → fs = |s|².
        const cd amp{ 2.0, 0.0 };
        const cd beta{ 0.3, 0.1 };
        PolEnsemble3 cov = targetCovariance( amp, { 0, 0 }, amp * beta );
        FreemanDurdenResult r;
        REQUIRE( freemanDurden( cov, &r ) );
        REQUIRE( r.volume == 0.0 );
        REQUIRE( r.doubleBounce == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.surface
                 == Approx( std::norm( amp ) * ( 1.0 + std::norm( beta ) ) ).margin( 1e-12 ) );
    }

    SECTION( "pure double (α ≠ 0)" )
    {
        // S = d·[α, 0, 1] → fd = |d|².
        const cd amp{ 2.0, 0.0 };
        const cd alpha{ -0.5, 0.2 };
        PolEnsemble3 cov = targetCovariance( amp * alpha, { 0, 0 }, amp );
        FreemanDurdenResult r;
        REQUIRE( freemanDurden( cov, &r ) );
        REQUIRE( r.volume == 0.0 );
        REQUIRE( r.surface == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.doubleBounce
                 == Approx( std::norm( amp ) * ( 1.0 + std::norm( alpha ) ) ).margin( 1e-12 ) );
    }

    SECTION( "pure volume" )
    {
        const double fv = 2.0;
        // Cv = fv·[[1,0,1/3],[0,2/3,0],[1/3,0,1]] → c11 = c33 = fv, c22 = 2fv/3,
        // c13 = fv/3.
        PolEnsemble3 cov;
        cov.c11 = fv;
        cov.c22 = 2.0 * fv / 3.0;
        cov.c33 = fv;
        cov.c13 = { fv / 3.0, 0.0 };
        cov.samples = 1;
        FreemanDurdenResult r;
        REQUIRE( freemanDurden( cov, &r ) );
        REQUIRE( r.surface == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.doubleBounce == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.volume == Approx( ( 8.0 / 3.0 ) * fv ).margin( 1e-12 ) );
    }

    SECTION( "surface + volume (α = 0): exact split + SPAN conservation" )
    {
        const double fs = 2.0;
        const cd beta{ 0.3, 0.1 };
        const double fv = 1.0;
        PolEnsemble3 cov;
        cov.c11 = fs + fv;
        cov.c22 = 2.0 * fv / 3.0;
        cov.c33 = fs * std::norm( beta ) + fv;
        cov.c13 = fs * std::conj( beta ) + double( fv ) / 3.0;
        cov.samples = 1;

        FreemanDurdenResult r;
        REQUIRE( freemanDurden( cov, &r ) );
        REQUIRE( r.surface == Approx( fs * ( 1.0 + std::norm( beta ) ) ).margin( 1e-12 ) );
        REQUIRE( r.doubleBounce == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.volume == Approx( ( 8.0 / 3.0 ) * fv ).margin( 1e-12 ) );

        const double span = cov.c11 + cov.c22 + cov.c33;
        REQUIRE( r.surface + r.doubleBounce + r.volume == Approx( span ).margin( 1e-12 ) );
    }

    SECTION( "double + volume (β = 0): exact split + SPAN conservation" )
    {
        const double fd = 3.0;
        const cd alpha{ -0.5, 0.2 };
        const double fv = 0.6;
        PolEnsemble3 cov;
        cov.c11 = fd * std::norm( alpha ) + fv;
        cov.c22 = 2.0 * fv / 3.0;
        cov.c33 = fd + fv;
        cov.c13 = double( fd ) * alpha + double( fv ) / 3.0;
        cov.samples = 1;

        FreemanDurdenResult r;
        REQUIRE( freemanDurden( cov, &r ) );
        REQUIRE( r.surface == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.doubleBounce == Approx( fd * ( 1.0 + std::norm( alpha ) ) ).margin( 1e-12 ) );
        REQUIRE( r.volume == Approx( ( 8.0 / 3.0 ) * fv ).margin( 1e-12 ) );

        const double span = cov.c11 + cov.c22 + cov.c33;
        REQUIRE( r.surface + r.doubleBounce + r.volume == Approx( span ).margin( 1e-12 ) );
    }
}

TEST_CASE( "yamaguchi4 — exact recovery incl. helix", "[sar][polsar]" )
{
    SECTION( "surface + volume + helix (α = 0): exact" )
    {
        const double fs = 2.0;
        const cd beta{ 0.3, 0.1 };
        const double fv = 1.0;
        const double fh = 0.8;
        PolEnsemble3 cov;
        cov.c11 = fs + fv + fh / 4.0;
        cov.c22 = 2.0 * fv / 3.0 + fh / 4.0;
        cov.c33 = fs * std::norm( beta ) + fv + fh / 4.0;
        cov.c13 = fs * std::conj( beta ) + double( fv ) / 3.0 - fh / 4.0;
        cov.c12 = { 0.0, -fh / 4.0 };
        cov.c23 = { 0.0, -fh / 4.0 };
        cov.samples = 1;

        YamaguchiResult r;
        REQUIRE( yamaguchi4( cov, &r ) );
        REQUIRE( r.surface == Approx( fs * ( 1.0 + std::norm( beta ) ) ).margin( 1e-12 ) );
        REQUIRE( r.doubleBounce == Approx( 0.0 ).margin( 1e-12 ) );
        REQUIRE( r.volume == Approx( ( 8.0 / 3.0 ) * fv ).margin( 1e-12 ) );
        REQUIRE( r.helix == Approx( fh * ( 3.0 / 4.0 ) ).margin( 1e-12 ) );

        const double span = cov.c11 + cov.c22 + cov.c33;
        REQUIRE( r.surface + r.doubleBounce + r.volume + r.helix
                 == Approx( span ).margin( 1e-12 ) );
    }

    SECTION( "left helicity (fh < 0) mirrors the power" )
    {
        const double fh = -0.8;
        PolEnsemble3 cov;
        cov.c11 = fh / 4.0;
        cov.c22 = fh / 4.0;
        cov.c33 = fh / 4.0;
        cov.c13 = -fh / 4.0;
        cov.c12 = { 0.0, -fh / 4.0 };
        cov.c23 = { 0.0, -fh / 4.0 };
        cov.samples = 1;

        YamaguchiResult r;
        REQUIRE( yamaguchi4( cov, &r ) );
        REQUIRE( r.helix == Approx( std::abs( fh ) * ( 3.0 / 4.0 ) ).margin( 1e-12 ) );
    }

    SECTION( "reflection-symmetric input (Im C12 = Im C23 = 0) reduces to Freeman-Durden" )
    {
        // Reflection symmetry: C12 and C23 must be REAL (choose a real SVV).
        PolEnsemble3 cov = targetCovariance( { 2, 0 }, { 0.4, 0 }, { -0.9, 0 } );
        FreemanDurdenResult fd;
        REQUIRE( freemanDurden( cov, &fd ) );
        YamaguchiResult y;
        REQUIRE( yamaguchi4( cov, &y ) );
        REQUIRE( y.surface == Approx( fd.surface ).margin( 1e-12 ) );
        REQUIRE( y.doubleBounce == Approx( fd.doubleBounce ).margin( 1e-12 ) );
        REQUIRE( y.volume == Approx( fd.volume ).margin( 1e-12 ) );
        REQUIRE( y.helix == Approx( 0.0 ).margin( 1e-12 ) );
    }
}

TEST_CASE( "ensemble accumulation ignores invalid samples", "[sar][polsar]" )
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    PolEnsemble3 ens;
    accumulatePolSample( ens, { 1, 0 }, { 0, 0 }, { 1, 0 } );
    accumulatePolSample( ens, { nan, 0 }, { 0, 0 }, { 1, 0 } );
    accumulatePolSample( ens, { 1, 0 }, { 0, 0 }, { 0, nan } );
    REQUIRE( ens.samples == 1 );
    PolEnsemble3 cov;
    REQUIRE( finalizeEnsemble( ens, &cov ) );
    REQUIRE( cov.c11 == Approx( 1.0 ) );
    REQUIRE( cov.c33 == Approx( 1.0 ) );
    REQUIRE( cov.c13.real() == Approx( 1.0 ) );
}
