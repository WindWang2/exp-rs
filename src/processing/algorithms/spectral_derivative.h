// spectral_derivative.h — per-pixel spectral derivatives along the wavelength
// axis (Foundation 5.0, Milestone B3).
//
// Contract:
//   * Band vectors are per-pixel, band-ordered; @a wavelengths (nm) must be
//     strictly ascending and match the band order (duplicate or descending
//     axes are ill-defined and refused by the operator, mirroring continuum
//     removal's wavelength policy).
//   * First derivative: out[i] = (v[i+1] − v[i]) / (λ[i+1] − λ[i]) over the
//     B−1 band pairs; output band i carries the midpoint wavelength.
//   * Second derivative: the first derivative applied twice along the
//     successive midpoint axes (B−2 output bands) — the honest finite-
//     difference form for irregularly spaced bands, not a central-difference
//     approximation on the original grid.
//   * NaN in any participating value propagates to every output band that
//     touches it (never silently drops the pixel's other derivatives).
#pragma once

#include <cstddef>

namespace SpectralDerivative
{

/// @a values has @a bands samples; @a wavelengths (nm) has @a bands strictly
/// ascending entries; @a out receives @a bands − 1 derivatives.
void firstDerivative( const float *values, const double *wavelengths, int bands, float *out );

/// Applies the first derivative twice (successive midpoint axes); @a out
/// receives @a bands − 2 derivatives. Requires @a bands >= 3.
void secondDerivative( const float *values, const double *wavelengths, int bands, float *out );

/// Midpoint wavelength axis after one first-derivative pass.
void midpointAxis( const double *wavelengths, int bands, double *outMidpoints );

} // namespace SpectralDerivative
