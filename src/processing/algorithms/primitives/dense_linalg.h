// dense_linalg.h — small dense linear algebra for per-pixel scientific
// kernels (Foundation 7.0 primitive consolidation).
//
// Single owner of the Gauss-Jordan inverse that previously existed as two
// identical copies (spectral_anomaly.cpp invertMatrix, spectral_unmixing.cpp
// invertMatrixInPlace). The 1e-12 pivot threshold is part of the contract:
// every former copy used it, and the RX/unmixing tolerance tests are pinned
// against it.
//
// NOTE: temporal_fit's solveSmall is deliberately NOT folded in here — it
// is Gaussian elimination + back-substitution (solve form, different numeric
// path and O(n²) storage), not an inverse. Keeping them separate preserves
// the SG-fit numerics the temporal tests pin.
#pragma once

#include <vector>

namespace sicnu::primitives
{

/// Inverts the n×n row-major matrix @a m in place (Gauss-Jordan with partial
/// pivoting). Returns false when the matrix is singular (best pivot
/// |value| < 1e-12); @a m is then left in an unspecified state.
bool invertDenseMatrixInPlace( std::vector<double> &m, int n );

/// Convenience form: @a m is preserved, the inverse is written to
/// @a inverse (row-major, n×n). Same singular contract.
bool invertDenseMatrix( const std::vector<double> &m, int n, std::vector<double> *inverse );

} // namespace sicnu::primitives
