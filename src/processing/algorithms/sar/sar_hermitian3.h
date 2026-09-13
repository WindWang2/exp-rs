// sar_hermitian3.h — deterministic eigen-decomposition of 3×3 Hermitian
// matrices (Advanced SAR / PolSAR / InSAR 10.0, package B).
//
// PolSAR coherency (T3) and covariance (C3) matrices are 3×3 Hermitian and
// positive semidefinite by construction (E[k·k^H] for any complex ensemble
// vector k). Cloude-Pottier H/A/α needs the full eigen-system with a FIXED
// ordering and a deterministic iteration — the cyclic Jacobi sweep below
// visits (p,q) in the fixed order (0,1),(0,2),(1,2) with a relative
// convergence threshold, so identical input matrices yield bit-identical
// eigen-systems on every run and platform (Determinism Grade: bit-exact).
//
// No third-party linear algebra is linked: OpenCV's cv::eigen covers real
// symmetric matrices only, and adding Eigen is outside this track's
// dependency budget (DECISIONS D-004).
//
// Storage contract: the Hermitian matrix is passed as its six independent
// values — real diagonal a11, a22, a33 and the complex upper triangle
// a12, a13, a23 (a_ji = conj(a_ij) is implied, never stored).
#pragma once

#include <complex>

namespace sicnu::sar
{

struct HermitianEigen3
{
    /// Eigenvalues, strictly ordered descending λ1 ≥ λ2 ≥ λ3.
    double lambda[3] = { 0.0, 0.0, 0.0 };
    /// Unit eigenvectors: vec[i] is the 3-component complex eigenvector for
    /// lambda[i]. Orthonormal up to solver tolerance; phase fixed by the
    /// rotation accumulation (first significant component's phase is NOT
    /// normalized — consumers must not rely on eigenvector phase).
    std::complex<double> vec[3][3] = {};
};

/// Cyclic Jacobi eigen-decomposition of the Hermitian matrix
/// [[a11, a12, a13], [conj(a12), a22, a23], [conj(a13), conj(a23), a33]].
///
/// @return false when the sweep fails to converge within the iteration cap
/// (the only realistic cause is a non-finite input — callers refuse rather
/// than emit a wrong decomposition). The zero matrix converges immediately
/// to (0,0,0) with the identity eigenvectors.
bool hermitianEigen3( double a11, double a22, double a33,
                      std::complex<double> a12, std::complex<double> a13,
                      std::complex<double> a23,
                      HermitianEigen3 *out );

/// Reconstructs the max residual ‖A·v_i − λ_i·v_i‖₂ / max(1, ‖A‖) — the
/// self-check used by the known-answer tests and available to operators for
/// an internal sanity assertion.
double hermitianEigen3Residual( double a11, double a22, double a33,
                                std::complex<double> a12, std::complex<double> a13,
                                std::complex<double> a23,
                                const HermitianEigen3 &e );

} // namespace sicnu::sar
