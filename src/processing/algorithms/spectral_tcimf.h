// spectral_tcimf.h — Target-Constrained Interference-Minimized Filter (TCIMF)
// target detection (Spectral Intelligence 13.0, work package A).
//
// TCIMF (Manolakis, Siracusa & Shaw 2001; Manolakis et al. 2014,
// "Hyperspectral unmixing and detection: an ill-posed problem", TCIMF form) is
// the constrained companion of CEM for scenes with KNOWN undesired signatures
// (interference). It minimizes the filter output energy over the background
// subject to an exact distortionless constraint on the target AND exact null
// constraints on every interference signature:
//
//   min_w  wᵀRw   s.t.  wᵀt = 1,  Sᵀw = 0        (S = interference matrix, B×k)
//   ⇒  w = R⁻¹[t − S(SᵀR⁻¹S)⁻¹SᵀR⁻¹t] / tᵀR⁻¹[t − S(SᵀR⁻¹S)⁻¹SᵀR⁻¹t]
//
// (Lagrange form; the R⁻¹ applies to the whole combination t − S z, which is
// what makes both constraints hold exactly.)
//
// R is the sample CORRELATION (second-moment) matrix — the same matrix and the
// same scaled diagonal loading strategy as CEM (spectral_cem.h), so the two
// detectors share the streaming accumulator, the min-sample floor and the
// valid-pixel predicate. With an empty interference matrix the closed form
// degenerates to the CEM filter w = R⁻¹t/(tᵀR⁻¹t) exactly.
//
// Constraints (exact by construction, asserted in the kernel tests):
//   wᵀt  = 1            (distortionless: the target scores exactly 1)
//   Sᵀw  = 0            (every interference spectrum scores exactly 0)
// Exact in exact arithmetic: the REALIZED null-constraint residual scales
// with the conditioning of the Gram matrix SᵀR'⁻¹S (reported as
// interferenceCondition) — for a set with condition number c, residuals up
// to ~1e-12·c are expected from the cancellation in t − S z. The kernel
// tests assert that bound explicitly.
//
// Typed refusals (buildFilter returns false with a named reason, never a
// plausible-looking filter):
//   - interference spectrum non-finite, zero, or wrong band count;
//   - SᵀR'⁻¹S singular: the interference signatures are linearly dependent
//     under the background metric (duplicate, collinear or zero columns);
//   - the target lies inside the interference span: the distortionless
//     denominator (the Schur complement tᵀR⁻¹t − bᵀA⁻¹b ∈ [0, tᵀR⁻¹t]) falls
//     to 1e-12 of the unconstrained value or below, so the constraint wᵀt = 1
//     is unsatisfiable — refused instead of emitting a huge filter;
//   - singular loaded correlation, degenerate target, invalid loading —
//     the same structural refusals as CEM.
//
// Under-sampling is refused by the streaming driver BEFORE scoring using the
// CEM floor (minSamplesRequired): the same second-moment estimator needs the
// same headroom, and loading is the documented escape hatch.
//
// All scoring is per-pixel against the precomputed filter — no per-pixel heap
// allocation (scratch buffer passed in by the streaming operator).
#pragma once

#include "processing/algorithms/spectral_cem.h"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SpectralTcimf
{
    /// Streaming background accumulator: re-exported from SpectralCem so the
    /// driver accumulates the correlation once and can build either detector.
    using SpectralCem::CorrelationStats;
    using SpectralCem::accumulateCorrelation;
    using SpectralCem::finalizeCorrelation;
    using SpectralCem::minSamplesRequired;

    /// Precomputed TCIMF filter. Built once per scene after the background
    /// correlation is finalized (and loaded/inverted).
    struct Filter
    {
        std::vector<double> weight; ///< w with the exact constraints wᵀt = 1, Sᵀw = 0
    };

    /// Builds the TCIMF filter for @a target (bands values) against the
    /// finalized correlation @a correlation (bands², row-major) with a scaled
    /// diagonal loading of @a loading * (tr(R)/B). @a interference holds k
    /// spectra of @a bands values each (k may be 0 → the filter is exactly the
    /// CEM filter). Returns false on any typed refusal listed in the header;
    /// @a errorMessage (when provided) carries the named reason.
    /// When @a interferenceCondition is provided it receives the condition
    /// number λmax/λmin of the interference Gram matrix SᵀR'⁻¹S (k×k), or -1
    /// when k == 0 or the number is not computable — the near-collinear
    /// diagnostic that complements the hard singularity refusal.
    bool buildFilter( const float *target, int bands,
                      const std::vector<std::vector<float>> &interference,
                      const std::vector<double> &correlation,
                      double loading, Filter *out,
                      QString *errorMessage = nullptr,
                      double *interferenceCondition = nullptr );

    /// TCIMF score wᵀx (the target scores exactly 1, every interference
    /// spectrum exactly 0). NaN when @a x has a non-finite band.
    /// @a scratch must have capacity >= @a bands.
    float tcimfScore( const float *x, const Filter &filter, int bands,
                      std::vector<double> *scratch );

} // namespace SpectralTcimf
