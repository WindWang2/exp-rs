// sar_topographic_phase.cpp — see sar_topographic_phase.h
#include "sar_topographic_phase.h"

#include <cmath>

namespace sicnu::sar
{

namespace
{
constexpr double kTwoPi = 2.0 * M_PI;

double wrapToPi( double phase )
{
    if ( !std::isfinite( phase ) )
        return phase;
    // fmod-based wrap into (−π, π]; deterministic for any finite input.
    double wrapped = std::fmod( phase + M_PI, kTwoPi );
    if ( wrapped < 0.0 )
        wrapped += kTwoPi;
    return wrapped - M_PI;
}
} // namespace

double wrapPhaseRad( double phaseRad )
{
    return wrapToPi( phaseRad );
}

bool topographicPhaseAtGround( const OrbitSegment &masterOrbit,
                               const OrbitSegment &slaveOrbit,
                               double wavelengthM,
                               const GeodeticPoint &ground,
                               double *phaseRad, QString *error )
{
    if ( phaseRad == nullptr )
        return false;
    const auto fail = [error]( const char *code, const QString &msg ) {
        if ( error )
            *error = QStringLiteral( "%1: %2" ).arg( code, msg );
        return false;
    };

    if ( !std::isfinite( wavelengthM ) || wavelengthM <= 0.0 )
        return fail( "TOPO_PHASE_METADATA_MISSING",
                     QStringLiteral( "wavelength (m) is missing, non-finite or <= 0" ) );
    if ( !masterOrbit.isValid() || !slaveOrbit.isValid() )
        return fail( "ORBIT_SEGMENT_INVALID",
                     QStringLiteral( "both scene orbits must be valid segments" ) );
    if ( !std::isfinite( ground.latDeg ) || !std::isfinite( ground.lonDeg )
         || !std::isfinite( ground.heightM ) )
        return fail( "TOPO_PHASE_METADATA_MISSING",
                     QStringLiteral( "ground point (lat/lon/height) is non-finite — "
                                     "an invalid DEM sample must stay NaN, never a "
                                     "guessed phase" ) );

    double rMaster = 0.0, azimuthMaster = 0.0;
    if ( !forwardRangeDoppler( masterOrbit, ground, &azimuthMaster, &rMaster ) )
        return fail( "BASELINE_NO_ZERO_DOPPLER_MASTER",
                     QStringLiteral( "master orbit has no zero-Doppler crossing at the "
                                     "DEM point (lat %1°, lon %2°, h %3 m)" )
                         .arg( ground.latDeg )
                         .arg( ground.lonDeg )
                         .arg( ground.heightM ) );
    // The doppler zeroes on BOTH sides of the planet (the far side is the
    // range MAXIMUM, also a zero of the range rate). A crossing whose range
    // reaches the sensor's own orbit radius is the far-side zero, not an
    // imaging crossing — refuse instead of emitting a meaningless phase.
    {
        double csx = 0.0, csy = 0.0, csz = 0.0, cvx = 0.0, cvy = 0.0, cvz = 0.0;
        if ( !interpolateState( masterOrbit, azimuthMaster, &csx, &csy, &csz, &cvx, &cvy,
                                &cvz ) )
            return fail( "BASELINE_STATE_INTERPOLATION_FAILED",
                         QStringLiteral( "master state interpolation failed at the "
                                         "crossing" ) );
        if ( rMaster >= std::sqrt( csx * csx + csy * csy + csz * csz ) )
            return fail( "BASELINE_NO_ZERO_DOPPLER_MASTER",
                         QStringLiteral( "the only in-segment zero-Doppler crossing for "
                                         "the DEM point is on the far side of the planet "
                                         "(range %1 m >= orbit radius) - the point is not "
                                         "imaged by this segment" )
                             .arg( rMaster ) );
    }

    double rSlave = 0.0, azimuthSlave = 0.0;
    if ( !forwardRangeDoppler( slaveOrbit, ground, &azimuthSlave, &rSlave ) )
        return fail( "BASELINE_NO_ZERO_DOPPLER_SLAVE",
                     QStringLiteral( "slave orbit has no zero-Doppler crossing at the "
                                     "DEM point (lat %1°, lon %2°, h %3 m)" )
                         .arg( ground.latDeg )
                         .arg( ground.lonDeg )
                         .arg( ground.heightM ) );
    {
        double csx = 0.0, csy = 0.0, csz = 0.0, cvx = 0.0, cvy = 0.0, cvz = 0.0;
        if ( !interpolateState( slaveOrbit, azimuthSlave, &csx, &csy, &csz, &cvx, &cvy,
                                &cvz ) )
            return fail( "BASELINE_STATE_INTERPOLATION_FAILED",
                         QStringLiteral( "slave state interpolation failed at the "
                                         "crossing" ) );
        if ( rSlave >= std::sqrt( csx * csx + csy * csy + csz * csz ) )
            return fail( "BASELINE_NO_ZERO_DOPPLER_SLAVE",
                         QStringLiteral( "the only in-segment zero-Doppler crossing for "
                                         "the DEM point is on the far side of the planet "
                                         "(range %1 m >= orbit radius) - the point is not "
                                         "imaged by this segment" )
                             .arg( rSlave ) );
    }


    *phaseRad = wrapToPi( -4.0 * M_PI * ( rMaster - rSlave ) / wavelengthM );
    return true;
}

void removeTopographicPhase( const double *ifgPhase, const double *topoPhase,
                             long long n, double *out, long long *nanCount )
{
    long long nanOut = 0;
    for ( long long i = 0; i < n; ++i )
    {
        const double a = ifgPhase[i];
        const double b = topoPhase[i];
        if ( !std::isfinite( a ) || !std::isfinite( b ) )
        {
            out[i] = std::numeric_limits<double>::quiet_NaN();
            ++nanOut;
            continue;
        }
        out[i] = wrapToPi( a - b );
    }
    if ( nanCount )
        *nanCount = nanOut;
}

} // namespace sicnu::sar
