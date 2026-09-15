// src/processing/algorithms/solar_geometry.cpp — see solar_geometry.h for the
// formula set, accuracy notes and the fail-closed contract.
#include "solar_geometry.h"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <limits>

namespace SolarGeometry
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

/// Spencer (1971) fractional year Γ with the NOAA time-of-day term.
double fractionalYear( int dayOfYear, double utcHours )
{
    return 2.0 * kPi / 365.0 * ( dayOfYear - 1 + ( utcHours - 12.0 ) / 24.0 );
}

double equationOfTimeMinutes( double gamma )
{
    return 229.18 * ( 0.000075 + 0.001868 * std::cos( gamma ) - 0.032077 * std::sin( gamma )
                      - 0.014615 * std::cos( 2.0 * gamma ) - 0.040849 * std::sin( 2.0 * gamma ) );
}

double declinationRadians( double gamma )
{
    return 0.006918 - 0.399912 * std::cos( gamma ) + 0.070257 * std::sin( gamma )
           - 0.006758 * std::cos( 2.0 * gamma ) + 0.000907 * std::sin( 2.0 * gamma )
           - 0.002697 * std::cos( 3.0 * gamma ) + 0.00148 * std::sin( 3.0 * gamma );
}

/// Spencer (1971) inverse-square factor series E₀ = d⁻² (perihelion ≈ 1.035).
double inverseSquareSeries( int dayOfYear )
{
    const double g = 2.0 * kPi / 365.0 * dayOfYear;
    return 1.000110 + 0.034221 * std::cos( g ) + 0.001280 * std::sin( g )
           + 0.000719 * std::cos( 2.0 * g ) + 0.000077 * std::sin( 2.0 * g );
}

bool finite( double v )
{
    return std::isfinite( v );
}
} // namespace

int dayOfYear( const QDate &date )
{
    return date.isValid() ? date.dayOfYear() : 0;
}

bool earthSunFactor( int dayOfYearNum, double *inverseSquareFactor, QString *errorMessage )
{
    if ( dayOfYearNum < 1 || dayOfYearNum > 366 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: day of year %1 outside [1, 366]" )
                                 .arg( dayOfYearNum );
        return false;
    }
    if ( !inverseSquareFactor )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: null output pointer" );
        return false;
    }
    *inverseSquareFactor = inverseSquareSeries( dayOfYearNum );
    return true;
}

bool earthSunDistanceAu( int dayOfYearNum, double *distanceAu, QString *errorMessage )
{
    double factor = 0.0;
    if ( !earthSunFactor( dayOfYearNum, &factor, errorMessage ) )
        return false;
    if ( !distanceAu )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: null output pointer" );
        return false;
    }
    *distanceAu = 1.0 / std::sqrt( factor );
    return true;
}

bool solarPosition( const QDate &date, const QTime &utcTime,
                    double latitudeDeg, double longitudeDeg,
                    SunPosition *out, QString *errorMessage )
{
    if ( !out )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: null output pointer" );
        return false;
    }
    if ( !date.isValid() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: invalid acquisition date" );
        return false;
    }
    if ( !utcTime.isValid() )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: invalid acquisition time" );
        return false;
    }
    if ( !finite( latitudeDeg ) || latitudeDeg < -90.0 || latitudeDeg > 90.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: latitude %1 outside [-90, 90]" )
                                 .arg( latitudeDeg );
        return false;
    }
    if ( !finite( longitudeDeg ) || longitudeDeg < -180.0 || longitudeDeg > 180.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: longitude %1 outside [-180, 180]" )
                                 .arg( longitudeDeg );
        return false;
    }

    const int doy = date.dayOfYear();
    const double utcHours = utcTime.hour() + utcTime.minute() / 60.0
                            + ( utcTime.second() + utcTime.msec() / 1000.0 ) / 3600.0;
    const double gamma = fractionalYear( doy, utcHours );

    const double eotMin = equationOfTimeMinutes( gamma );
    const double declRad = declinationRadians( gamma );

    // True solar time / hour angle (UTC input ⇒ timezone offset term is 0).
    const double timeOffsetMin = eotMin + 4.0 * longitudeDeg;
    const double tstMin = 60.0 * utcHours + timeOffsetMin;
    double haDeg = tstMin / 4.0 - 180.0;
    haDeg = std::fmod( haDeg + 180.0, 360.0 );
    if ( haDeg <= 0.0 )
        haDeg += 360.0;
    haDeg -= 180.0; // normalize to (−180, 180]

    const double latRad = latitudeDeg * kDegToRad;
    const double haRad = haDeg * kDegToRad;

    double cosZenith = std::sin( latRad ) * std::sin( declRad )
                       + std::cos( latRad ) * std::cos( declRad ) * std::cos( haRad );
    cosZenith = std::clamp( cosZenith, -1.0, 1.0 );
    const double zenithDeg = std::acos( cosZenith ) * kRadToDeg;

    // Azimuth from north, clockwise, in the same horizontal frame as the
    // topographic-correction illumination cosine.
    const double east = -std::cos( declRad ) * std::sin( haRad );
    const double north = std::sin( declRad ) * std::cos( latRad )
                         - std::cos( declRad ) * std::sin( latRad ) * std::cos( haRad );
    double azimuthDeg = std::atan2( east, north ) * kRadToDeg;
    if ( azimuthDeg < 0.0 )
        azimuthDeg += 360.0;

    out->zenithDeg = zenithDeg;
    out->elevationDeg = 90.0 - zenithDeg;
    out->azimuthDeg = azimuthDeg;
    out->declinationDeg = declRad * kRadToDeg;
    out->hourAngleDeg = haDeg;
    out->equationOfTimeMin = eotMin;
    out->earthSunDistanceAu = 1.0 / std::sqrt( inverseSquareSeries( doy ) );
    out->sunAboveHorizon = out->elevationDeg > 0.0;
    return true;
}

bool validateSunGeometryDeg( double sunElevationDeg, double sunAzimuthDeg,
                             QString *errorMessage )
{
    if ( !finite( sunElevationDeg ) || sunElevationDeg <= 0.0 || sunElevationDeg > 90.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: sun elevation %1 outside (0, 90] — "
                                            "a below-horizon or unknown sun cannot calibrate" )
                                 .arg( sunElevationDeg );
        return false;
    }
    if ( !finite( sunAzimuthDeg ) || sunAzimuthDeg < 0.0 || sunAzimuthDeg >= 360.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: sun azimuth %1 outside [0, 360)" )
                                 .arg( sunAzimuthDeg );
        return false;
    }
    return true;
}

bool validateViewGeometryDeg( double viewZenithDeg, double viewAzimuthDeg,
                              QString *errorMessage )
{
    if ( !finite( viewZenithDeg ) || viewZenithDeg < 0.0 || viewZenithDeg >= 90.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: view zenith %1 outside [0, 90)" )
                                 .arg( viewZenithDeg );
        return false;
    }
    if ( !finite( viewAzimuthDeg ) || viewAzimuthDeg < 0.0 || viewAzimuthDeg >= 360.0 )
    {
        if ( errorMessage )
            *errorMessage = QStringLiteral( "solar_geometry: view azimuth %1 outside [0, 360)" )
                                 .arg( viewAzimuthDeg );
        return false;
    }
    return true;
}

} // namespace SolarGeometry
