// spectral_osp.h — Orthogonal Subspace Projection (OSP) target detection
// (Spectral Intelligence 13.0, work package A).
//
// OSP (Harsanyi & Chang 1994, "Hyperspectral image classification and
// dimensionality reduction: an orthogonal subspace projection approach")
// suppresses everything the undesired/background signatures span and projects
// the target onto what remains. With the undesired signature matrix
// U = [s₁ … s_k] (B×k) and target signature d (B values):
//
//   P = I − U(UᵀU)⁻¹Uᵀ          (orthogonal projector onto span(U)⟂)
//   w = P d,                    score(x) = wᵀx
//
// Properties (all asserted in the kernel tests):
//   - every undesired signature scores exactly 0:   wᵀs_c = (Pd)ᵀs_c = dᵀPs_c = 0
//   - the projector is idempotent:                  P² = P
//   - the score is signed and scales with |d|: OSP is NOT brightness-invariant
//     (unlike ACE/CEM) — documented, asserted, and intentional.
// Exact in exact arithmetic: the REALIZED suppression residual scales with
// the conditioning of UᵀU (reported as interferenceCondition) — for a set
// with condition number c, residuals up to ~1e-12·c are expected. The
// kernel tests assert that bound explicitly.
//
// Unlike MF/ACE/CEM/TCIMF, OSP consumes NO background statistics: the
// undesired subspace is an input, so the detector needs one scoring pass and
// has no under-sampling refusal. The cost moves to the caller, who must know
// the undesired signatures (inline, spectral-table artifact or library
// materials through the shared reference seam).
//
// Typed refusals (buildFilter returns false with a named reason):
//   - undesired spectrum non-finite, zero, or wrong band count;
//   - UᵀU singular: the undesired signatures are linearly dependent
//     (duplicate, collinear or zero columns);
//   - the projected target retains less than 1e-12 of the target energy
//     (|Pd|² <= 1e-12·|d|²): the target is numerically inside the undesired
//     subspace, so the detector is meaningless — refused instead of emitting
//     an amplifying noise filter.
// The conditioning of UᵀU is reported as a diagnostic
// (interferenceCondition, the true λmax/λmin condition number via
// sicnu::primitives::conditionNumber) so near-collinear signature sets are
// visible before they bite.
#pragma once

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SpectralOsp
{
    /// Precomputed OSP filter (the projected target direction).
    struct Filter
    {
        std::vector<double> weight; ///< w = P d; every undesired signature scores 0
    };

    /// Builds the OSP filter for @a target (bands values) against @a interference
    /// (k spectra of bands values each; k >= 1 — an empty matrix is a refusal,
    /// unlike TCIMF where it degenerates to CEM, because a projector onto
    /// span(∅)⟂ = ℝᴮ would reduce OSP to a bare dot product with no suppression
    /// semantics). Returns false on any typed refusal listed in the header;
    /// @a errorMessage (when provided) carries the named reason. When
    /// @a interferenceCondition is provided it receives the condition number
    /// λmax/λmin of the interference Gram matrix UᵀU (k×k), or -1 when it is
    /// not computable — the near-collinear diagnostic that complements the
    /// hard singularity refusal.
    bool buildFilter( const float *target, int bands,
                      const std::vector<std::vector<float>> &interference,
                      Filter *out, QString *errorMessage = nullptr,
                      double *interferenceCondition = nullptr );

    /// OSP score wᵀx (signed, scales with |d|). NaN when @a x has a non-finite
    /// band. @a scratch must have capacity >= @a bands.
    float ospScore( const float *x, const Filter &filter, int bands,
                    std::vector<double> *scratch );

} // namespace SpectralOsp
