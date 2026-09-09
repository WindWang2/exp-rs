// sar_terrain_geometry.cpp — see sar_terrain_geometry.h for the geometry
// contract and the documented full-range-Doppler refusal.

#include "sar_terrain_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::sar
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
} // namespace

TerrainGeometryResult terrainGeometry( double dzdx, double dzdy,
                                       double incidenceDeg, double lookAzimuthDeg )
{
    TerrainGeometryResult out;
    if ( !std::isfinite( dzdx ) || !std::isfinite( dzdy ) ||
         !std::isfinite( incidenceDeg ) || !std::isfinite( lookAzimuthDeg ) ||
         incidenceDeg <= 0.0 || incidenceDeg >= 90.0 )
    {
        out.localIncidenceDeg = kNaN;
        return out;
    }

    const double thetaI = incidenceDeg * kDegToRad;
    const double phiH = lookAzimuthDeg * kDegToRad;
    const double sinPhi = std::sin( phiH );
    const double cosPhi = std::cos( phiH );

    // cos(local incidence) = (−v)·n with unit-length factors; the ray's
    // vertical component is −cosθi (incidence measured from vertical):
    //   numerator   sinθi·(gE·sinφh + gN·cosφh) + cosθi
    //   denominator sqrt(gE² + gN² + 1)
    const double gLook = dzdx * sinPhi + dzdy * cosPhi;
    const double normalLen = std::sqrt( dzdx * dzdx + dzdy * dzdy + 1.0 );
    const double cosInc =
        ( std::sin( thetaI ) * gLook + std::cos( thetaI ) ) / normalLen;
    if ( !std::isfinite( cosInc ) )
    {
        out.localIncidenceDeg = kNaN;
        return out;
    }
    out.localIncidenceDeg =
        std::acos( std::clamp( cosInc, -1.0, 1.0 ) ) / kDegToRad;

    // Signed range-direction slope: positive when the terrain rises toward
    // the sensor (the sensor sits opposite the look azimuth).
    const double gTowardSensor = -gLook;
    const double alpha = std::atan( gTowardSensor ); // radians, signed

    if ( alpha > thetaI )
        out.maskClass = TerrainMaskClass::Layover;
    else if ( alpha < thetaI - kPi / 2.0 )
        out.maskClass = TerrainMaskClass::Shadow;
    else
        out.maskClass = TerrainMaskClass::Normal;
    return out;
}

double terrainRadiometricFactor( double localIncidenceDeg, double incidenceDeg )
{
    if ( !std::isfinite( localIncidenceDeg ) || !std::isfinite( incidenceDeg ) ||
         incidenceDeg <= 0.0 || incidenceDeg >= 90.0 )
        return kNaN;
    const double cosLocal = std::cos( localIncidenceDeg * kDegToRad );
    if ( cosLocal <= 1e-6 ) // grazing/overturned local geometry
        return kNaN;
    return std::cos( incidenceDeg * kDegToRad ) / cosLocal;
}

} // namespace sicnu::sar
