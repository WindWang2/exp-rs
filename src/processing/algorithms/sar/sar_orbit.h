// sar_orbit.h — orbit state-vector contract and zero-Doppler geometry
// (Scientific Algorithms 7.0, capability package A).
//
// This module consumes the additive extension contract documented in
// docs/processing/sar-domain.md §3: SICNU_SAR_ORBIT_STATES ("t;x;y;z;vx;vy;vz|…",
// UTC seconds relative to the scene azimuth epoch, WGS84 ECEF metres and m/s)
// plus SICNU_SAR_AZIMUTH_START_UTC (seconds in the same time base) mapping
// image rows to azimuth times. Under the declared contract the rigorous
// geometry chain is: orbit interpolation → per-row azimuth time → zero-Doppler
// geolocation / forward range-Doppler → per-pixel incidence and layover/shadow
// from real geometry. Scenes that do not declare (and pass validation of)
// these keys keep the constant-geometry subset and the typed refusals —
// nothing here approximates what the metadata cannot support.
//
// All math is double precision, deterministic, and known-answer tested
// against a synthetic circular orbit (tests/test_sar_orbit.cpp).
#pragma once

#include <QString>
#include <vector>

namespace sicnu::sar
{

/// WGS84 parameters and ECEF↔geodetic conversions shared by the orbit path.
struct Wgs84
{
    static constexpr double kSemiMajor = 6378137.0;               ///< a (m)
    static constexpr double kFlattening = 1.0 / 298.257223563;    ///< 1/f
    static constexpr double kEcc2 =
        kFlattening * ( 2.0 - kFlattening );                      ///< e² (first eccentricity squared)

    /// ECEF metres → geodetic (degrees, metres above the ellipsoid) via
    /// Bowring's closed form through the parametric angle (machine-precision,
    /// deterministic).
    static void ecefToGeodetic( double x, double y, double z,
                                double *latDeg, double *lonDeg, double *heightM );
    /// Geodetic (degrees, metres) → ECEF metres.
    static void geodeticToEcef( double latDeg, double lonDeg, double heightM,
                                double *x, double *y, double *z );
};

/// One state vector: UTC seconds on the declared time base, ECEF position
/// (m) and velocity (m/s).
struct OrbitStateVector
{
    double t = 0.0;
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
};

struct OrbitSegment
{
    std::vector<OrbitStateVector> states;

    bool empty() const { return states.empty(); }
    double startTime() const { return states.front().t; }
    double endTime() const { return states.back().t; }
    /// Contract validation: >= 2 states, strictly ascending times, all
    /// components finite, at least one non-degenerate velocity.
    bool isValid() const;
};

/// Parses the documented SICNU_SAR_ORBIT_STATES encoding
/// "t;x;y;z;vx;vy;vz|t;x;y;z;vx;vy;vz|…" (state-separator '|', field
/// separator ';'). Returns false with a human-readable @a error when any
/// record is malformed or the segment fails isValid() — typed callers
/// refuse instead of interpolating garbage.
bool parseOrbitStates( const QString &value, OrbitSegment *out, QString *error = nullptr );

/// Cubic Hermite interpolation of the platform state (position from the
/// position+velocity pair per axis) at @a t; false when @a t lies outside
/// the segment or the bracketing states are invalid. Continuity of
/// position and velocity across state samples makes the per-axis Hermite
/// form the standard segment-level orbit interpolator (accuracy ~1e-2 m
/// over 60 s Sentinel-1 gaps with real orbits; the tests pin the exact
/// circular-orbit recovery).
bool interpolateState( const OrbitSegment &orbit, double t,
                       double *x, double *y, double *z,
                       double *vx, double *vy, double *vz );

struct GeodeticPoint
{
    double latDeg = 0.0;
    double lonDeg = 0.0;
    double heightM = 0.0;
};

/// Zero-Doppler geolocation (image → ground): the ground point imaged at
/// @a azimuthTime with slant range @a slantRangeM lying on the surface
/// shell at @a height above the WGS84 ellipsoid. Solves the Doppler plane
/// ∩ range sphere ∩ height-shell system by Newton iteration on the
/// in-plane angle; false when the azimuth time is outside the orbit
/// segment or no solution converges (callers must refuse, not guess).
bool geolocateZeroDoppler( const OrbitSegment &orbit, double azimuthTime,
                           double slantRangeM, double heightM, GeodeticPoint *out );

/// Forward range-Doppler (ground → image): the zero-Doppler azimuth time
/// and slant range the sensor records for the ground point @a p.
/// Newton iteration on the Doppler equation (the crossing is unique for
/// near-Earth orbits inside the segment); false outside the segment.
bool forwardRangeDoppler( const OrbitSegment &orbit, const GeodeticPoint &p,
                          double *azimuthTime, double *slantRangeM );

/// Local incidence angle (degrees, [0, 180]) at the ground point for the
/// given azimuth time: the angle between the surface normal at @a p
/// (ellipsoidal, using the geodetic latitude — the DEM gradient provides
/// the local perturbation upstream) and the line of sight P→P_sat.
/// NaN outside the segment.
double incidenceAngleDeg( const OrbitSegment &orbit, double azimuthTime, const GeodeticPoint &p );

} // namespace sicnu::sar
