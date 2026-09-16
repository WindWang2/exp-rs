// sar_baseline.cpp — see sar_baseline.h
#include "sar_baseline.h"

#include <cmath>

namespace sicnu::sar
{

namespace
{
constexpr double kDaySeconds = 86400.0;

const QString &failCode( QString *error, const QString &code, const QString &message )
{
    static const QString kEmpty;
    if ( error )
        *error = code + QStringLiteral( ": " ) + message;
    return kEmpty;
}
} // namespace

// ------------------------------------------------------ scene truth -------

bool validateSceneTruth( const InSarSceneTruth &scene, QString *error )
{
    if ( !std::isfinite( scene.acquisitionUtcSec ) )
    {
        failCode( error, QStringLiteral( "SCENE_TRUTH_INVALID" ),
                  QStringLiteral( "acquisition UTC is missing or non-finite" ) );
        return false;
    }
    if ( !std::isfinite( scene.wavelengthUm ) || scene.wavelengthUm <= 0.0 )
    {
        failCode( error, QStringLiteral( "SCENE_TRUTH_INVALID" ),
                  QStringLiteral( "wavelength (µm) is missing, non-finite or <= 0" ) );
        return false;
    }
    if ( !scene.orbit.isValid() )
    {
        failCode( error, QStringLiteral( "ORBIT_SEGMENT_INVALID" ),
                  QStringLiteral( "orbit segment needs >= 2 states with strictly "
                                  "ascending finite times and non-degenerate velocity" ) );
        return false;
    }
    return true;
}

bool buildPairTruth( const InSarSceneTruth &master, const InSarSceneTruth &slave,
                     InSarPairTruth *out, QString *error )
{
    if ( out == nullptr )
        return false;
    if ( !validateSceneTruth( master, error ) )
        return false;
    if ( !validateSceneTruth( slave, error ) )
        return false;

    const double relDiff = std::abs( master.wavelengthUm - slave.wavelengthUm )
                           / std::max( master.wavelengthUm, slave.wavelengthUm );
    if ( relDiff > 1e-9 )
    {
        failCode( error, QStringLiteral( "WAVELENGTH_INCOMPATIBLE" ),
                  QStringLiteral( "master %1 µm vs slave %2 µm (relative %3 > 1e-9) — "
                                  "interferometric phase is only defined between "
                                  "co-nominal radars" )
                      .arg( master.wavelengthUm )
                      .arg( slave.wavelengthUm )
                      .arg( relDiff ) );
        return false;
    }

    out->master = master;
    out->slave = slave;
    out->temporalDays = ( slave.acquisitionUtcSec - master.acquisitionUtcSec ) / kDaySeconds;
    out->orbitsShareAbsoluteBase = false;
    out->truthVersion = kInSarTruthVersion;

    if ( std::isfinite( master.azimuthStartUtcSec ) && std::isfinite( slave.azimuthStartUtcSec ) )
    {
        // Absolute windows of the two orbit segments; a common window is a
        // necessary (not sufficient) condition for a common imaged area.
        const double mStart = master.azimuthStartUtcSec + master.orbit.startTime();
        const double mEnd = master.azimuthStartUtcSec + master.orbit.endTime();
        const double sStart = slave.azimuthStartUtcSec + slave.orbit.startTime();
        const double sEnd = slave.azimuthStartUtcSec + slave.orbit.endTime();
        if ( std::max( mStart, sStart ) >= std::min( mEnd, sEnd ) )
        {
            failCode( error, QStringLiteral( "ORBIT_EPOCH_MISMATCH" ),
                      QStringLiteral( "absolute orbit windows do not overlap "
                                      "(master [%1, %2] vs slave [%3, %4] UTC) — the "
                                      "acquisitions cannot share an imaged area" )
                          .arg( mStart )
                          .arg( mEnd )
                          .arg( sStart )
                          .arg( sEnd ) );
            return false;
        }
        out->orbitsShareAbsoluteBase = true;
    }
    return true;
}

// -------------------------------------------------- pair baseline ---------

bool pairBaselineAtGround( const InSarPairTruth &pair,
                           const GeodeticPoint &ground,
                           PairBaselineSample *out, QString *error )
{
    if ( out == nullptr )
        return false;

    double t1 = kUnset, r1 = kUnset;
    if ( !forwardRangeDoppler( pair.master.orbit, ground, &t1, &r1 ) )
    {
        failCode( error, QStringLiteral( "BASELINE_NO_ZERO_DOPPLER_MASTER" ),
                  QStringLiteral( "master orbit has no zero-Doppler crossing at the "
                                  "ground point (lat %1°, lon %2°, h %3 m)" )
                      .arg( ground.latDeg )
                      .arg( ground.lonDeg )
                      .arg( ground.heightM ) );
        return false;
    }
    double t2 = kUnset, r2 = kUnset;
    if ( !forwardRangeDoppler( pair.slave.orbit, ground, &t2, &r2 ) )
    {
        failCode( error, QStringLiteral( "BASELINE_NO_ZERO_DOPPLER_SLAVE" ),
                  QStringLiteral( "slave orbit has no zero-Doppler crossing at the "
                                  "ground point (lat %1°, lon %2°, h %3 m)" )
                      .arg( ground.latDeg )
                      .arg( ground.lonDeg )
                      .arg( ground.heightM ) );
        return false;
    }

    double x1 = 0.0, y1 = 0.0, z1 = 0.0, vx = 0.0, vy = 0.0, vz = 0.0;
    if ( !interpolateState( pair.master.orbit, t1, &x1, &y1, &z1, &vx, &vy, &vz ) )
    {
        failCode( error, QStringLiteral( "BASELINE_STATE_INTERPOLATION_FAILED" ),
                  QStringLiteral( "master state interpolation failed at t=%1" ).arg( t1 ) );
        return false;
    }
    // Near-side validation: the doppler also zeroes on the FAR side of the
    // planet (the range maximum). A crossing at or beyond the sensor's own
    // orbit radius is that far-side zero, not an imaging crossing.
    if ( r1 >= std::sqrt( x1 * x1 + y1 * y1 + z1 * z1 ) )
    {
        failCode( error, QStringLiteral( "BASELINE_NO_ZERO_DOPPLER_MASTER" ),
                  QStringLiteral( "the only in-segment crossing for the ground point is "
                                  "on the far side of the planet — the point is not "
                                  "imaged by this master segment" ) );
        return false;
    }
    double x2 = 0.0, y2 = 0.0, z2 = 0.0;
    if ( !interpolateState( pair.slave.orbit, t2, &x2, &y2, &z2, &vx, &vy, &vz ) )
    {
        failCode( error, QStringLiteral( "BASELINE_STATE_INTERPOLATION_FAILED" ),
                  QStringLiteral( "slave state interpolation failed at t=%1" ).arg( t2 ) );
        return false;
    }
    if ( r2 >= std::sqrt( x2 * x2 + y2 * y2 + z2 * z2 ) )
    {
        failCode( error, QStringLiteral( "BASELINE_NO_ZERO_DOPPLER_SLAVE" ),
                  QStringLiteral( "the only in-segment crossing for the ground point is "
                                  "on the far side of the planet — the point is not "
                                  "imaged by this slave segment" ) );
        return false;
    }

    double gx = 0.0, gy = 0.0, gz = 0.0;
    Wgs84::geodeticToEcef( ground.latDeg, ground.lonDeg, ground.heightM, &gx, &gy, &gz );

    // Unit LOS ground → master sensor (the interferometricBaseline contract).
    const double dx = x1 - gx, dy = y1 - gy, dz = z1 - gz;
    const double losNorm = std::sqrt( dx * dx + dy * dy + dz * dz );
    if ( !( losNorm > 0.0 ) )
    {
        failCode( error, QStringLiteral( "BASELINE_STATE_INTERPOLATION_FAILED" ),
                  QStringLiteral( "degenerate LOS (sensor at the ground point)" ) );
        return false;
    }

    InterferometricBaseline baseline;
    if ( !interferometricBaseline( x1, y1, z1, x2, y2, z2,
                                   dx / losNorm, dy / losNorm, dz / losNorm, &baseline ) )
    {
        failCode( error, QStringLiteral( "BASELINE_STATE_INTERPOLATION_FAILED" ),
                  QStringLiteral( "interferometricBaseline refused the pair geometry" ) );
        return false;
    }

    out->parallelM = baseline.parallelM;
    out->perpendicularM = baseline.perpendicularM;
    out->magnitudeM = baseline.magnitudeM;
    out->rangeMasterM = r1;
    out->rangeSlaveM = r2;
    out->azimuthTimeMaster = t1;
    out->azimuthTimeSlave = t2;
    return true;
}

double heightAmbiguityM( double perpendicularM, double slantRangeM,
                         double incidenceDeg, double wavelengthM,
                         QString *error )
{
    if ( !std::isfinite( perpendicularM ) || std::abs( perpendicularM ) < 1e-9
         || !std::isfinite( slantRangeM ) || slantRangeM <= 0.0
         || !std::isfinite( incidenceDeg ) || !std::isfinite( wavelengthM )
         || wavelengthM <= 0.0 )
    {
        failCode( error, QStringLiteral( "SCENE_TRUTH_INVALID" ),
                  QStringLiteral( "height ambiguity is undefined for |B⊥|=%1, "
                                  "range=%2, incidence=%3°, λ=%4 m (needs |B⊥| > 0 "
                                  "and positive range/λ)" )
                      .arg( perpendicularM )
                      .arg( slantRangeM )
                      .arg( incidenceDeg )
                      .arg( wavelengthM ) );
        return kUnset;
    }
    const double sinTheta = std::sin( incidenceDeg * M_PI / 180.0 );
    if ( !( sinTheta > 0.0 ) )
    {
        failCode( error, QStringLiteral( "SCENE_TRUTH_INVALID" ),
                  QStringLiteral( "height ambiguity is undefined at incidence %1° "
                                  "(sin θ <= 0)" )
                      .arg( incidenceDeg ) );
        return kUnset;
    }
    return wavelengthM * slantRangeM * sinTheta / ( 2.0 * std::abs( perpendicularM ) );
}

} // namespace sicnu::sar
