// spectral_cem.h — Constrained Energy Minimization (CEM) target detection
// (Spectral Intelligence 12.0, work package A).
//
// CEM (Ren & Chang 2000) is the third standard target detector alongside the
// matched filter and ACE (SpectralDetection, Foundation 5.0 Milestone C).
// It finds the finite-impulse filter that minimizes the filter output energy
// over the scene subject to an exact distortionless constraint on the target:
//
//   min_w  (1/N) Σᵢ (wᵀxᵢ)²  =  wᵀRw    s.t.  wᵀt = 1
//   ⇒  w = R⁻¹t / (tᵀR⁻¹t),   score(x) = wᵀx
//
// R = (1/N) Σᵢ xᵢxᵢᵀ is the sample CORRELATION (second-moment) matrix —
// deliberately NOT the mean-centered covariance used by MF/ACE: the
// constraint design makes CEM tolerant to multiplicative brightness
// scaling, the classic companion to the signed MF and the squared ACE.
// A pixel whose spectrum equals the target scores exactly 1.
//
// Numerical strategy mirrors SpectralLocalRx (ADR 0163): the correlation
// receives a scaled diagonal loading
//     R' = R + loading * (tr(R)/B) * I
// — proportional to the data variance so the bias stays predictable — and
// the streaming driver must refuse scenes with fewer valid background
// samples than minSamplesRequired() BEFORE scoring, instead of letting the
// tiny ridge mask a rank-deficient estimate (fail-closed, not thrash).
//
// All scoring is per-pixel against the precomputed filter — no per-pixel
// heap allocation (scratch buffer passed in by the streaming operator).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SpectralCem
{
    /// Streaming second-moment accumulator for the CEM background.
    /// correlation.size() == bands*bands (row-major); after finalizeCorrelation()
    /// it holds R = (sum of raw outer products) / count. The valid-pixel
    /// predicate is identical to SpectralAnomaly::BackgroundStats: a pixel is
    /// excluded as a whole when any band is non-finite or — for bands with a
    /// declared NoData value — equal to that NoData.
    struct CorrelationStats
    {
        std::vector<double> correlation; ///< bands² row-major (upper-tri sum before finalize)
        int bands = 0;                   ///< band count inferred on first accumulation
        size_t count = 0;                ///< valid pixels accumulated in the current pass
    };

    /// Accumulate a sum-of-raw-outer-products pass. Resets @a stats on first
    /// call (band count inferred). Layout: pixels[p*bands+b].
    void accumulateCorrelation( const float *pixels, size_t count, int bands,
                                CorrelationStats *stats, bool skipNonFinite = false,
                                const float *noDataBands = nullptr,
                                const uint8_t *hasNoDataBands = nullptr );

    /// Divide the accumulated sum by count → correlation matrix R. Keeps count.
    void finalizeCorrelation( CorrelationStats *stats );

    /// Precomputed CEM filter. Built once per scene after the background
    /// correlation is finalized (and loaded/inverted).
    struct Filter
    {
        std::vector<double> weight; ///< w with the exact constraint wᵀt = 1
    };

    /// Operator-side fail-closed floor: the minimum number of valid background
    /// samples a scene must provide before CEM may score. With no loading the
    /// B×B second-moment estimate needs the same headroom as local RX Full
    /// (2B+2); an explicitly enabled scaled loading is the documented escape
    /// hatch for tighter scenes (B+1, still full-rank in general position).
    int minSamplesRequired( int bands, bool loadingEnabled );

    /// Builds the CEM filter for @a target (bands values) against the finalized
    /// correlation @a correlation (bands², row-major) with a scaled diagonal
    /// loading of @a loading * (tr(R)/B). Returns false on structurally invalid
    /// calls (null buffers, size mismatch, negative/non-finite loading), a
    /// non-finite or non-positive-definite background, a degenerate target
    /// (non-finite or zero norm under R⁻¹), or a singular loaded matrix.
    bool buildFilter( const float *target, int bands,
                      const std::vector<double> &correlation,
                      double loading, Filter *out );

    /// CEM score wᵀx (the target scores exactly 1). NaN when @a x has a
    /// non-finite band. @a scratch must have capacity >= @a bands.
    float cemScore( const float *x, const Filter &filter, int bands,
                    std::vector<double> *scratch );

} // namespace SpectralCem
