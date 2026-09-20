// test_spectral_library_scale.cpp — Spectral Intelligence 12.0 work package C:
// library-scale matching. The scaled path (grid-bucket index + cached query
// resampling + prescreened top-K) must be result-identical to the brute-force
// SpectralLibrary::matchSpectrum, and its Stats must prove the avoided work
// (machine-independent operation counts, not wall-clock).
//
// Deterministic LCG fixtures: entry values and query values are generated
// from a fixed linear congruential generator so failures are reproducible.

#include "processing/algorithms/spectral_library.h"
#include "processing/algorithms/spectral_library_scale.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace SpectralLibrary;
using namespace SpectralLibraryScale;

namespace
{
struct Lcg
{
    unsigned long long state = 20260920ull;
    float next( float lo, float hi )
    {
        state = state * 1103515245ull + 12345ull;
        const float unit = static_cast<float>( ( state >> 16 ) % 100000ull ) / 100000.0f;
        return lo + unit * ( hi - lo );
    }
};

Entry makeEntry( int index, int bands, Lcg &lcg, const std::vector<float> &wavelengths )
{
    Entry entry;
    entry.name = QStringLiteral( "entry_%1" ).arg( index );
    entry.material = QStringLiteral( "material_%1" ).arg( index % 5 );
    entry.spectrum.resize( bands );
    for ( int b = 0; b < bands; ++b )
        entry.spectrum[static_cast<size_t>( b )] = lcg.next( 0.01f, 0.9f );
    if ( !wavelengths.empty() )
        entry.wavelengths = wavelengths;
    return entry;
}

void expectSameMatches( const std::vector<MatchScore> &brute,
                        const std::vector<MatchScore> &scaled )
{
    REQUIRE( scaled.size() == brute.size() );
    for ( size_t i = 0; i < brute.size(); ++i )
    {
        INFO( "rank " << i << ": brute entry " << brute[i].entryIndex << " vs scaled entry "
                      << scaled[i].entryIndex );
        REQUIRE( scaled[i].entryIndex == brute[i].entryIndex );
        REQUIRE( scaled[i].resampled == brute[i].resampled );
        const bool bruteNaN = std::isnan( brute[i].angleDegrees );
        REQUIRE( std::isnan( scaled[i].angleDegrees ) == bruteNaN );
        if ( !bruteNaN )
            REQUIRE( scaled[i].angleDegrees == brute[i].angleDegrees ); // bit-exact
        const bool bruteDivNaN = std::isnan( brute[i].divergence );
        REQUIRE( std::isnan( scaled[i].divergence ) == bruteDivNaN );
        if ( !bruteDivNaN )
            REQUIRE( scaled[i].divergence == brute[i].divergence ); // bit-exact
    }
}
} // namespace

TEST_CASE( "MatchIndex matches brute force exactly across grids, sentinels and skips",
           "[spectral][library][scale]" )
{
    // Grid A: 64 bands, 400–715 nm; grids B/C: 8/32 bands, both fully inside
    // A's range so resampling onto them succeeds (out-of-range targets are
    // correctly skipped by BOTH paths — exercised in its own test below).
    std::vector<float> gridA( 64 );
    for ( size_t i = 0; i < gridA.size(); ++i )
        gridA[i] = 400.0f + 5.0f * static_cast<float>( i );
    std::vector<float> gridB{ 420.0f, 470.0f, 520.0f, 570.0f, 620.0f, 650.0f, 690.0f, 710.0f };
    std::vector<float> gridC( 32 );
    for ( size_t i = 0; i < gridC.size(); ++i )
        gridC[i] = 450.0f + 8.0f * static_cast<float>( i );

    Library library;
    Lcg lcg;
    // 1000 entries on A, 150 on B, 50 on C.
    for ( int i = 0; i < 1000; ++i )
        library.entries.append( makeEntry( i, 64, lcg, gridA ) );
    for ( int i = 0; i < 150; ++i )
        library.entries.append( makeEntry( 1000 + i, 8, lcg, gridB ) );
    for ( int i = 0; i < 50; ++i )
        library.entries.append( makeEntry( 1150 + i, 32, lcg, gridC ) );
    // Degenerate entries on grid A: one with a sentinel value, one all-zero.
    {
        Entry sentinel = makeEntry( 1200, 64, lcg, gridA );
        sentinel.spectrum[10] = SpectralClassification::kNoDataSentinel;
        library.entries.append( sentinel );
        Entry zero = makeEntry( 1201, 64, lcg, gridA );
        zero.spectrum.assign( 64, 0.0f );
        library.entries.append( zero );
    }

    // Query on grid A (the dominant bucket → only B and C need resampling).
    Lcg lcgQuery;
    std::vector<float> query( 64 );
    for ( size_t b = 0; b < query.size(); ++b )
        query[b] = lcgQuery.next( 0.02f, 0.85f );

    MatchIndex index;
    REQUIRE( MatchIndex::build( library, &index ) );

    auto brute = matchSpectrum( query, gridA, library );
    auto scaled = index.match( query, gridA );
    expectSameMatches( brute, scaled );

    // Machine-independent scale evidence: exactly two distinct non-query
    // grids were resampled (B and C), not 200 entries.
    const Stats stats = index.lastStats();
    REQUIRE( stats.resamplesPerformed == 2 );
    REQUIRE( stats.resamplesAvoided == 198 );
    REQUIRE( stats.entriesPrescreened == 0 ); // full scoring path scored everything
    REQUIRE( stats.entriesScored == static_cast<int>( brute.size() ) );

    // Prescreened top-K path: identical prefix, less work.
    Options topK;
    topK.topK = 10;
    auto scaledTop = index.match( query, gridA, topK );
    REQUIRE( scaledTop.size() == 10 );
    for ( int i = 0; i < 10; ++i )
    {
        INFO( "top-K rank " << i );
        REQUIRE( scaledTop[static_cast<size_t>( i )].entryIndex ==
                 brute[static_cast<size_t>( i )].entryIndex );
        REQUIRE( scaledTop[static_cast<size_t>( i )].angleDegrees ==
                 brute[static_cast<size_t>( i )].angleDegrees );
        REQUIRE( scaledTop[static_cast<size_t>( i )].divergence ==
                 brute[static_cast<size_t>( i )].divergence );
    }
    const Stats topStats = index.lastStats();
    REQUIRE( topStats.resamplesPerformed == 2 );
    REQUIRE( topStats.entriesPrescreened == 1202 ); // every comparable entry
    REQUIRE( topStats.entriesScored == 10 );        // only the kept prefix
    // Prescreen must keep the true best: rank 0 identical, no sentinel entry
    // smuggled into the prefix.
    for ( const auto &score : scaledTop )
        REQUIRE( score.entryIndex < 1200 );
}

TEST_CASE( "MatchIndex skips incomparable buckets like brute force (no query grid)",
           "[spectral][library][scale]" )
{
    std::vector<float> gridA{ 400.0f, 500.0f, 600.0f };
    std::vector<float> gridB{ 500.0f, 600.0f };
    Library library;
    Lcg lcg;
    library.entries.append( makeEntry( 0, 3, lcg, gridA ) );
    library.entries.append( makeEntry( 1, 3, lcg, gridA ) );
    library.entries.append( makeEntry( 2, 2, lcg, gridB ) );
    library.entries.append( makeEntry( 3, 2, lcg, std::vector<float>{} ) ); // no grid

    // Query on grid A WITHOUT its own wavelengths: band-mismatched entries
    // cannot be resampled, so only the same-band-count entries are scored.
    Lcg lcgQuery;
    std::vector<float> query( 3 );
    for ( auto &v : query )
        v = lcgQuery.next( 0.1f, 0.8f );

    auto brute = matchSpectrum( query, {}, library );
    REQUIRE( brute.size() == 2 );

    MatchIndex index;
    REQUIRE( MatchIndex::build( library, &index ) );
    auto scaled = index.match( query, {} );
    expectSameMatches( brute, scaled );
    REQUIRE( index.lastStats().resamplesPerformed == 0 );

    // WITH the query grid the 2-band grid-B entry becomes comparable too;
    // the grid-less 2-band entry stays incomparable (both paths skip it).
    auto bruteWithWl = matchSpectrum( query, gridA, library );
    REQUIRE( bruteWithWl.size() == 3 );
    auto scaledWithWl = index.match( query, gridA );
    expectSameMatches( bruteWithWl, scaledWithWl );
    const size_t resampledCount = static_cast<size_t>(
        std::count_if( scaledWithWl.begin(), scaledWithWl.end(),
                       []( const MatchScore &m ) { return m.resampled; } ) );
    REQUIRE( resampledCount == 1 ); // only the 2-band entry with a grid
}

TEST_CASE( "MatchIndex Gaussian resampling path matches brute force (FWHM grids)",
           "[spectral][library][scale]" )
{
    // Grid-B entries carry a DIFFERENT band count (4) plus FWHM, so they are
    // resampled (Gaussian SRF) rather than raw-scored; all B centers lie
    // inside grid A's [400,600] range.
    std::vector<float> gridA{ 400.0f, 500.0f, 600.0f };
    std::vector<float> gridB{ 420.0f, 495.0f, 560.0f, 595.0f };
    std::vector<float> fwhmB{ 10.0f, 12.0f, 9.0f, 8.0f };

    Library library;
    Lcg lcg;
    library.entries.append( makeEntry( 0, 3, lcg, gridA ) );
    for ( int i = 0; i < 4; ++i )
    {
        Entry entry = makeEntry( 1 + i, 4, lcg, gridB );
        entry.fwhm = fwhmB;
        library.entries.append( entry );
    }

    Lcg lcgQuery;
    std::vector<float> query( 3 );
    for ( auto &v : query )
        v = lcgQuery.next( 0.1f, 0.8f );

    MatchIndex index;
    REQUIRE( MatchIndex::build( library, &index ) );
    auto brute = matchSpectrum( query, gridA, library );
    REQUIRE( brute.size() == 5 );
    auto scaled = index.match( query, gridA );
    expectSameMatches( brute, scaled );
    REQUIRE( index.lastStats().resamplesPerformed == 1 ); // one B bucket
}

TEST_CASE( "MatchIndex build and match guards", "[spectral][library][scale]" )
{
    Library empty;
    MatchIndex index;
    REQUIRE_FALSE( MatchIndex::build( empty, &index ) );
    REQUIRE_FALSE( MatchIndex::build( empty, nullptr ) );

    std::vector<float> gridA{ 400.0f, 500.0f, 600.0f };
    Library library;
    Lcg lcg;
    library.entries.append( makeEntry( 0, 3, lcg, gridA ) );
    REQUIRE( MatchIndex::build( library, &index ) );

    REQUIRE( index.match( {}, {} ).empty() );
    REQUIRE( index.match( { 0.1f, 0.2f, 0.3f }, {} ).size() == 1 );
}

TEST_CASE( "MatchIndex cross-bucket undefined-angle tail keeps library order",
           "[spectral][library][scale]" )
{
    // Regression (review P0): the full-scoring path pushes scores in
    // bucket-first-appearance order, so a plain stable angle sort ordered the
    // NaN tail by BUCKET order instead of library order when sentinel/zero
    // entries lived in different grids. Library order here is
    // [okB, okA, sentinelA, zeroB]; brute force must rank the undefined pair
    // as sentinelA then zeroB even though the B bucket is discovered first.
    std::vector<float> gridA{ 400.0f, 500.0f, 600.0f };
    std::vector<float> gridB{ 450.0f, 550.0f };

    Library library;
    Lcg lcg;
    Entry okB = makeEntry( 0, 2, lcg, gridB );
    library.entries.append( okB );
    library.entries.append( makeEntry( 1, 3, lcg, gridA ) );
    Entry sentinelA = makeEntry( 2, 3, lcg, gridA );
    sentinelA.spectrum[1] = SpectralClassification::kNoDataSentinel;
    library.entries.append( sentinelA );
    Entry zeroB = makeEntry( 3, 2, lcg, gridB );
    zeroB.spectrum.assign( 2, 0.0f );
    library.entries.append( zeroB );

    Lcg lcgQuery;
    std::vector<float> query( 3 );
    for ( auto &v : query )
        v = lcgQuery.next( 0.1f, 0.8f );

    auto brute = matchSpectrum( query, gridA, library );
    REQUIRE( brute.size() == 4 );
    REQUIRE( brute[2].entryIndex == 2 ); // sentinelA (NaN tail, library order)
    REQUIRE( brute[3].entryIndex == 3 ); // zeroB
    REQUIRE( std::isnan( brute[2].angleDegrees ) );
    REQUIRE( std::isnan( brute[3].angleDegrees ) );

    MatchIndex index;
    REQUIRE( MatchIndex::build( library, &index ) );
    auto scaled = index.match( query, gridA );
    expectSameMatches( brute, scaled );

    // The prescreened path must reproduce the same tail order for the kept
    // prefix.
    Options topK;
    topK.topK = 4;
    auto scaledTop = index.match( query, gridA, topK );
    expectSameMatches( brute, scaledTop );
}

TEST_CASE( "MatchIndex scales to a 10k-entry library without per-query full "
           "resampling",
           "[spectral][library][scale][consolidation]" )
{
    // 10k entries on three grids (9000/750/250): the consolidation must not
    // degrade the scale path — one query resamples per DISTINCT grid (2 here),
    // never per entry, and the ranking stays bit-identical to brute force.
    // Grid A spans 400–715 nm so grids B (to 710) and C (to 600) are fully
    // inside the query range (an out-of-range grid would be legitimately
    // skipped as incomparable, which is not what this case measures).
    std::vector<float> gridA( 64 );
    for ( size_t i = 0; i < gridA.size(); ++i )
        gridA[i] = 400.0f + 5.0f * static_cast<float>( i );
    std::vector<float> gridB{ 420.0f, 470.0f, 520.0f, 570.0f, 620.0f, 650.0f, 690.0f };
    std::vector<float> gridC( 16 );
    for ( size_t i = 0; i < gridC.size(); ++i )
        gridC[i] = 450.0f + 10.0f * static_cast<float>( i );

    Library library;
    Lcg lcg;
    for ( int i = 0; i < 9000; ++i )
        library.entries.append( makeEntry( i, 64, lcg, gridA ) );
    for ( int i = 0; i < 750; ++i )
        library.entries.append( makeEntry( 9000 + i, 7, lcg, gridB ) );
    for ( int i = 0; i < 250; ++i )
        library.entries.append( makeEntry( 9750 + i, 16, lcg, gridC ) );

    Lcg lcgQuery;
    std::vector<float> query( 64 );
    for ( size_t b = 0; b < query.size(); ++b )
        query[b] = lcgQuery.next( 0.02f, 0.85f );

    MatchIndex index;
    REQUIRE( MatchIndex::build( library, &index ) );

    const auto brute = matchSpectrum( query, gridA, library );
    const auto scaled = index.match( query, gridA );
    expectSameMatches( brute, scaled );

    const Stats stats = index.lastStats();
    REQUIRE( stats.resamplesPerformed == 2 ); // B and C, NOT 1000 entries
    REQUIRE( stats.resamplesAvoided == 998 );
    REQUIRE( stats.entriesScored == static_cast<int>( brute.size() ) );

    // Top-K prefix: identical ranking and values, only the prefix scored.
    Options topK;
    topK.topK = 25;
    const auto scaledTop = index.match( query, gridA, topK );
    REQUIRE( scaledTop.size() == 25 );
    for ( int i = 0; i < 25; ++i )
    {
        INFO( "top-K rank " << i );
        REQUIRE( scaledTop[static_cast<size_t>( i )].entryIndex ==
                 brute[static_cast<size_t>( i )].entryIndex );
        REQUIRE( scaledTop[static_cast<size_t>( i )].angleDegrees ==
                 brute[static_cast<size_t>( i )].angleDegrees );
        REQUIRE( scaledTop[static_cast<size_t>( i )].divergence ==
                 brute[static_cast<size_t>( i )].divergence );
    }
    const Stats topStats = index.lastStats();
    REQUIRE( topStats.resamplesPerformed == 2 );
    REQUIRE( topStats.entriesPrescreened == 10000 );
    REQUIRE( topStats.entriesScored == 25 );
}
