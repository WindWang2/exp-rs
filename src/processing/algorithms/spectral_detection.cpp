// spectral_detection.cpp — see spectral_detection.h for contracts.

#include "spectral_detection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SpectralDetection
{
namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
}

bool buildTargetModel( const float *target, int bands,
                       const std::vector<double> &mean,
                       const std::vector<double> &invCov,
                       TargetModel *out )
{
    if ( !target || !out || bands <= 0 )
        return false;
    if ( mean.size() != static_cast<size_t>( bands ) ||
         invCov.size() != static_cast<size_t>( bands ) * bands )
        return false;

    out->invCovT.assign( static_cast<size_t>( bands ), 0.0 );
    for ( int i = 0; i < bands; ++i )
    {
        const double tm = static_cast<double>( target[i] ) - mean[static_cast<size_t>( i )];
        if ( !std::isfinite( tm ) )
            return false;
        for ( int j = 0; j < bands; ++j )
            out->invCovT[static_cast<size_t>( j )] +=
                invCov[static_cast<size_t>( j ) * bands + i] * tm;
    }
    for ( int j = 0; j < bands; ++j )
        if ( !std::isfinite( out->invCovT[static_cast<size_t>( j )] ) )
            return false;

    out->tWhitenedNorm2 = 0.0;
    for ( int i = 0; i < bands; ++i )
        out->tWhitenedNorm2 +=
            ( static_cast<double>( target[i] ) - mean[static_cast<size_t>( i )] ) *
            out->invCovT[static_cast<size_t>( i )];
    return std::isfinite( out->tWhitenedNorm2 ) && out->tWhitenedNorm2 > 0.0;
}

float matchedFilterScore( const float *x, const TargetModel &model,
                          const std::vector<double> &mean, int bands,
                          std::vector<double> *scratch )
{
    if ( !x || !scratch || scratch->size() < static_cast<size_t>( bands ) )
        return std::numeric_limits<float>::quiet_NaN();
    double *centered = scratch->data();
    for ( int b = 0; b < bands; ++b )
    {
        if ( !std::isfinite( x[b] ) )
            return std::numeric_limits<float>::quiet_NaN();
        centered[b] = static_cast<double>( x[b] ) - mean[static_cast<size_t>( b )];
    }
    double score = 0.0;
    for ( int b = 0; b < bands; ++b )
        score += model.invCovT[static_cast<size_t>( b )] * centered[b];
    return static_cast<float>( score );
}

float aceScore( const float *x, const TargetModel &model,
                const std::vector<double> &mean,
                const std::vector<double> &invCov, int bands,
                std::vector<double> *scratch )
{
    if ( !x || !scratch || scratch->size() < static_cast<size_t>( bands ) )
        return std::numeric_limits<float>::quiet_NaN();
    double *centered = scratch->data();
    for ( int b = 0; b < bands; ++b )
    {
        if ( !std::isfinite( x[b] ) )
            return std::numeric_limits<float>::quiet_NaN();
        centered[b] = static_cast<double>( x[b] ) - mean[static_cast<size_t>( b )];
    }

    // x̃ᵀΣ⁻¹x̃ and t̃ᵀΣ⁻¹x̃.
    double xNorm2 = 0.0;
    double proj = 0.0;
    for ( int i = 0; i < bands; ++i )
    {
        double rowDot = 0.0;
        for ( int j = 0; j < bands; ++j )
            rowDot += invCov[static_cast<size_t>( i ) * bands + j] * centered[j];
        xNorm2 += centered[i] * rowDot;
        proj += model.invCovT[static_cast<size_t>( i )] * centered[i];
    }
    if ( !( xNorm2 > 0.0 ) || !std::isfinite( xNorm2 ) )
        return std::numeric_limits<float>::quiet_NaN();
    const double denom = model.tWhitenedNorm2 * xNorm2;
    const double score = ( proj * proj ) / denom;
    return static_cast<float>( std::clamp( score, 0.0, 1.0 ) );
}

} // namespace SpectralDetection
