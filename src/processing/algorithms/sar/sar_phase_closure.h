// sar_phase_closure.h — interferometric phase-closure QA (Advanced InSAR
// 11.0, package E).
//
// THE INVARIANT (independent of any geophysical model): for three
// interferograms of a common SLC stack, I_ab = a·conj b, I_bc = b·conj c,
// I_ca = c·conj a, formed on ONE consistent same-grid sampling,
//   φ_clo = wrap( arg(I_ab · I_bc · I_ca) )
// is IDENTICALLY zero (mod 2π) wherever all three products are valid —
// the product reduces to the positive real |a||b||c|. Per-scene phase
// (deformation, atmosphere, topography) is common-mode and cancels: the
// closure does NOT detect displacement. What it DOES detect is a broken
// stack: the three interferograms not formed from consistent same-grid
// samples (misaligned resamplings, mixed products, processing bugs) —
// exactly what a pair network must validate before inversion.
//
// INPUT CONTRACT: the three COMPLEX INTERFEROGRAM planes (the products),
// not the SLCs — three SLC samples at one pixel trivially satisfy the
// identity by algebra and could never expose an inconsistent stack.
//
// Numeric domain: closure in radians wrapped to (−π, π]; NaN wherever any
// operand is invalid (counted, never treated as zero).
#pragma once

#include <complex>
#include <functional>

namespace sicnu::sar
{

/// Pointwise closure of three interferogram products; NaN when any
/// operand is non-finite or zero (interferogramPhase semantics).
double phaseClosureRad( std::complex<double> ifgAB, std::complex<double> ifgBC,
                        std::complex<double> ifgCA );

struct PhaseClosureStats
{
    long long evaluated = 0;    ///< pixels visited
    long long validCount = 0;   ///< pixels with a finite closure
    long long invalidCount = 0; ///< pixels with at least one invalid operand
    double maxAbsClosureRad = 0.0;  ///< max |φ_clo| over valid pixels
    double rmsClosureRad = 0.0;     ///< RMS of φ_clo over valid pixels
};

/// Plane closure over three same-grid complex interferogram planes (w*h,
/// row-major): fills @a closureOut (may be null; radians, NaN at invalid
/// pixels) and the statistics. Cancellation is cooperative (probe every 64
/// rows).
void phaseClosurePlane( const std::complex<float> *ifgAB, const std::complex<float> *ifgBC,
                        const std::complex<float> *ifgCA, int w, int h,
                        double *closureOut, PhaseClosureStats *stats,
                        const std::function<void()> &cancelProbe = {} );

} // namespace sicnu::sar
