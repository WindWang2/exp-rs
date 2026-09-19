// spectral_library_scale.cpp — see spectral_library_scale.h for contracts.

#include "spectral_library_scale.h"

#include "processing/algorithms/spectral_classification.h"
#include "processing/algorithms/spectral_resampling.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpectralLibraryScale
{
namespace
{
// The brute-force validity predicate of SpectralClassification::spectralAngle:
// a single sentinel/non-finite value on either side makes the angle undefined.
bool samUsable( const float *a, const float *b, int bands, float nodata )
{
    for ( int i = 0; i < bands; ++i )
    {
        if ( a[i] == nodata || b[i] == nodata ||
             !std::isfinite( a[i] ) || !std::isfinite( b[i] ) )
            return false;
    }
    return true;
}
} // namespace

bool MatchIndex::build( const SpectralLibrary::Library &library, MatchIndex *out )
{
    if ( !out || library.entries.empty() )
        return false;

    out->m_library = &library;
    out->m_buckets.clear();
    out->m_lastStats = Stats{};

    // Buckets keyed by EXACT (band count, grid bytes) — a linear scan over the
    // (small) bucket list with full vector equality, so correctness never
    // depends on a hash. Entries without a wavelength grid can never be
    // resampled-to by the brute-force path; they share one raw-scorable
    // (bands, empty grid) bucket, which reproduces the brute-force skip
    // semantics exactly.
    const int entryCount = static_cast<int>( library.entries.size() );
    for ( int i = 0; i < entryCount; ++i )
    {
        const SpectralLibrary::Entry &entry = library.entries[static_cast<size_t>( i )];
        const int bands = static_cast<int>( entry.spectrum.size() );
        const bool hasGrid = entry.wavelengths.size() == entry.spectrum.size();
        const bool hasFwhm = !entry.fwhm.empty() && entry.fwhm.size() == entry.spectrum.size();

        std::size_t bucketIdx = 0;
        while ( bucketIdx < out->m_buckets.size() )
        {
            const Bucket &existing = out->m_buckets[bucketIdx];
            if ( existing.bands == bands && existing.wavelengths == entry.wavelengths &&
                 existing.fwhm == entry.fwhm )
                break;
            ++bucketIdx;
        }
        if ( bucketIdx == out->m_buckets.size() )
        {
            Bucket bucket;
            bucket.bands = bands;
            bucket.hasGrid = hasGrid;
            if ( hasGrid )
                bucket.wavelengths = entry.wavelengths;
            if ( hasFwhm )
                bucket.fwhm = entry.fwhm;
            out->m_buckets.push_back( std::move( bucket ) );
        }
        Bucket &bucket = out->m_buckets[bucketIdx];

        double norm2 = 0.0;
        for ( const float v : entry.spectrum )
        {
            if ( std::isfinite( v ) )
                norm2 += static_cast<double>( v ) * static_cast<double>( v );
        }
        bucket.entryIndices.push_back( i );
        bucket.norms2.push_back( norm2 );
    }
    return true;
}

std::vector<float> MatchIndex::resampleQueryToBucket(
    const std::vector<float> &spectrum, const std::vector<float> &spectrumWavelengths,
    const Bucket &bucket, bool *ok ) const
{
    *ok = false;
    // Same preconditions as the brute-force path: the query needs its grid,
    // the entry (bucket) needs one sized to its spectrum.
    if ( spectrumWavelengths.size() != spectrum.size() ||
         bucket.wavelengths.size() != static_cast<size_t>( bucket.bands ) )
        return {};

    std::vector<float> resampled( static_cast<size_t>( bucket.bands ), 0.0f );
    const bool gaussian = !bucket.fwhm.empty() &&
                          bucket.fwhm.size() == static_cast<size_t>( bucket.bands );
    const bool okResampling =
        gaussian ? SpectralResampling::resampleSpectrumGaussian(
                       spectrum.data(), spectrumWavelengths.data(),
                       static_cast<int>( spectrum.size() ), bucket.wavelengths.data(),
                       bucket.fwhm.data(), bucket.bands, resampled.data() )
                 : SpectralResampling::resampleSpectrum(
                       spectrum.data(), spectrumWavelengths.data(),
                       static_cast<int>( spectrum.size() ), bucket.wavelengths.data(),
                       bucket.bands, resampled.data() );
    if ( !okResampling )
        return {};
    // Out-of-source-range target bands produce NaN — the brute-force path
    // treats such an entry as incomparable (skipped, absent from the output).
    for ( const float v : resampled )
    {
        if ( std::isnan( v ) )
            return {};
    }
    *ok = true;
    return resampled;
}

std::vector<SpectralLibrary::MatchScore> MatchIndex::match(
    const std::vector<float> &spectrum, const std::vector<float> &spectrumWavelengths,
    const Options &options, float nodata ) const
{
    std::vector<SpectralLibrary::MatchScore> scores;
    m_lastStats = Stats{};
    if ( !m_library || spectrum.empty() )
        return scores;

    const int queryBands = static_cast<int>( spectrum.size() );
    const bool fullScoring = ( options.topK <= 0 ) || !options.prescreen;

    // (angleDegrees, candidate) pairs for the prescreened path; the degree
    // conversion happens BEFORE ranking so the sort key is the exact value
    // the brute-force path sorts on.
    struct Candidate
    {
        int entryIndex = -1;
        std::size_t bucketIndex = 0;
        bool rawScored = false;
        double angleDegrees = 0.0; ///< NaN when the SAM kernel would return NaN
    };
    std::vector<Candidate> candidates;

    for ( std::size_t b = 0; b < m_buckets.size(); ++b )
    {
        const Bucket &bucket = m_buckets[b];
        const bool rawScored = ( bucket.bands == queryBands );
        std::vector<float> queryEff;
        if ( rawScored )
        {
            queryEff = spectrum;
        }
        else
        {
            bool ok = false;
            queryEff = resampleQueryToBucket( spectrum, spectrumWavelengths, bucket, &ok );
            if ( !ok )
            {
                // The whole bucket is incomparable — exactly like the
                // brute-force path, which would attempt (and fail) one
                // resample per entry.
                m_lastStats.resamplesAvoided +=
                    static_cast<int>( bucket.entryIndices.size() );
                continue;
            }
            ++m_lastStats.resamplesPerformed;
            m_lastStats.resamplesAvoided += static_cast<int>( bucket.entryIndices.size() ) - 1;
        }

        for ( std::size_t m = 0; m < bucket.entryIndices.size(); ++m )
        {
            const SpectralLibrary::Entry &entry =
                m_library->entries[static_cast<size_t>( bucket.entryIndices[m] )];
            const float *entryData = entry.spectrum.data();

            if ( fullScoring )
            {
                SpectralLibrary::MatchScore score;
                score.entryIndex = bucket.entryIndices[m];
                score.name = entry.name;
                score.material = entry.material;
                score.resampled = !rawScored;
                score.angleDegrees =
                    SpectralClassification::spectralAngle( queryEff.data(), entryData,
                                                           bucket.bands, nodata ) *
                    ( 180.0 / std::acos( -1.0 ) );
                score.divergence =
                    SpectralClassification::spectralDivergence( queryEff.data(), entryData,
                                                                bucket.bands, nodata );
                scores.push_back( std::move( score ) );
                ++m_lastStats.entriesScored;
            }
            else
            {
                // Prescreen: the SAME clamped-cosine/acos expression as the
                // SAM kernel, in the same accumulation order, so the degree
                // key is bit-identical to brute force; only the SID histogram
                // is deferred until the cut is known.
                Candidate candidate;
                candidate.entryIndex = bucket.entryIndices[m];
                candidate.bucketIndex = b;
                candidate.rawScored = rawScored;

                double angleDegrees = std::numeric_limits<double>::quiet_NaN();
                if ( samUsable( queryEff.data(), entryData, bucket.bands, nodata ) &&
                     bucket.norms2[m] > 0.0 )
                {
                    double dot = 0.0;
                    double normQ = 0.0;
                    for ( int i = 0; i < bucket.bands; ++i )
                    {
                        const double tv =
                            static_cast<double>( queryEff[static_cast<size_t>( i )] );
                        const double rv = static_cast<double>( entryData[i] );
                        dot += tv * rv;
                        normQ += tv * tv;
                    }
                    if ( normQ > 0.0 )
                    {
                        const double denom =
                            std::sqrt( normQ ) * std::sqrt( bucket.norms2[m] );
                        angleDegrees = std::acos( std::clamp( dot / denom, -1.0, 1.0 ) ) *
                                       ( 180.0 / std::acos( -1.0 ) );
                    }
                }
                candidate.angleDegrees = angleDegrees;
                candidates.push_back( candidate );
                ++m_lastStats.entriesPrescreened;
            }
        }
    }

    if ( fullScoring )
    {
        // Ascending SAM angle; undefined (NaN) angles sort last; ties keep
        // library order (stable_sort) — identical to the brute-force path.
        std::stable_sort( scores.begin(), scores.end(),
                          []( const SpectralLibrary::MatchScore &a,
                              const SpectralLibrary::MatchScore &b ) {
                              const auto key = []( double v ) {
                                  return std::isnan( v )
                                             ? std::numeric_limits<double>::infinity()
                                             : v;
                              };
                              return key( a.angleDegrees ) < key( b.angleDegrees );
                          } );
        return scores;
    }

    // Prescreened path: valid candidates by (angle, entry index), undefined
    // angles last in library order — the brute-force order for the kept
    // prefix. SID runs only for the kept entries.
    std::vector<int> order( candidates.size(), 0 );
    for ( std::size_t i = 0; i < order.size(); ++i )
        order[i] = static_cast<int>( i );
    std::sort( order.begin(), order.end(), [&]( int a, int b ) {
        const auto key = []( double v ) {
            return std::isnan( v ) ? std::numeric_limits<double>::infinity() : v;
        };
        const double ka = key( candidates[static_cast<size_t>( a )].angleDegrees );
        const double kb = key( candidates[static_cast<size_t>( b )].angleDegrees );
        if ( ka != kb )
            return ka < kb;
        return candidates[static_cast<size_t>( a )].entryIndex <
               candidates[static_cast<size_t>( b )].entryIndex;
    } );

    const int comparableCount = static_cast<int>( candidates.size() );
    const int keep = options.topK > 0 ? std::min( options.topK, comparableCount )
                                      : comparableCount;
    scores.reserve( static_cast<size_t>( keep ) );
    for ( int i = 0; i < keep; ++i )
    {
        const Candidate &candidate =
            candidates[static_cast<size_t>( order[static_cast<size_t>( i )] )];
        const Bucket &bucket = m_buckets[candidate.bucketIndex];
        std::vector<float> queryEff;
        if ( candidate.rawScored )
            queryEff = spectrum;
        else
        {
            bool ok = false;
            queryEff = resampleQueryToBucket( spectrum, spectrumWavelengths, bucket, &ok );
            if ( !ok )
                continue; // cannot happen: the candidate survived the prescreen
        }
        const SpectralLibrary::Entry &entry =
            m_library->entries[static_cast<size_t>( candidate.entryIndex )];
        SpectralLibrary::MatchScore score;
        score.entryIndex = candidate.entryIndex;
        score.name = entry.name;
        score.material = entry.material;
        score.resampled = !candidate.rawScored;
        score.angleDegrees = candidate.angleDegrees;
        score.divergence = SpectralClassification::spectralDivergence(
            queryEff.data(), entry.spectrum.data(), bucket.bands, nodata );
        scores.push_back( std::move( score ) );
        ++m_lastStats.entriesScored;
    }
    return scores;
}

} // namespace SpectralLibraryScale
