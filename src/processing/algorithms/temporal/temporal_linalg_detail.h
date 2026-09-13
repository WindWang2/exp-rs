// src/processing/algorithms/temporal/temporal_linalg_detail.h
// Shared small dense solver for the temporal kernels (detail namespace, not
// public API). Extracted from temporal_fit.cpp so the joint harmonic+trend
// segmentation (temporal_change.h) reuses the same elimination instead of
// growing a second copy.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace sicnu::temporal::detail
{

/// Solves the small dense symmetric system A·x = b by Gaussian elimination
/// with partial pivoting (row-major @a a, n×n). Returns false on a singular
/// system (pivot < 1e-12).
inline bool solveSmallDense( std::vector<double> a, std::vector<double> b, int n,
                             std::vector<double> *x )
{
  for ( int col = 0; col < n; ++col )
  {
    int pivot = col;
    double best = std::fabs( a[static_cast<size_t>( col ) * n + col] );
    for ( int row = col + 1; row < n; ++row )
    {
      const double v = std::fabs( a[static_cast<size_t>( row ) * n + col] );
      if ( v > best )
      {
        best = v;
        pivot = row;
      }
    }
    if ( best < 1e-12 )
      return false;
    if ( pivot != col )
    {
      for ( int k = col; k < n; ++k )
        std::swap( a[static_cast<size_t>( col ) * n + k],
                   a[static_cast<size_t>( pivot ) * n + k] );
      std::swap( b[col], b[pivot] );
    }
    const double d = a[static_cast<size_t>( col ) * n + col];
    for ( int row = col + 1; row < n; ++row )
    {
      const double factor = a[static_cast<size_t>( row ) * n + col] / d;
      if ( factor == 0.0 )
        continue;
      for ( int k = col; k < n; ++k )
        a[static_cast<size_t>( row ) * n + k] -= factor * a[static_cast<size_t>( col ) * n + k];
      b[row] -= factor * b[col];
    }
  }
  x->assign( n, 0.0 );
  for ( int row = n - 1; row >= 0; --row )
  {
    double sum = b[row];
    for ( int k = row + 1; k < n; ++k )
      sum -= a[static_cast<size_t>( row ) * n + k] * ( *x )[k];
    ( *x )[row] = sum / a[static_cast<size_t>( row ) * n + row];
  }
  return true;
}

} // namespace sicnu::temporal::detail
