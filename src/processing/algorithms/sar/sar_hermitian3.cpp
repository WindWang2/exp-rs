// src/processing/algorithms/sar/sar_hermitian3.cpp — cyclic Jacobi
// eigen-decomposition for 3×3 Hermitian matrices. Contract: sar_hermitian3.h.
#include "sar_hermitian3.h"

#include <algorithm>
#include <cmath>

namespace sicnu::sar
{

namespace
{
constexpr int kMaxSweeps = 60;

using C3 = std::complex<double>[3][3];

/// One Hermitian Jacobi rotation: A ← U^H·A·U with U = I except
/// U_pp = U_qq = c, U_pq = s·e^{iφ}, U_qp = −s·e^{−iφ}, where φ = arg(A_pq).
/// This zeroes A[p][q] (annihilating condition t² + 2θt − 1 = 0 with
/// θ = (A_qq − A_pp)/(2|A_pq|); see the sweep below).
struct Givens
{
    double c = 1.0;
    std::complex<double> uPq = { 0.0, 0.0 }; // U[p][q]
    int p = 0;
    int q = 0;
};

void applyJacobi( C3 &a, const Givens &g )
{
    const int p = g.p;
    const int q = g.q;
    const std::complex<double> uPq = g.uPq;

    // LEFT: U^H·A — mixes rows p,q across every column m.
    for ( int m = 0; m < 3; ++m )
    {
        const std::complex<double> apm = a[p][m];
        const std::complex<double> aqm = a[q][m];
        a[p][m] = g.c * apm - g.uPq * aqm;
        a[q][m] = std::conj( g.uPq ) * apm + g.c * aqm;
    }
    // Right multiply by U (mixes COLUMNS p,q of every row):
    //   (BU)[k][p] = c·B[k][p] − conj(uPq)·B[k][q]
    //   (BU)[k][q] = uPq·B[k][p] + c·B[k][q]
    for ( int k = 0; k < 3; ++k )
    {
        const std::complex<double> akp = a[k][p];
        const std::complex<double> akq = a[k][q];
        a[k][p] = g.c * akp - std::conj( uPq ) * akq;
        a[k][q] = uPq * akp + g.c * akq;
    }
    // Hermitian bookkeeping: restore exact conj symmetry + real diagonal
    // (round-off would otherwise accumulate imaginary drift).
    for ( int i = 0; i < 3; ++i )
    {
        for ( int j = i + 1; j < 3; ++j )
            a[j][i] = std::conj( a[i][j] );
        a[i][i] = { std::real( a[i][i] ), 0.0 };
    }
}

void rotateVectors( std::complex<double> v[3][3], const Givens &g )
{
    const int p = g.p;
    const int q = g.q;
    const std::complex<double> uPq = g.uPq;
    // V ← V·U (rows of V are eigenvector coordinates).
    for ( int k = 0; k < 3; ++k )
    {
        const std::complex<double> vkp = v[k][p];
        const std::complex<double> vkq = v[k][q];
        v[k][p] = g.c * vkp - std::conj( uPq ) * vkq;
        v[k][q] = uPq * vkp + g.c * vkq;
    }
}

} // anonymous namespace

bool hermitianEigen3( double a11, double a22, double a33,
                      std::complex<double> a12, std::complex<double> a13,
                      std::complex<double> a23,
                      HermitianEigen3 *out )
{
    if ( !out )
        return false;

    // Non-finite input can never converge to a valid eigen-system.
    if ( !std::isfinite( a11 ) || !std::isfinite( a22 ) || !std::isfinite( a33 )
         || !std::isfinite( a12.real() ) || !std::isfinite( a12.imag() )
         || !std::isfinite( a13.real() ) || !std::isfinite( a13.imag() )
         || !std::isfinite( a23.real() ) || !std::isfinite( a23.imag() ) )
        return false;

    C3 a = {};
    a[0][0] = { a11, 0.0 };
    a[1][1] = { a22, 0.0 };
    a[2][2] = { a33, 0.0 };
    a[0][1] = a12;
    a[1][0] = std::conj( a12 );
    a[0][2] = a13;
    a[2][0] = std::conj( a13 );
    a[1][2] = a23;
    a[2][1] = std::conj( a23 );

    std::complex<double> v[3][3] = {};
    v[0][0] = v[1][1] = v[2][2] = { 1.0, 0.0 };

    // Frobenius scale for the relative convergence threshold.
    double scale = std::abs( a[0][0] ) + std::abs( a[1][1] ) + std::abs( a[2][2] )
                   + 2.0 * ( std::abs( a[0][1] ) + std::abs( a[0][2] ) + std::abs( a[1][2] ) );
    if ( scale == 0.0 )
        scale = 1.0;
    const double tol = 1e-15 * scale;

    for ( int sweep = 0; sweep < kMaxSweeps; ++sweep )
    {
        const double off = std::abs( a[0][1] ) + std::abs( a[0][2] ) + std::abs( a[1][2] );
        if ( off <= tol )
            break;

        const int pairs[3][2] = { { 0, 1 }, { 0, 2 }, { 1, 2 } };
        for ( const auto &pq : pairs )
        {
            const int p = pq[0];
            const int q = pq[1];
            const std::complex<double> apq = a[p][q];
            if ( std::abs( apq ) <= tol )
                continue;

            const double app = std::real( a[p][p] );
            const double aqq = std::real( a[q][q] );
            const double theta = ( aqq - app ) / ( 2.0 * std::abs( apq ) );
            double t;
            if ( theta >= 0.0 )
                t = 1.0 / ( theta + std::sqrt( 1.0 + theta * theta ) );
            else
                t = -1.0 / ( -theta + std::sqrt( 1.0 + theta * theta ) );
            Givens g;
            g.c = 1.0 / std::sqrt( 1.0 + t * t );
            g.uPq = g.c * t * ( apq / std::abs( apq ) );
            g.p = p;
            g.q = q;

            applyJacobi( a, g );
            rotateVectors( v, g );
        }
    }

    // Final off-diagonal check — convergence gate (fail closed).
    const double off = std::abs( a[0][1] ) + std::abs( a[0][2] ) + std::abs( a[1][2] );
    if ( off > 1e-10 * scale )
        return false;

    // Sort eigenpairs descending by eigenvalue (fixed 3-element selection
    // order — deterministic).
    int order[3] = { 0, 1, 2 };
    if ( std::real( a[order[0]][order[0]] ) < std::real( a[order[1]][order[1]] ) )
        std::swap( order[0], order[1] );
    if ( std::real( a[order[1]][order[1]] ) < std::real( a[order[2]][order[2]] ) )
        std::swap( order[1], order[2] );
    if ( std::real( a[order[0]][order[0]] ) < std::real( a[order[1]][order[1]] ) )
        std::swap( order[0], order[1] );

    for ( int i = 0; i < 3; ++i )
    {
        out->lambda[i] = std::real( a[order[i]][order[i]] );
        for ( int k = 0; k < 3; ++k )
            out->vec[i][k] = v[k][order[i]];
    }
    return true;
}

double hermitianEigen3Residual( double a11, double a22, double a33,
                                std::complex<double> a12, std::complex<double> a13,
                                std::complex<double> a23,
                                const HermitianEigen3 &e )
{
    const std::complex<double> A[3][3] = {
        { { a11, 0.0 }, a12, a13 },
        { std::conj( a12 ), { a22, 0.0 }, a23 },
        { std::conj( a13 ), std::conj( a23 ), { a33, 0.0 } },
    };

    double normA = 0.0;
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            normA += std::norm( A[i][j] );
    normA = std::sqrt( normA );
    const double denom = std::max( 1.0, normA );

    double worst = 0.0;
    for ( int i = 0; i < 3; ++i )
    {
        double residual = 0.0;
        for ( int k = 0; k < 3; ++k )
        {
            std::complex<double> dot = { 0.0, 0.0 };
            for ( int j = 0; j < 3; ++j )
                dot += A[k][j] * e.vec[i][j];
            const std::complex<double> r = dot - e.lambda[i] * e.vec[i][k];
            residual += std::norm( r );
        }
        worst = std::max( worst, std::sqrt( residual ) );
    }
    return worst / denom;
}

} // namespace sicnu::sar
