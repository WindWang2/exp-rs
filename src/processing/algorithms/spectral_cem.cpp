// spectral_cem.cpp — see spectral_cem.h for contracts.

#include "spectral_cem.h"

#include "processing/algorithms/primitives/dense_linalg.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpectralCem
{
namespace
{
// Same per-band validity predicate as the RX family: a pixel is invalid as a
// whole when any band is non-finite, or equal (exact compare, family idiom)
// to a band's declared NoData value.
bool pixelValid( const float *pixel, int bands, const float *noDataBands,
                 const uint8_t *hasNoDataBands )
{
    for ( int b = 0; b < bands; ++b )
    {
        const float v = pixel[b];
        const bool checkNd = noDataBands && ( !hasNoDataBands || hasNoDataBands[b] );
        if ( !std::isfinite( v ) || ( checkNd && v == noDataBands[b] ) )
            return false;
    }
    return true;
}
} // namespace

void accumulateCorrelation( const float *pixels, size_t count, int bands,
                            CorrelationStats *stats, bool skipNonFinite,
                            const float *noDataBands,
                            const uint8_t *hasNoDataBands )
{
    if ( !pixels || !stats || count == 0 || bands <= 0 )
        return;
    if ( stats->bands != bands ||
         stats->correlation.size() != static_cast<size_t>( bands ) * bands )
    {
        stats->correlation.assign( static_cast<size_t>( bands ) * bands, 0.0 );
        stats->bands = bands;
        stats->count = 0;
    }
    // Upper-triangle raw outer product (mirrored in finalizeCorrelation).
    for ( size_t p = 0; p < count; ++p )
    {
        const float *x = pixels + p * static_cast<size_t>( bands );
        if ( skipNonFinite && !pixelValid( x, bands, noDataBands, hasNoDataBands ) )
            continue;
        for ( int i = 0; i < bands; ++i )
        {
            const double xi = x[i];
            const size_t rowOffset = static_cast<size_t>( i ) * bands;
            for ( int j = i; j < bands; ++j )
                stats->correlation[rowOffset + j] += xi * static_cast<double>( x[j] );
        }
        ++stats->count;
    }
}

void finalizeCorrelation( CorrelationStats *stats )
{
    if ( !stats || stats->count == 0 || stats->bands <= 0 )
        return;
    const int bands = stats->bands;
    // Second-moment (biased, /N) estimator — the matrix the CEM energy is
    // defined on. count is intentionally KEPT: the streaming driver needs it
    // for the min-samples guard and the diagnostics report.
    const double invCount = 1.0 / static_cast<double>( stats->count );
    for ( int i = 0; i < bands; ++i )
    {
        for ( int j = i; j < bands; ++j )
        {
            const double v = stats->correlation[static_cast<size_t>( i ) * bands + j] * invCount;
            stats->correlation[static_cast<size_t>( i ) * bands + j] = v;
            stats->correlation[static_cast<size_t>( j ) * bands + i] = v;
        }
    }
}

int minSamplesRequired( int bands, bool loadingEnabled )
{
    if ( bands <= 0 )
        return 0;
    return loadingEnabled ? bands + 1 : 2 * bands + 2;
}

bool buildFilter( const float *target, int bands,
                  const std::vector<double> &correlation,
                  double loading, Filter *out )
{
    if ( !target || !out || bands <= 0 )
        return false;
    if ( correlation.size() != static_cast<size_t>( bands ) * bands )
        return false;
    if ( !std::isfinite( loading ) || loading < 0.0 )
        return false;
    for ( const double v : correlation )
        if ( !std::isfinite( v ) )
            return false;
    for ( int b = 0; b < bands; ++b )
        if ( !std::isfinite( static_cast<double>( target[b] ) ) )
            return false;

    double trace = 0.0;
    for ( int i = 0; i < bands; ++i )
        trace += correlation[static_cast<size_t>( i ) * bands + i];
    if ( !( trace > 0.0 ) )
        return false;

    // Scaled diagonal loading: proportional to the data variance so the bias
    // stays predictable (same strategy as SpectralLocalRx, ADR 0163).
    std::vector<double> loaded = correlation;
    if ( loading > 0.0 )
    {
        const double load = loading * ( trace / static_cast<double>( bands ) );
        for ( int i = 0; i < bands; ++i )
            loaded[static_cast<size_t>( i ) * bands + i] += load;
    }

    std::vector<double> invCorr;
    if ( !sicnu::primitives::invertDenseMatrix( loaded, bands, &invCorr ) )
        return false;

    // w_raw = R'⁻¹ t; denominator tᵀR'⁻¹t must be positive and finite.
    std::vector<double> weight( static_cast<size_t>( bands ), 0.0 );
    double denom = 0.0;
    for ( int i = 0; i < bands; ++i )
    {
        const size_t rowOffset = static_cast<size_t>( i ) * bands;
        double row = 0.0;
        for ( int j = 0; j < bands; ++j )
            row += invCorr[rowOffset + j] * static_cast<double>( target[j] );
        weight[static_cast<size_t>( i )] = row;
        denom += static_cast<double>( target[i] ) * row;
    }
    if ( !std::isfinite( denom ) || !( denom > 0.0 ) )
        return false;

    out->weight.assign( static_cast<size_t>( bands ), 0.0 );
    for ( int i = 0; i < bands; ++i )
    {
        const double w = weight[static_cast<size_t>( i )] / denom;
        if ( !std::isfinite( w ) )
            return false;
        out->weight[static_cast<size_t>( i )] = w;
    }
    return true;
}

float cemScore( const float *x, const Filter &filter, int bands,
                std::vector<double> *scratch )
{
    // @a scratch is validated for capacity but unused: the CEM score needs no
    // centering buffer. Keeping the parameter lets the streaming driver pass
    // one shared scratch to the MF/ACE/CEM kernels uniformly.
    if ( !x || !scratch || scratch->size() < static_cast<size_t>( bands ) )
        return std::numeric_limits<float>::quiet_NaN();
    if ( filter.weight.size() != static_cast<size_t>( bands ) )
        return std::numeric_limits<float>::quiet_NaN();

    double score = 0.0;
    for ( int b = 0; b < bands; ++b )
    {
        if ( !std::isfinite( x[b] ) )
            return std::numeric_limits<float>::quiet_NaN();
        score += filter.weight[static_cast<size_t>( b )] * static_cast<double>( x[b] );
    }
    return static_cast<float>( score );
}

} // namespace SpectralCem
