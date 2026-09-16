// src/processing/algorithms/spectral_hybrid_similarity.h — SID-SAM hybrid
// spectral similarity (Spectral Intelligence 11.0, work package C).
#pragma once

#include <QString>

#include <cstddef>
#include <string>
#include <vector>

/// Hybrid spectral similarity combining the Spectral Angle Mapper (shape,
/// radians) and Spectral Information Divergence (probability distribution,
/// nats). SAM and SID themselves stay single-sourced in
/// SpectralClassification (spectral_angle / spectral_divergence); this file
/// owns only their combination, the bounded normalization, and the
/// wavelength-grid reconciliation guard.
///
/// Two forms are provided (DECISIONS D4):
///
///   - ProductNormalized (default): maps both measures to [0, 1] and
///     multiplies them into a bounded, cross-library comparable similarity
///         sam'  = 1 - 2*theta/pi          (1 = identical direction)
///         sid'  = 1 / (1 + sid)           (1 = identical distribution,
///                                          sid in nats, >= 0)
///         hybrid = sam' * sid'            in [0, 1]; higher = more similar.
///
///   - ClassicTan: the unbounded Chang-style hybrid SID*tan(theta).
///     Kept for literature comparability; explodes as theta -> pi/2, so it
///     is NOT the classification default.
///
/// Scale invariance: SAM and SID are both invariant to positive rescaling of
/// a spectrum, so the hybrid is a brightness/illumination-insensitive
/// similarity by construction (verified by known-answer tests).
namespace SpectralHybridSimilarity
{
    /// Hybrid form (DECISIONS D4).
    enum class Form
    {
        ProductNormalized, ///< bounded sam' * sid' in [0, 1] (default)
        ClassicTan,        ///< unbounded SID * tan(theta)
    };

    const char *formText( Form form );

    /// Parse a form name ("product_normalized" / "classic_tan"); false on an
    /// unknown name (fail-closed — no silent default on typo'd parameters).
    bool formFromText( const std::string &text, Form *out );

    struct SimilarityResult
    {
        double samRadians = 0.0;       ///< [0, pi/2]; NaN when undefined
        double sidNats = 0.0;          ///< [0, inf); NaN when undefined
        double hybrid = 0.0;           ///< form-dependent; NaN when undefined
        bool defined = false;          ///< false when any measure was NaN
    };

    /**
     * Hybrid similarity between two equal-length spectra.
     *
     * Conventions inherited from SpectralClassification: a nodata sentinel
     * or non-finite band value in either spectrum, a negative band value,
     * or a zero spectrum makes every measure NaN and returns true with
     * result->defined == false (the pair is unscorable, not an error).
     * Identical spectra: sam = 0, sid = 0, ProductNormalized = 1,
     * ClassicTan = 0.
     *
     * @param wavelengthGuard optional pair of nm grids (size == bands each).
     *        When both are non-empty they must have equal size and
     *        overlapping ranges — disjoint grids are a typed refusal (they
     *        would make the band-wise comparison meaningless). Empty grids
     *        skip the check (metadata-less rasters stay usable).
     */
    bool similarity( const float *t, const float *r, size_t bands, float nodata,
                     Form form, SimilarityResult *result,
                     QString *errorMessage = nullptr,
                     const std::vector<float> *wavelengthsT = nullptr,
                     const std::vector<float> *wavelengthsR = nullptr );

    /**
     * Hybrid-similarity classification: label each pixel to the reference
     * maximizing the hybrid similarity (ProductNormalized) or minimizing
     * ClassicTan. Mirrors samClassify/sidClassify semantics: label -1 when
     * the pixel is nodata or no reference is scorable; @a scores (optional)
     * carries the best hybrid value per pixel (NaN when undefined).
     */
    bool classify( const float *pixels, size_t count, int bands,
                   const float *refs, int refCount,
                   int *labels, float *scores,
                   Form form, float nodata,
                   QString *errorMessage = nullptr );

} // namespace SpectralHybridSimilarity
