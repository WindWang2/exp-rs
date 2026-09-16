// src/processing/algorithms/spectral_sparse_unmixing.h — sparse (L1, non-
// negative) spectral unmixing (Spectral Intelligence 11.0, work package B).
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

/// Sparse linear spectral unmixing: per-pixel abundance estimation against
/// an (optionally overcomplete) endmember dictionary under an L1 sparsity
/// constraint and a non-negativity constraint, with the same sum-to-one
/// PENALTY convention as the master FCLS solver (rho * 11^T augmentation —
/// a documented penalty, not a hard KKT constraint; callers report the
/// achieved |sum(a) - 1| as QA):
///
///   min_a  0.5 * || x - E a ||^2  +  lambda * ||a||_1  +  rho/2 * (1^T a - 1)^2
///   s.t.   a >= 0
///
/// Solver: FISTA with the exact prox of the (separable) nonsmooth part —
/// the L1 soft-threshold composed with the non-negativity clamp,
/// prox(v) = max(v - lambda * step, 0) — deterministic, fixed-iteration,
/// no randomization. The Lipschitz constant is estimated by a bounded,
/// deterministic power iteration on the (augmented) Gram matrix.
///
/// Interpretability guards (fail-closed, mirroring the FCLS refusals):
///   - zero-norm endmembers refuse;
///   - endmember pairs whose spectral angle is below
///     collinearAngleDegrees refuse — an L1 solution over a dictionary of
///     near-duplicate atoms is not uniquely interpretable, and reporting
///     one arbitrary split of the mass would dress ambiguity up as result.
///
/// Unlike unmix()/unmixFcls(), nEndmembers MAY exceed bands (the sparse
/// setting); the Gram materialization is capped defensively.
namespace SpectralSparseUnmixing
{
    struct Config
    {
        double lambda = 0.01;            ///< L1 weight (>= 0); 0 reduces to NNLS
        double sumToOnePenalty = 0.0;    ///< rho (>= 0); 0 = free abundances
        int maxIterations = 2000;        ///< per-pixel FISTA iteration cap
        double tolerance = 1e-8;         ///< relative iterate change stopping rule
        double collinearAngleDegrees = 0.5; ///< pairwise SAM refusal threshold
    };

    struct SparseUnmixResult
    {
        std::vector<float> abundances;          ///< p * nEndmembers, >= 0
        std::vector<float> reconstructionError; ///< per-pixel RMSE ||x - Ea||/sqrt(B)
        std::vector<float> abundanceSums;       ///< per-pixel sum(a) (sum-to-one QA)
        std::vector<int32_t> iterations;        ///< per-pixel iterations used
        std::vector<uint8_t> converged;         ///< per-pixel tolerance reached
    };

    struct Dictionary
    {
        // Precomputed once per call / operator run; reusable across pixels.
        std::vector<double> gram;       ///< G = E^T E (+ rho * 11^T when rho > 0), n*n
        std::vector<double> endmembers; ///< E, band-major columns (n * bands)
        std::vector<double> etUnit;     ///< E^T 1 (n) — sum-to-one penalty RHS
        int bands = 0;
        int nEndmembers = 0;
        double lipschitz = 0.0;         ///< power-iteration estimate of lambda_max(G)
    };

    /**
     * Validate the dictionary and precompute the Gram augmentation + Lipschitz
     * estimate. Fail-closed: null/empty inputs, zero-norm endmembers,
     * endmember pairs closer than the collinearity threshold, or a dictionary
     * above the 2048-atom defensive cap refuse with a named message.
     */
    bool buildDictionary( const float *endmembers, int bands, int nEndmembers,
                          const Config &config, Dictionary *out,
                          QString *errorMessage = nullptr );

    /**
     * Solve one pixel against a prepared dictionary. NaN pixels produce NaN
     * abundances/error (same convention as unmix()/unmixFcls()). Non-
     * convergence within maxIterations is NOT an error: the result is
     * returned with converged=false and the iteration count — the caller
     * surfaces that honestly in diagnostics.
     */
    bool solveSparsePixel( const float *pixel, const Dictionary &dictionary,
                           const Config &config,
                           std::vector<double> *abundances,
                           int32_t *iterationsUsed, bool *converged,
                           QString *errorMessage = nullptr );

    /**
     * Batch driver over @p count pixel-major spectra.
     * @return false only for structurally invalid arguments or dictionary
     *         refusals (see buildDictionary); per-pixel non-convergence is
     *         reported in the result, not as an error.
     */
    bool unmixSparse( const float *pixels, size_t count, int bands,
                      const float *endmembers, int nEndmembers,
                      const Config &config, SparseUnmixResult *result,
                      QString *errorMessage = nullptr );

} // namespace SpectralSparseUnmixing
