// topographic_correction.cpp — see topographic_correction.h for contracts.

#include "topographic_correction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace TopographicCorrection
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
/// Same flatness epsilon as TerrainAnalysis::aspect.
constexpr float kFlatEps = 1e-10f;
} // namespace

const char *methodName( Method m )
{
    switch ( m )
    {
        case Method::Cosine: return "cosine";
        case Method::CCorrection: return "c_correction";
        case Method::Minnaert: return "minnaert";
    }
    return "cosine";
}

bool parseMethod( const char *token, Method *out )
{
    if ( !token || !out )
        return false;
    const std::string t( token );
    if ( t == "cosine" ) { *out = Method::Cosine; return true; }
    if ( t == "c_correction" ) { *out = Method::CCorrection; return true; }
    if ( t == "minnaert" ) { *out = Method::Minnaert; return true; }
    return false;
}

void hornGradient( const float *k9, double cellSizeX, double cellSizeY,
                   double *dzdx, double *dzdy )
{
    // Matches TerrainAnalysis's Horn kernels exactly (interior cells):
    //   dzdx = ((c + 2f + i) - (a + 2d + g)) / (8·csX)
    //   dzdy = ((g + 2h + i) - (a + 2b + c)) / (8·csY)
    const double a = k9[0], b = k9[1], c = k9[2];
    const double d = k9[3], /* e */ f = k9[5];
    const double g = k9[6], h = k9[7], i = k9[8];
    *dzdx = ( ( c + 2.0 * f + i ) - ( a + 2.0 * d + g ) ) / ( 8.0 * cellSizeX );
    *dzdy = ( ( g + 2.0 * h + i ) - ( a + 2.0 * b + c ) ) / ( 8.0 * cellSizeY );
}

void slopeAspectDeg( double dzdx, double dzdy, double *slopeDeg, double *aspectDeg )
{
    const double slope = std::atan( std::sqrt( dzdx * dzdx + dzdy * dzdy ) );
    *slopeDeg = slope * 180.0 / kPi;

    if ( std::abs( dzdx ) < kFlatEps && std::abs( dzdy ) < kFlatEps )
    {
        *aspectDeg = -1.0; // platform flat marker
        return;
    }
    double angle = std::atan2( -dzdx, dzdy ) * 180.0 / kPi;
    if ( angle < 0.0 )
        angle += 360.0;
    *aspectDeg = angle;
}

double illuminationCosine( double slopeDeg, double aspectDeg,
                           double solarZenithDeg, double solarAzimuthDeg )
{
    const double slope = slopeDeg * kDegToRad;
    const double zen = solarZenithDeg * kDegToRad;
    if ( slopeDeg <= 0.0 || aspectDeg < 0.0 ) // flat (or the −1 marker)
        return std::cos( zen );
    const double az = aspectDeg * kDegToRad;
    const double sunAz = solarAzimuthDeg * kDegToRad;
    return std::cos( zen ) * std::cos( slope ) +
           std::sin( zen ) * std::sin( slope ) * std::cos( sunAz - az );
}

// ---------------------------------------------------------------------------

void OlsRegression::add( double x, double y )
{
    if ( !std::isfinite( x ) || !std::isfinite( y ) )
        return;
    ++m_count;
    m_sx += x;
    m_sy += y;
    m_sxx += x * x;
    m_sxy += x * y;
}

bool OlsRegression::fit( double *a, double *b ) const
{
    if ( !a || !b || m_count < 2 )
        return false;
    const double n = static_cast<double>( m_count );
    const double meanX = m_sx / n;
    const double meanY = m_sy / n;
    // Centered (numerically stable) slope: Σ(x−x̄)(y−ȳ) / Σ(x−x̄)².
    const double sxx = m_sxx - n * meanX * meanX;
    if ( !( sxx > 0.0 ) )
        return false;
    const double sxy = m_sxy - n * meanX * meanY;
    const double slope = sxy / sxx;
    if ( !std::isfinite( slope ) )
        return false;
    *b = slope;
    *a = meanY - slope * meanX;
    return true;
}

void MinnaertRegression::add( double cosIllumination, double value )
{
    // Valid domain: strictly positive illumination and radiance (log fit).
    if ( !( cosIllumination > 0.0 ) || !( value > 0.0 ) )
        return;
    if ( !std::isfinite( cosIllumination ) || !std::isfinite( value ) )
        return;
    // One owner of the OLS sums (Milestone C): the Minnaert exponent is the
    // slope of ln L on ln cos_i over the log-transformed pairs (#773).
    m_logFit.add( std::log( cosIllumination ), std::log( value ) );
}

bool MinnaertRegression::fit( double *k ) const
{
    if ( !k )
        return false;
    double a = 0.0;
    double slope = 0.0;
    if ( !m_logFit.fit( &a, &slope ) )
        return false;
    if ( !std::isfinite( slope ) )
        return false;
    // Empirical Minnaert relation: L = L_n · cos(i)^k, i.e. the log-log fit
    // ln L = a + k·ln cos_i. The OLS slope of ln L on ln cos_i therefore IS
    // the Minnaert exponent: real imagery brightens with illumination, so a
    // physical scene yields a positive slope. (#773: negating the slope here
    // made every physically valid scene fail the positivity guard below —
    // and forced the old synthetic test to feed inverted radiance.)
    if ( !( slope > 1e-6 ) )
        return false;
    *k = slope;
    return true;
}

BandFit fitBand( Method method, double solarZenithDeg,
                 const OlsRegression &ols, const MinnaertRegression &minnaert )
{
    BandFit fit;
    fit.cosZenith = std::cos( solarZenithDeg * kDegToRad );
    switch ( method )
    {
        case Method::Cosine:
            fit.usable = true;
            break;
        case Method::CCorrection:
        {
            double a = 0.0;
            double b = 0.0;
            if ( ols.fit( &a, &b ) && std::abs( b ) >= 1e-6 )
            {
                fit.a = a;
                fit.b = b;
                fit.c = a / b;
                fit.usable = true;
            }
            break;
        }
        case Method::Minnaert:
        {
            double k = 0.0;
            if ( minnaert.fit( &k ) )
            {
                fit.k = k;
                fit.usable = true;
            }
            break;
        }
    }
    return fit;
}

float correctPixel( Method method, float value, double cosIllumination, const BandFit &fit )
{
    if ( !std::isfinite( value ) )
        return std::numeric_limits<float>::quiet_NaN();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    switch ( method )
    {
        case Method::Cosine:
            if ( !( cosIllumination > 1e-9 ) )
                return nan; // self-shadowed: no illumination model applies
            return static_cast<float>( value * fit.cosZenith / cosIllumination );

        case Method::CCorrection:
        {
            const double denom = cosIllumination + fit.c;
            if ( !( denom > 1e-9 ) )
                return nan;
            return static_cast<float>( value * ( fit.cosZenith + fit.c ) / denom );
        }

        case Method::Minnaert:
            if ( !( cosIllumination > 1e-9 ) || !( value > 0.0 ) )
                return nan; // outside the fit domain
            return static_cast<float>(
                value * std::pow( fit.cosZenith / cosIllumination, fit.k ) );
    }
    return nan;
}

} // namespace TopographicCorrection
