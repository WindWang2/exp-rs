// src/processing/algorithms/brdf_normalization.cpp — see
// brdf_normalization.h for the kernel forms, weight contract and validity
// conditions; formulas per Lucht et al. 2000 (MODIS BRDF/Albedo ATBD).
#include "brdf_normalization.h"

#include "solar_geometry.h"

#include <cmath>
#include <limits>

namespace BrdfNormalization
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kDegenerateSlope = 1e-6;

bool finite( double v )
{
    return std::isfinite( v );
}

/// Zenith in [0°, 90°) — the validated kernel domain (90° excluded: sec()
/// diverges; the SolarGeometry view validator uses the same domain).
bool validZenithDeg( double z )
{
    return finite( z ) && z >= 0.0 && z < 90.0;
}

bool validRelativeAzimuthDeg( double a )
{
    return finite( a ) && a >= -360.0 && a <= 360.0; // cos() folds sign
}
} // namespace

double rossThick( double sunZenithDeg, double viewZenithDeg, double relativeAzimuthDeg )
{
    const double cosXi = std::cos( sunZenithDeg * kDegToRad ) * std::cos( viewZenithDeg * kDegToRad )
                         + std::sin( sunZenithDeg * kDegToRad )
                               * std::sin( viewZenithDeg * kDegToRad )
                               * std::cos( relativeAzimuthDeg * kDegToRad );
    const double xi = std::acos( std::clamp( cosXi, -1.0, 1.0 ) );
    const double cosSun = std::cos( sunZenithDeg * kDegToRad );
    const double cosView = std::cos( viewZenithDeg * kDegToRad );
    return ( ( kPi / 2.0 - xi ) * cosXi + std::sin( xi ) ) / ( cosSun + cosView ) - kPi / 4.0;
}

double liSparseReciprocal( double sunZenithDeg, double viewZenithDeg,
                           double relativeAzimuthDeg )
{
    const double tanSun = std::tan( sunZenithDeg * kDegToRad );
    const double tanView = std::tan( viewZenithDeg * kDegToRad );
    const double secSun = 1.0 / std::cos( sunZenithDeg * kDegToRad );
    const double secView = 1.0 / std::cos( viewZenithDeg * kDegToRad );
    const double cosDelta = std::cos( relativeAzimuthDeg * kDegToRad );
    const double sinDelta = std::sin( relativeAzimuthDeg * kDegToRad );

    const double d2 = tanSun * tanSun + tanView * tanView - 2.0 * tanSun * tanView * cosDelta;
    const double d = std::sqrt( std::max( d2, 0.0 ) );

    // Overlap: (h/b) = 2 (Lucht et al. 2000 standard crown-relative height).
    const double cosT = 2.0 * std::sqrt( std::max( d2 + tanSun * tanSun * tanView * tanView
                                                       * sinDelta * sinDelta,
                                                   0.0 ) )
                        / ( secSun + secView );
    const double t = std::acos( std::clamp( cosT, -1.0, 1.0 ) );
    const double overlap = ( t - std::sin( t ) * std::cos( t ) ) / kPi * ( secSun + secView );

    const double dPrime = std::sqrt( std::max(
        d2 + 4.0 * tanSun * tanSun * tanView * tanView * sinDelta * sinDelta, 0.0 ) );

    return overlap - secSun - secView + 0.5 * ( d + dPrime );
}

bool anisotropyFactor( double sunZenithDeg, double viewZenithDeg, double relativeAzimuthDeg,
                       double fVol, double fGeo, double *out, QString *errorMessage )
{
    if ( !validZenithDeg( sunZenithDeg ) || !validZenithDeg( viewZenithDeg ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: sun/view zenith outside [0, 90) degrees" );
        return false;
    }
    if ( !validRelativeAzimuthDeg( relativeAzimuthDeg ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: relative azimuth outside ±360 degrees" );
        return false;
    }
    if ( !finite( fVol ) || !finite( fGeo ) )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: kernel weights must be finite" );
        return false;
    }
    const double factor = 1.0 + fVol * rossThick( sunZenithDeg, viewZenithDeg, relativeAzimuthDeg )
                          + fGeo * liSparseReciprocal( sunZenithDeg, viewZenithDeg,
                                                       relativeAzimuthDeg );
    if ( !finite( factor ) || factor <= 0.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: anisotropy factor %1 is nonphysical "
                                            "(1 + f_vol·k_vol + f_geo·k_geo must be > 0)" )
                                .arg( factor );
        return false;
    }
    if ( out )
        *out = factor;
    return true;
}

bool normalizeKernelDriven( float value, double sunZenithDeg, double viewZenithDeg,
                            double relativeAzimuthDeg, double fVol, double fGeo, float *out,
                            QString *errorMessage, double refSunZenithDeg,
                            double refViewZenithDeg, double refRelativeAzimuthDeg )
{
    if ( !out )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: null output pointer" );
        return false;
    }
    if ( !std::isfinite( value ) )
    {
        *out = std::numeric_limits<float>::quiet_NaN();
        return true;
    }
    // Default reference geometry: unchanged sun, nadir view.
    const double refSun = refSunZenithDeg < 0.0 ? sunZenithDeg : refSunZenithDeg;

    double fObs = 0.0;
    if ( !anisotropyFactor( sunZenithDeg, viewZenithDeg, relativeAzimuthDeg, fVol, fGeo,
                            &fObs, errorMessage ) )
        return false;
    double fRef = 0.0;
    if ( !anisotropyFactor( refSun, refViewZenithDeg, refRelativeAzimuthDeg, fVol, fGeo, &fRef,
                            errorMessage ) )
        return false;

    *out = static_cast<float>( value * fRef / fObs );
    return true;
}

void PairRegression::add( double date2Value, double date1Value )
{
    if ( !finite( date2Value ) || !finite( date1Value ) || date2Value <= 0.0
         || date1Value <= 0.0 )
        return; // outside the reflectance domain — not a usable pair
    ++m_count;
    m_sx += date2Value;
    m_sy += date1Value;
    m_sxx += date2Value * date2Value;
    m_sxy += date2Value * date1Value;
}

bool PairRegression::fitCFactor( size_t minPairs, double *c, QString *errorMessage ) const
{
    if ( !c )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: null output pointer" );
        return false;
    }
    if ( m_count < minPairs || m_count < 2 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: only %1 usable pairs (need ≥ %2)" )
                                .arg( m_count )
                                .arg( std::max( minPairs, size_t{ 2 } ) );
        return false;
    }
    const double n = static_cast<double>( m_count );
    const double b = ( n * m_sxy - m_sx * m_sy ) / ( n * m_sxx - m_sx * m_sx );
    if ( !finite( b ) || std::abs( b ) < kDegenerateSlope )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: degenerate regression slope %1 — the pair "
                                            "sample carries no radiometric signal" )
                                .arg( b );
        return false;
    }
    const double meanX = m_sx / n;
    const double meanY = m_sy / n;
    const double a = meanY - b * meanX;
    const double cValue = a / b;
    if ( !finite( cValue ) || a < 0.0 || cValue <= 0.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "brdf: unusable c-factor (a = %1, c = %2)" )
                                .arg( a )
                                .arg( cValue );
        return false;
    }
    *c = cValue;
    return true;
}

float applyPairNormalization( float date2Value, double cFactor )
{
    if ( !std::isfinite( date2Value ) )
        return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>( date2Value * cFactor );
}

} // namespace BrdfNormalization
