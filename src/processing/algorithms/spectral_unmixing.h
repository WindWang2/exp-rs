// src/processing/algorithms/spectral_unmixing.h — linear spectral unmixing
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

/// Linear spectral unmixing: per-pixel abundance estimation of a set of
/// endmember spectra.
///
/// Method: per pixel, solve the least-squares system E a = x (E = endmember
/// matrix, band-major columns) via normal equations with a small ridge on the
/// diagonal; clip abundances to [0, 1] and renormalize to unit sum. The
/// reconstruction error is the per-pixel RMSE ||x - E a|| / sqrt(bands).
namespace SpectralUnmixing
{
    /// Abundances are pixel-major: abundances[p * nEndmembers + e].
    struct UnmixResult
    {
        std::vector<float> abundances;
        std::vector<float> reconstructionError; // per-pixel RMSE (float)
    };

    /**
     * Unmix @p count pixels against @p nEndmembers endmember spectra.
     *
     * @param pixels      pixel-major spectra: pixels[p * bands + b]
     * @param count       number of pixels
     * @param bands       spectral bands per pixel / endmember
     * @param endmembers  endmember-major: endmembers[e * bands + b]
     * @param nEndmembers number of endmembers (must be >= 1 and <= bands)
     * @param result      [out] abundances + per-pixel reconstruction error
     * @param errorMessage optional error sink
     * @return true on success; false for invalid arguments (count == 0,
     *         bands <= 0, nEndmembers out of [1, bands], null pointers)
     */
    bool unmix( const float *pixels, size_t count, int bands,
                const float *endmembers, int nEndmembers,
                UnmixResult *result, QString *errorMessage = nullptr );

    /**
     * Fully constrained least squares (FCLS): minimize ||x - E a||^2 subject
     * to a >= 0 and sum(a) = 1.
     *
     * Method: Lawson-Hanson active-set NNLS on the penalty-augmented normal
     * equations — G~ = E^T E + rho * 11^T, u~ = E^T x + rho * 1 with
     * rho = 1e6 * mean(diag(E^T E)); the sum-to-one constraint therefore
     * holds to a relative deviation of about 1e-6 (callers report the exact
     * mean |sum(a) - 1| as QA). The sum constraint is a penalty, not a hard
     * KKT constraint: this is a deliberate, documented trade — it keeps one
     * small, well-tested solver for both constraints. NNLS terminates
     * finitely; the grade is tolerance (iteration order is deterministic).
     *
     * Guarded, fail-closed: a zero-norm endmember or a rank-deficient
     * (collinear) endmember set refuses with a named message before any
     * pixel is processed — unlike unmix(), which degrades via a ridge.
     * NaN pixels produce NaN abundances/error (same convention as unmix).
     *
     * @return true on success; false with @p errorMessage for invalid
     *         arguments or the collinearity/zero-norm refusals above.
     */
    bool unmixFcls( const float *pixels, size_t count, int bands,
                    const float *endmembers, int nEndmembers,
                    UnmixResult *result, QString *errorMessage = nullptr );
} // namespace SpectralUnmixing

// ─── D13 · typed unmixing seam (Day 13) ───────────────────────────────────
// The D13 workbench drives unmixing through this seam. It delegates the
// proven penalty-augmented Lawson–Hanson FCLS kernel of the legacy namespace
// above (one solver, no duplication) and adds endmember extraction plus an
// explicit sum-to-one QA metric.
namespace exp_spectral
{
    enum class EndmemberExtractionMethod
    {
        PixelPurityIndex = 0,   ///< PPI: random skewers projection (delegated kernel)
        VertexComponentAnalysis ///< VCA: sequential orthogonal projection onto simplex vertices
    };

    struct UnmixingResult
    {
        std::vector<float> abundances;           ///< Pixel-major abundance fractions [p * nEndmembers + e]
        std::vector<float> reconstructionError;  ///< Per-pixel RMSE: ||y - M·f|| / sqrt(bands)
        double meanSumConstraintViolation = 0.0; ///< QA: mean |sum(f) - 1.0| across pixels
    };

    class SpectralUnmixing
    {
      public:
        /// Extracts @p endmemberCount pure pixel spectra from an image cube.
        /// PPI votes on random skewer projection extremes; VCA walks the
        /// simplex vertices by sequential orthogonal projection. Both return
        /// the original pixel spectra at the selected indices, are seeded and
        /// reproducible (PPI/VCA are deterministic for a given input; @p seed
        /// is reserved for tie-breaking and stays part of the seam contract).
        static bool extractEndmembers( const float *pixels, size_t pixelCount, int bandCount,
                                       int endmemberCount, EndmemberExtractionMethod method,
                                       std::vector<float> *outEndmembers, unsigned int seed = 42 );

        /// Fully Constrained Least Squares unmixing: (1) f_i >= 0 (ANC),
        /// (2) sum(f_i) = 1 (ASC, penalty-augmented NNLS, ~1e-6 relative).
        /// Rank-deficient or zero-norm endmember sets fail closed with a
        /// named message before any pixel is processed.
        static bool unmixFcls( const float *pixels, size_t pixelCount, int bandCount,
                               const float *endmembers, int endmemberCount,
                               UnmixingResult *result, QString *errorMessage = nullptr );
    };
} // namespace exp_spectral
