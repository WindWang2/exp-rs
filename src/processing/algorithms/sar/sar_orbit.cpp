// sar_orbit.cpp — see sar_orbit.h
#include "sar_orbit.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double norm( double x, double y, double z ) { return std::sqrt( x * x + y * y + z * z ); }
} // namespace

// ------------------------------------------------------------- Wgs84 -------

void Wgs84::geodeticToEcef( double latDeg, double lonDeg, double heightM,
                            double *x, double *y, double *z )
{
    const double lat = latDeg * kDegToRad;
    const double lon = lonDeg * kDegToRad;
    const double sinLat = std::sin( lat );
    const double cosLat = std::cos( lat );
    // Prime-vertical radius of curvature.
    const double n = kSemiMajor / std::sqrt( 1.0 - kEcc2 * sinLat * sinLat );
    *x = ( n + heightM ) * cosLat * std::cos( lon );
    *y = ( n + heightM ) * cosLat * std::sin( lon );
    *z = ( n * ( 1.0 - kEcc2 ) + heightM ) * sinLat;
}

void Wgs84::ecefToGeodetic( double x, double y, double z,
                            double *latDeg, double *lonDeg, double *heightM )
{
    const double lon = std::atan2( y, x );
    const double p = std::hypot( x, y );
    // Bowring's closed form through the parametric angle: machine-precision
    // in one step, deterministic, no iteration-tolerance ambiguity (an
    // in-house iteration variant here carried a latitude-dependent bias of
    // up to 0.3 deg, which the geolocation round-trip test exposed).
    const double b = kSemiMajor * ( 1.0 - kFlattening );
    const double eps2 = kEcc2 / ( 1.0 - kEcc2 );
    const double theta = std::atan2( z * kSemiMajor, p * b );
    const double sinT = std::sin( theta );
    const double cosT = std::cos( theta );
    const double lat = std::atan2( z + eps2 * b * sinT * sinT * sinT,
                                   p - kEcc2 * kSemiMajor * cosT * cosT * cosT );
    const double sinLat = std::sin( lat );
    const double n = kSemiMajor / std::sqrt( 1.0 - kEcc2 * sinLat * sinLat );
    const double height = p / std::cos( lat ) - n;
    *latDeg = lat * kRadToDeg;
    *lonDeg = lon * kRadToDeg;
    *heightM = height;
}

// ------------------------------------------------------------ OrbitSegment -

bool OrbitSegment::isValid() const
{
    if ( states.size() < 2 )
        return false;
    bool anyVelocity = false;
    for ( size_t i = 0; i < states.size(); ++i )
    {
        const OrbitStateVector &s = states[i];
        if ( !std::isfinite( s.t ) || !std::isfinite( s.x ) || !std::isfinite( s.y )
             || !std::isfinite( s.z ) || !std::isfinite( s.vx ) || !std::isfinite( s.vy )
             || !std::isfinite( s.vz ) )
            return false;
        if ( i > 0 && !( s.t > states[i - 1].t ) )
            return false;
        if ( norm( s.vx, s.vy, s.vz ) > 0.0 )
            anyVelocity = true;
    }
    return anyVelocity;
}

bool parseOrbitStates( const QString &value, OrbitSegment *out, QString *error )
{
    const auto fail = [error]( const QString &msg ) {
        if ( error )
            *error = msg;
        return false;
    };
    if ( out == nullptr )
        return fail( QStringLiteral( "output segment is null" ) );
    out->states.clear();

    const QStringList records = value.split( QLatin1Char( '|' ) );
    for ( const QString &record : records )
    {
        // An empty record ("state||state") is a malformed encoding, not
        // something to skip: the header promises refusal.
        if ( record.isEmpty() )
            return fail( QStringLiteral( "empty state record (double '|'?')" ) );
        const QStringList fields =
            record.split( QLatin1Char( ';' ), Qt::KeepEmptyParts );
        if ( fields.size() != 7 )
            return fail( QStringLiteral( "state record needs 7 fields 't;x;y;z;vx;vy;vz' (got %1)" )
                             .arg( fields.size() ) );
        OrbitStateVector s;
        bool ok = true;
        double *targets[7] = { &s.t, &s.x, &s.y, &s.z, &s.vx, &s.vy, &s.vz };
        for ( int f = 0; f < 7; ++f )
        {
            *targets[f] = fields[f].toDouble( &ok );
            if ( !ok )
                return fail( QStringLiteral( "state field %1 is not a number" ).arg( f ) );
        }
        out->states.push_back( s );
    }
    if ( !out->isValid() )
    {
        out->states.clear();
        return fail( QStringLiteral( "orbit segment invalid: needs >= 2 states with "
                                     "strictly ascending finite times and velocities" ) );
    }
    return true;
}

// ------------------------------------------------------- interpolation -----

bool interpolateState( const OrbitSegment &orbit, double t,
                       double *x, double *y, double *z,
                       double *vx, double *vy, double *vz )
{
    const std::vector<OrbitStateVector> &s = orbit.states;
    if ( s.size() < 2 || t < orbit.startTime() || t > orbit.endTime() )
        return false;

    // Find the bracketing states (binary search on the ascending times).
    size_t low = 0;
    size_t high = s.size() - 1;
    while ( low + 1 < high )
    {
        const size_t mid = ( low + high ) / 2;
        if ( s[mid].t <= t )
            low = mid;
        else
            high = mid;
    }
    const double t0 = s[low].t;
    const double t1 = s[high].t;
    const double h = t1 - t0;
    const double u = ( t - t0 ) / h;

    // Cubic Hermite per axis using position + velocity at both ends
    // (velocity scaled by the knot spacing per the Hermite basis).
    auto hermite = [u, h]( double p0, double p1, double v0, double v1 ) {
        const double u2 = u * u;
        const double u3 = u2 * u;
        return ( 2.0 * u3 - 3.0 * u2 + 1.0 ) * p0 + ( u3 - 2.0 * u2 + u ) * h * v0
               + ( -2.0 * u3 + 3.0 * u2 ) * p1 + ( u3 - u2 ) * h * v1;
    };

    const OrbitStateVector &a = s[low];
    const OrbitStateVector &b = s[high];
    if ( x ) *x = hermite( a.x, b.x, a.vx, b.vx );
    if ( y ) *y = hermite( a.y, b.y, a.vy, b.vy );
    if ( z ) *z = hermite( a.z, b.z, a.vz, b.vz );
    if ( vx || vy || vz )
    {
        // Derivative of the Hermite basis (exact for position; velocity is
        // recovered consistently for near-linear segments).
        auto hermiteDeriv = [u, h]( double p0, double p1, double v0, double v1 ) {
            const double u2 = u * u;
            return ( 6.0 * u2 - 6.0 * u ) * ( p0 - p1 ) / h
                   + ( 3.0 * u2 - 4.0 * u + 1.0 ) * v0
                   + ( 3.0 * u2 - 2.0 * u ) * v1;
        };
        if ( vx ) *vx = hermiteDeriv( a.x, b.x, a.vx, b.vx );
        if ( vy ) *vy = hermiteDeriv( a.y, b.y, a.vy, b.vy );
        if ( vz ) *vz = hermiteDeriv( a.z, b.z, a.vz, b.vz );
    }
    return true;
}

// -------------------------------------------------------- zero-Doppler -----

bool geolocateZeroDoppler( const OrbitSegment &orbit, double azimuthTime,
                           double slantRangeM, double heightM, GeodeticPoint *out )
{
    double sx, sy, sz, vx, vy, vz;
    if ( !interpolateState( orbit, azimuthTime, &sx, &sy, &sz, &vx, &vy, &vz ) )
        return false;
    if ( !( slantRangeM > 0.0 ) || out == nullptr )
        return false;

    // Doppler plane {P : (P − P_sat)·V = 0} with normal V̂; the range sphere
    // intersects it in a circle P(u) = P_sat + r·(cos u · f1 + sin u · f2)
    // with the IN-PLANE orthonormal basis
    //   f1 = normalize(P_sat − (P_sat·n̂)n̂)   (radial projection)
    //   f2 = n̂ × f1                            (along-track sense)
    // (V itself is the plane NORMAL, never an in-plane direction.)
    const double vNorm = norm( vx, vy, vz );
    if ( !( vNorm > 0.0 ) )
        return false;
    const double nx = vx / vNorm, ny = vy / vNorm, nz = vz / vNorm;
    const double satNorm = norm( sx, sy, sz );
    if ( !( satNorm > 0.0 ) )
        return false;
    const double radial = ( sx * nx + sy * ny + sz * nz ) / satNorm;
    double f1x = sx / satNorm - radial * nx;
    double f1y = sy / satNorm - radial * ny;
    double f1z = sz / satNorm - radial * nz;
    const double f1Norm = norm( f1x, f1y, f1z );
    if ( !( f1Norm > 1e-9 ) )
        return false; // degenerate projection (orbit along the normal)
    f1x /= f1Norm; f1y /= f1Norm; f1z /= f1Norm;
    // f2 = n̂ × f1 (already unit length).
    const double e2x = ny * f1z - nz * f1y;
    const double e2y = nz * f1x - nx * f1z;
    const double e2z = nx * f1y - ny * f1x;

    auto heightOf = []( double px, double py, double pz ) {
        double lat, lon, h;
        Wgs84::ecefToGeodetic( px, py, pz, &lat, &lon, &h );
        return h;
    };

    // Start Newton at the Earth-facing circle point (u = π: P_sat − r·f1);
    // the height function is monotone around that minimum.
    double u = M_PI;

    bool converged = false;
    double px = 0.0, py = 0.0, pz = 0.0;
    for ( int it = 0; it < 60; ++it )
    {
        const double cu = std::cos( u ), su = std::sin( u );
        px = sx + slantRangeM * ( cu * f1x + su * e2x );
        py = sy + slantRangeM * ( cu * f1y + su * e2y );
        pz = sz + slantRangeM * ( cu * f1z + su * e2z );
        const double h = heightOf( px, py, pz );
        const double residual = h - heightM;
        if ( std::fabs( residual ) < 1e-6 )
        {
            converged = true;
            break;
        }
        // dh/du via central difference on the circle (the ellipsoid height
        // along the circle is smooth and monotone across the solution).
        constexpr double kDu = 1e-7;
        const double cu1 = std::cos( u + kDu ), su1 = std::sin( u + kDu );
        const double cu0 = std::cos( u - kDu ), su0 = std::sin( u - kDu );
        const double hPlus = heightOf( sx + slantRangeM * ( cu1 * f1x + su1 * e2x ),
                                       sy + slantRangeM * ( cu1 * f1y + su1 * e2y ),
                                       sz + slantRangeM * ( cu1 * f1z + su1 * e2z ) );
        const double hMinus = heightOf( sx + slantRangeM * ( cu0 * f1x + su0 * e2x ),
                                        sy + slantRangeM * ( cu0 * f1y + su0 * e2y ),
                                        sz + slantRangeM * ( cu0 * f1z + su0 * e2z ) );
        const double dh = ( hPlus - hMinus ) / ( 2.0 * kDu );
        if ( !std::isfinite( dh ) )
            break;
        if ( std::fabs( dh ) < 1e-9 )
        {
            // u = π is the symmetric height minimum along the circle (the
            // central difference vanishes there exactly): nudge off the
            // symmetry point along +e2 and let Newton climb to the shell
            // crossing on that side (deterministic; the e2 direction is
            // orbit-dependent, either crossing is a valid solution).
            u += 0.1;
            continue;
        }
        double uNew = u - residual / dh;
        uNew = std::clamp( uNew, u - 0.5, u + 0.5 );
        u = uNew;
    }
    if ( !converged )
    {
        // Accept the last iterate only when the residual is tightly finite
        // (fail-closed: NaN height compares false against any threshold and
        // would otherwise be accepted as a "solution").
        if ( !( std::fabs( heightOf( px, py, pz ) - heightM ) <= 1e-4 ) )
            return false;
    }

    double lat, lon, h;
    Wgs84::ecefToGeodetic( px, py, pz, &lat, &lon, &h );
    out->latDeg = lat;
    out->lonDeg = lon;
    out->heightM = heightM;
    return true;
}

namespace
{
bool finishForward( const OrbitSegment &orbit, double t, double px, double py, double pz,
                    double *azimuthTime, double *slantRangeM )
{
    double sx, sy, sz;
    if ( !interpolateState( orbit, t, &sx, &sy, &sz, nullptr, nullptr, nullptr ) )
        return false;
    *azimuthTime = t;
    *slantRangeM = norm( sx - px, sy - py, sz - pz );
    return true;
}
} // namespace

bool forwardRangeDoppler( const OrbitSegment &orbit, const GeodeticPoint &p,
                          double *azimuthTime, double *slantRangeM )
{
    if ( orbit.empty() || azimuthTime == nullptr || slantRangeM == nullptr )
        return false;
    double px, py, pz;
    Wgs84::geodeticToEcef( p.latDeg, p.lonDeg, p.heightM, &px, &py, &pz );

    // Zero-Doppler crossing: f(t) = (P_sat(t) − P)·V_sat(t) = 0. A plain
    // Newton from the segment midpoint can march away from the crossing
    // (f is only locally monotone), so bracket the segment by sign scan
    // first and bisect inside the bracketing interval — deterministic and
    // global for the unique near-Earth crossing.
    auto dopplerAt = [&]( double t, double *value ) -> bool {
        double sx, sy, sz, vx, vy, vz;
        if ( !interpolateState( orbit, t, &sx, &sy, &sz, &vx, &vy, &vz ) )
            return false;
        *value = ( sx - px ) * vx + ( sy - py ) * vy + ( sz - pz ) * vz;
        return true;
    };

    constexpr int kScanSteps = 32;
    const double tLo = orbit.startTime();
    const double tHi = orbit.endTime();
    const double step = ( tHi - tLo ) / kScanSteps;
    double a = tLo;
    double fA = 0.0;
    if ( !dopplerAt( a, &fA ) )
        return false;
    double b = a;
    double fB = fA;
    bool bracketed = false;
    for ( int i = 0; i < kScanSteps; ++i )
    {
        b = ( i == kScanSteps - 1 ) ? tHi : tLo + step * ( i + 1 );
        if ( !dopplerAt( b, &fB ) )
            return false;
        if ( fA == 0.0 || ( fA < 0.0 ) != ( fB < 0.0 ) )
        {
            bracketed = true;
            break;
        }
        a = b;
        fA = fB;
    }
    if ( !bracketed )
        return false; // no zero-Doppler crossing inside the segment

    // Bisection to the machine limit (60 halvings; each is a factor 2 —
    // the bracket shrinks below any double-resolution long before).
    for ( int it = 0; it < 60; ++it )
    {
        const double m = 0.5 * ( a + b );
        double fM = 0.0;
        if ( !dopplerAt( m, &fM ) )
            return false;
        if ( fM == 0.0 || ( b - a ) < 1e-12 )
            return finishForward( orbit, m, px, py, pz, azimuthTime, slantRangeM );
        if ( ( fA < 0.0 ) != ( fM < 0.0 ) )
        {
            b = m;
            fB = fM;
        }
        else
        {
            a = m;
            fA = fM;
        }
    }
    return finishForward( orbit, 0.5 * ( a + b ), px, py, pz, azimuthTime, slantRangeM );
}

double incidenceAngleDeg( const OrbitSegment &orbit, double azimuthTime, const GeodeticPoint &p )
{
    double sx, sy, sz;
    if ( !interpolateState( orbit, azimuthTime, &sx, &sy, &sz, nullptr, nullptr, nullptr ) )
        return kNaN;
    double px, py, pz;
    Wgs84::geodeticToEcef( p.latDeg, p.lonDeg, p.heightM, &px, &py, &pz );
    // Angle between the ellipsoidal surface normal at p and the line of
    // sight p → P_sat: cos θi = n̂·(P_sat − p)/|P_sat − p| where the normal
    // uses the geodetic (not geocentric) latitude.
    const double lat = p.latDeg * kDegToRad;
    const double lon = p.lonDeg * kDegToRad;
    const double sinLat = std::sin( lat );
    const double cosLat = std::cos( lat );
    const double n = Wgs84::kSemiMajor / std::sqrt( 1.0 - Wgs84::kEcc2 * sinLat * sinLat );
    double nx = cosLat * std::cos( lon ), ny = cosLat * std::sin( lon ), nz = sinLat;
    const double losx = sx - px, losy = sy - py, losz = sz - pz;
    const double losNorm = norm( losx, losy, losz );
    if ( !( losNorm > 0.0 ) )
        return kNaN;
    const double cosTheta = ( nx * losx + ny * losy + nz * losz ) / losNorm;
    return std::acos( std::clamp( cosTheta, -1.0, 1.0 ) ) * kRadToDeg;
}

} // namespace sicnu::sar
