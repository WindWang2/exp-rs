// sar_terrain_geometry.h — per-pixel SAR terrain geometry under the declared
// constant-geometry contract (Foundation 5.0, Milestone D.3).
//
// SCOPE (honest subset): with only a declared constant incidence angle
// (SICNU_SAR_INCIDENCE_DEG) and platform heading (SICNU_SAR_HEADING_DEG),
// the rigorously computable terrain products are the LOCAL INCIDENCE ANGLE
// and the geometric LAYOVER/SHADOW classes — both follow from the surface
// normal and the look vector alone. FULL range-Doppler terrain correction /
// geocoding additionally needs orbit state vectors and sensor timing
// (zero-Doppler range equations, DEM sampling in range time); the current
// generic product metadata contract does not carry them, so operators MUST
// refuse such requests with a typed error instead of approximating them.
// Additive extension contract for future providers (documented, not yet
// consumed): dataset metadata keys
//   SICNU_SAR_ORBIT_STATES      — "t;x;y;z;vx;vy;vz|..." (UTC seconds, metres, m/s)
//   SICNU_SAR_RANGE_WINDOW      — range gate start/stop (s)
//   SICNU_SAR_PRF / SICNU_SAR_RANGE_RATE — sampling contract
// When these are declared AND validated, a provider may extend this module
// with zero-Doppler solving; nothing in this file approximates it.
//
// Geometry (surface-normal form; all gradients in metres/metre, Horn kernels
// shared with the terrain/topographic families):
//   look azimuth φh (deg, clockwise from north — the direction the beam
//   travels); incidence θi (deg from vertical at the pixel);
//   n = (−gE, −gN, 1)/|·|      surface normal (E, N, Z)
//   v = (sinθi·sinφh, sinθi·cosφh, −cosθi)   unit look vector (downward)
//   cosθl = (−v)·n            local incidence angle
//   gTowardSensor = −(gE·sinφh + gN·cosφh)   terrain slope toward the radar
//   α = atan(gTowardSensor)   signed range-direction slope angle
// Classes (classic geometric masks, Small 2011 form):
//   LAYOVER  when α > θi            (slope faces the radar steeper than the look)
//   SHADOW   when α < θi − 90°      (far side steeper than the illumination cone)
//   NORMAL   otherwise
// Degenerate gradient neighbourhoods (any NaN) yield NaN output — the caller
// masks them as NoData, never as a valid geometry.
#pragma once

namespace sicnu::sar
{

enum class TerrainMaskClass
{
    Normal = 0,
    Layover = 1,
    Shadow = 2,
};

struct TerrainGeometryResult
{
    double localIncidenceDeg = 0.0; ///< NaN when the gradient is degenerate
    TerrainMaskClass maskClass = TerrainMaskClass::Normal;
};

/// Computes the local incidence angle and the geometric mask class for one
/// pixel. @a dzdx is dz/dEast, @a dzdy dz/dNorth (metres/metre),
/// @a incidenceDeg ∈ (0, 90), @a lookAzimuthDeg = antenna look azimuth
/// (boresight ground azimuth) clockwise from north — NOT the platform
/// flight heading: heading and look azimuth are orthogonal (#785). Use
/// lookAzimuthFromHeading() to derive the boresight from the flight
/// direction and the antenna side. NaN gradients → NaN localIncidenceDeg
/// (class Normal, caller masks).
TerrainGeometryResult terrainGeometry( double dzdx, double dzdy,
                                       double incidenceDeg, double lookAzimuthDeg );

/// Antenna look azimuth (degrees clockwise from north, normalized to
/// [0, 360)) from the platform flight heading and the antenna side:
/// right-looking sensors bore heading + 90°, left-looking heading − 90°.
/// (#785: operators used to feed the heading itself, a 90° orthogonal
/// error that invalidated masks and radiometric flattening.)
double lookAzimuthFromHeading( double headingDeg, bool rightLooking );

/// Radiometric terrain factor cos(θi)/cos(θl) for the pixel (the standard
/// first-order terrain normalization ratio; consumers apply it to already
/// calibrated backscatter). NaN under the same degenerate conditions.
double terrainRadiometricFactor( double localIncidenceDeg, double incidenceDeg );

} // namespace sicnu::sar
