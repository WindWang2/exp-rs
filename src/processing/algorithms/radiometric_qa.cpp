// src/processing/algorithms/radiometric_qa.cpp — see radiometric_qa.h for
// the flag-word contract and propagation semantics.
#include "radiometric_qa.h"

#include <cmath>
#include <limits>

namespace RadiometricQa
{

namespace
{
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

/// Flag → summary index (bit position); word ≤ bit 8 ⇒ index 0..8.
size_t flagIndex( uint16_t flag )
{
    size_t idx = 0;
    while ( ( ( flag >> idx ) & 1u ) == 0u )
        ++idx;
    return idx;
}
} // namespace

double Summary::fraction( uint16_t flag ) const
{
    // Single-bit flag words only; FlagNone (0) has no index.
    if ( pixels == 0 || flag == 0 || ( flag & ( flag - 1 ) ) != 0 )
        return 0.0;
    return static_cast<double>( counts[flagIndex( flag )] ) / static_cast<double>( pixels );
}

void evaluateReflectance( const float *rho, uint16_t *flags, size_t count,
                          float saturationLevel, Summary *summary )
{
    if ( summary )
        summary->pixels = count;
    for ( size_t i = 0; i < count; ++i )
    {
        uint16_t f = FlagNone;
        const float v = rho[i];
        if ( !std::isfinite( v ) )
        {
            f |= FlagNotFinite;
        }
        else
        {
            if ( v < 0.0f )
                f |= FlagNegative;
            if ( v > 1.0f )
                f |= FlagOverRange;
            if ( saturationLevel > 0.0f && v >= saturationLevel )
                f |= FlagSaturated;
        }
        flags[i] = f;
        if ( summary )
        {
            for ( uint16_t bit = FlagSaturated; bit != ( FlagInvalidAngles << 1 );
                  bit <<= 1 )
            {
                if ( f & bit )
                    ++summary->counts[flagIndex( bit )];
            }
            if ( f != FlagNone )
                ++summary->flaggedPixels;
        }
    }
}

void propagate( uint16_t *flags, const uint16_t *stepFlags, size_t count )
{
    if ( flags == stepFlags )
        return;
    for ( size_t i = 0; i < count; ++i )
        flags[i] |= stepFlags[i];
}

void markFromMask( uint16_t *flags, const uint8_t *mask, size_t count, uint16_t which )
{
    for ( size_t i = 0; i < count; ++i )
        if ( mask[i] )
            flags[i] |= which;
}

void addSaturationBits( uint16_t *flags, const uint16_t *qaBits, size_t count,
                        uint16_t bandMask )
{
    for ( size_t i = 0; i < count; ++i )
        if ( qaBits[i] & bandMask )
            flags[i] |= FlagSaturationQa;
}

bool propagateLinearUncertainty( const float *x, const float *sigmaX, size_t count,
                                 double gain, double gainSigma, double biasSigma,
                                 float *sigmaY, QString *errorMessage )
{
    if ( !x || !sigmaY )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "radiometric_qa: null buffers" );
        return false;
    }
    if ( gainSigma < 0.0 || biasSigma < 0.0 || gain < 0.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "radiometric_qa: negative uncertainty" );
        return false;
    }
    for ( size_t i = 0; i < count; ++i )
    {
        const float xi = x[i];
        if ( !std::isfinite( xi ) )
        {
            sigmaY[i] = kNaN;
            continue;
        }
        const double sxi = sigmaX ? static_cast<double>( sigmaX[i] ) : 0.0;
        const double variance = std::pow( gain * sxi, 2 ) + std::pow( xi * gainSigma, 2 )
                                + biasSigma * biasSigma;
        sigmaY[i] = static_cast<float>( std::sqrt( variance ) );
    }
    return true;
}

} // namespace RadiometricQa
