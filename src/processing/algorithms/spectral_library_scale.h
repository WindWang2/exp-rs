// spectral_library_scale.h — library-scale matching (Spectral Intelligence
// 12.0, work package C).
//
// The per-query cost of SpectralLibrary::matchSpectrum is O(N · (R + B)) for
// N entries: EVERY entry whose grid differs from the query re-resamples the
// query spectrum, every entry is fully scored (SAM + SID), and the whole
// result list is globally sorted. For pixel-wise library search over a
// thousand-entry library the re-resampling dominates and the full ranked
// list stays in memory unused beyond the first few ranks.
//
// This layer keeps the brute-force semantics (same comparable entries, same
// scores, same order) while removing both costs:
//
//   1. Grid-bucket index (the "wavelength index"): entries are grouped ONCE
//      by (band count, wavelength+FWHM grid bytes). A query is resampled
//      once per DISTINCT grid (G resamples, G << N) instead of once per
//      entry; entries whose band count equals the query's are scored raw
//      exactly as the brute-force path does. Bucket members share one grid,
//      so per-entry resampling decisions and NaN-skip outcomes are identical.
//   2. Prescreen + top-K ranking: for top-K queries the prescreen computes
//      the SAME clamped cosine (and its acos) the SAM kernel would — in the
//      same accumulation order — so candidate ranking and tie handling
//      (angle, then original entry order) are bit-identical to brute force;
//      the expensive SID evaluation runs only on the kept entries. Equal
//      angles preserve library order via the stable (angle, index) key.
#pragma once

#include "spectral_library.h"

#include <cstddef>
#include <vector>

namespace SpectralLibraryScale
{
    /// Options for the scaled matching path. Defaults reproduce the
    /// brute-force semantics (all entries, ranked).
    struct Options
    {
        int topK = 0;          ///< keep only the best K (0 = all)
        bool prescreen = true; ///< cosine/acos prescreen for top-K > 0
    };

    /// Counters from the last match call — the machine-independent scale
    /// evidence (resample operations avoided, entries fully scored).
    struct Stats
    {
        int resamplesPerformed = 0; ///< distinct-grid resamples actually run
        int resamplesAvoided = 0;   ///< brute-force would have run this many more
        int entriesScored = 0;      ///< full SAM+SID evaluations
        int entriesPrescreened = 0; ///< entries ranked by cosine only
    };

    /// Opaque-ish precomputed index: grid buckets + per-entry norms. Build
    /// once per (library, expected query grid) pair and reuse for any number
    /// of queries; index build is O(N·B) with no resampling.
    class MatchIndex
    {
    public:
        MatchIndex() = default;

        /// Builds the grid buckets for @p library. Bucketing is by EXACT
        /// (band count, wavelength grid bytes, FWHM bytes) equality — a linear
        /// scan over the bucket list, O(N·G·B) once per library (G = distinct
        /// grids, tiny for real libraries). Returns false when the library is
        /// empty (nothing to index). An entry without a wavelength grid forms
        /// part of the raw-scorable (bands, no grid) bucket — the entries the
        /// brute-force path would never resample to.
        static bool build( const SpectralLibrary::Library &library, MatchIndex *out );

        const SpectralLibrary::Library *library() const { return m_library; }
        Stats lastStats() const { return m_lastStats; }

        /// Ranked matches for one query spectrum; identical semantics to
        /// SpectralLibrary::matchSpectrum(spectrum, spectrumWavelengths,
        /// library, nodata) — same entries scored, same scores, same order —
        /// subject to @p options (top-K truncation after ranking).
        std::vector<SpectralLibrary::MatchScore> match(
            const std::vector<float> &spectrum,
            const std::vector<float> &spectrumWavelengths,
            const Options &options = {},
            float nodata = SpectralClassification::kNoDataSentinel ) const;

    private:
        struct Bucket
        {
            int bands = 0;
            bool hasGrid = false;           ///< entries carry a usable wavelength grid
            std::vector<float> wavelengths; ///< shared grid of the bucket (may be empty)
            std::vector<float> fwhm;        ///< shared FWHM (may be empty)
            std::vector<int> entryIndices;
            std::vector<double> norms2;     ///< Σ v² over finite entry values
        };
        const SpectralLibrary::Library *m_library = nullptr;
        std::vector<Bucket> m_buckets;
        mutable Stats m_lastStats;

        std::vector<float> resampleQueryToBucket( const std::vector<float> &spectrum,
                                                  const std::vector<float> &spectrumWavelengths,
                                                  const Bucket &bucket, bool *ok ) const;
    };

} // namespace SpectralLibraryScale
