// sar_geocoding.cpp — see sar_geocoding.h
#include "sar_geocoding.h"

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
// Two-way light path: range TIME (s) → slant range (m).
constexpr double kHalfLightSpeed = 299792458.0 / 2.0;

double clamp1( double v ) { return std::clamp( v, -1.0, 1.0 ); }
} // namespace

// -------------------------------------------------- scene contract ---------

bool parseSarSceneContract( const std::function<QString( const char * )> &metaItem,
                            int imageWidth, int imageHeight,
                            SarSceneContract *out, QString *error )
{
    const auto fail = [error]( const QString &msg ) {
        if ( error )
            *error = msg;
        return false;
    };
    if ( !out || imageWidth <= 0 || imageHeight <= 0 )
        return fail( QStringLiteral( "scene contract needs a non-empty raster" ) );

    const QString orbitStates = metaItem( "SICNU_SAR_ORBIT_STATES" );
    const QString azStart = metaItem( "SICNU_SAR_AZIMUTH_START_UTC" );
    const QString prf = metaItem( "SICNU_SAR_PRF" );
    const QString rangeWindow = metaItem( "SICNU_SAR_RANGE_WINDOW" );
    const QString rangeRate = metaItem( "SICNU_SAR_RANGE_RATE" );
    if ( orbitStates.isEmpty() || azStart.isEmpty() || prf.isEmpty()
         || rangeRate.isEmpty() || rangeWindow.isEmpty() )
        return fail( QStringLiteral(
            "the declared orbit contract is incomplete: geocoding requires "
            "SICNU_SAR_ORBIT_STATES, SICNU_SAR_AZIMUTH_START_UTC, SICNU_SAR_PRF, "
            "SICNU_SAR_RANGE_WINDOW and SICNU_SAR_RANGE_RATE; this scene does not "
            "declare it and the geometry is never approximated" ) );

    SarSceneContract c;
    c.imageWidth = imageWidth;
    c.imageHeight = imageHeight;

    QString orbitError;
    if ( !parseOrbitStates( orbitStates, &c.orbit, &orbitError ) )
        return fail( QStringLiteral( "SICNU_SAR_ORBIT_STATES is invalid: %1" ).arg( orbitError ) );

    bool ok = false;
    c.azimuthStartSeconds = azStart.toDouble( &ok );
    if ( !ok || !std::isfinite( c.azimuthStartSeconds ) )
        return fail( QStringLiteral( "SICNU_SAR_AZIMUTH_START_UTC is not a finite number" ) );

    c.prfHz = prf.toDouble( &ok );
    if ( !ok || !( c.prfHz > 0.0 ) )
        return fail( QStringLiteral( "SICNU_SAR_PRF must be > 0" ) );

    const double rangeRate = rangeRate.toDouble( &ok );
    if ( !ok || !( rangeRate > 0.0 ) )
        return fail( QStringLiteral( "SICNU_SAR_RANGE_RATE must be > 0" ) );
    c.rangeSampleSpacingM = kHalfLightSpeed / rangeRate;

    const QStringList windowFields = rangeWindow.split( QLatin1Char( ';' ) );
    if ( windowFields.size() != 2 )
        return fail( QStringLiteral( "SICNU_SAR_RANGE_WINDOW must be 'start;stop' in seconds" ) );
    bool okStart = false;
    bool okStop = false;
    const double rangeStartTime = windowFields[0].toDouble( &okStart );
    const double rangeStopTime = windowFields[1].toDouble( &okStop );
    if ( !okStart || !okStop || !std::isfinite( rangeStartTime )
         || !std::isfinite( rangeStopTime ) || !( rangeStopTime > rangeStartTime ) )
        return fail( QStringLiteral( "SICNU_SAR_RANGE_WINDOW must be 'start;stop' in seconds" ) );
    c.rangeStartM = rangeStartTime * kHalfLightSpeed;

    // Timing consistency: the window must cover the raster's columns within
    // the same tolerance the backward orbit product enforces.
    const double coveredSamples = ( rangeStopTime - rangeStartTime ) * rangeRate;
    if ( std::fabs( coveredSamples - ( imageWidth - 1 ) ) > 2.0 )
        return fail( QStringLiteral(
                         "SICNU_SAR_RANGE_WINDOW covers %1 range samples but the raster has %2 "
                         "columns (tolerance 2) - the declared timing contradicts the grid" )
                         .arg( coveredSamples )
                         .arg( imageWidth - 1 ) );

    *out = c;
    return true;
}

// ------------------------------------------------- per-cell geometry -------

bool geocodeGroundCell( const SarSceneContract &contract,
                        double latDeg, double lonDeg, double heightM,
                        double dzdE, double dzdN,
                        GeocodeGeometry *out )
{
    if ( out == nullptr )
        return false;
    *out = GeocodeGeometry{};

    const GeodeticPoint p{ latDeg, lonDeg, heightM };
    double azimuthTime = 0.0;
    double slantRange = 0.0;
    if ( !forwardRangeDoppler( contract.orbit, p, &azimuthTime, &slantRange ) )
        return false; // no zero-Doppler crossing inside the declared segment

    out->resolved = true;
    out->rowF = contract.rowOfAzimuthTime( azimuthTime );
    out->colF = contract.colOfSlantRange( slantRange );

    // Real line of sight: satellite position at the cell's own azimuth time,
    // ground point on the height shell.
    double sx, sy, sz;
    if ( !interpolateState( contract.orbit, azimuthTime, &sx, &sy, &sz, nullptr, nullptr, nullptr ) )
    {
        out->resolved = false;
        return false;
    }
    double px, py, pz;
    Wgs84::geodeticToEcef( latDeg, lonDeg, heightM, &px, &py, &pz );
    const double losx = sx - px, losy = sy - py, losz = sz - pz;
    const double losNorm = std::sqrt( losx * losx + losy * losy + losz * losz );
    if ( !( losNorm > 0.0 ) )
    {
        out->resolved = false;
        return false;
    }
    const double shx = losx / losNorm, shy = losy / losNorm, shz = losz / losNorm;

    // Local ENU basis at the ground point (geodetic latitude).
    const double lat = latDeg * kDegToRad;
    const double lon = lonDeg * kDegToRad;
    const double sinLat = std::sin( lat ), cosLat = std::cos( lat );
    const double sinLon = std::sin( lon ), cosLon = std::cos( lon );
    const double ex = -sinLon,        ey = cosLon,        ez = 0.0;               // east
    const double nx = -sinLat * cosLon, ny = -sinLat * sinLon, nz = cosLat;       // north
    const double ux = cosLat * cosLon, uy = cosLat * sinLon, uz = sinLat;        // up

    // Reference (ellipsoid) incidence: angle between the geodetic surface
    // normal and the line of sight — same definition as
    // sar_orbit.cpp incidenceAngleDeg, evaluated on the already-interpolated
    // state so one cell costs one orbit interpolation.
    const double nPrime = Wgs84::kSemiMajor
                          / std::sqrt( 1.0 - Wgs84::kEcc2 * sinLat * sinLat );
    const double nRefx = cosLat * cosLon, nRefy = cosLat * sinLon, nRefz = sinLat;
    const double cosTheta0 = ( nRefx * shx + nRefy * shy + nRefz * shz );
    out->incidenceDeg = std::acos( clamp1( cosTheta0 ) ) * kRadToDeg;

    // REAL look elevation above the local horizontal plane.
    out->lookElevationDeg = std::asin( clamp1( shx * ux + shy * uy + shz * uz ) ) * kRadToDeg;

    // Terrain-facet geometry: normal from the Horn gradients (metres per
    // metre east/north), rotated into ECEF through the ENU basis.
    const bool facetValid = std::isfinite( dzdE ) && std::isfinite( dzdN );
    if ( facetValid )
    {
        const double inv = 1.0 / std::sqrt( dzdE * dzdE + dzdN * dzdN + 1.0 );
        const double facetx = ( -dzdE * ex - dzdN * nx + ux ) * inv;
        const double facety = ( -dzdE * ey - dzdN * ny + uy ) * inv;
        const double facetz = ( -dzdE * ez - dzdN * nz + uz ) * inv;
        const double cosThetaL = facetx * shx + facety * shy + facetz * shz;
        out->localIncidenceDeg = std::acos( clamp1( cosThetaL ) ) * kRadToDeg;

        // Radiometric-terrain area factor (Ulander 1996): gamma0 = sigma0 ·
        // sin θ0 / sin θL. NaN when the facet is tilted at or past grazing
        // (sin θL ≤ 0) — the mask class below flags the same cells.
        const double sinTheta0 = std::sin( out->incidenceDeg * kDegToRad );
        const double sinThetaL = std::sin( out->localIncidenceDeg * kDegToRad );
        if ( sinThetaL > 0.0 && sinTheta0 >= 0.0 )
            out->rtcFactor = sinTheta0 / sinThetaL;
    }

    // Layover/shadow from the REAL per-pixel look elevation: slope angle
    // toward the radar along the horizontal look direction vs the elevation
    // cone. Degenerate (NaN) gradients leave the class Normal — callers mask
    // through the NaN geometry products, exactly like the constant-geometry
    // kernel.
    if ( facetValid )
    {
        double gx = shx * ex + shy * ey; // horizontal look direction (toward sensor)
        double gy = shx * nx + shy * ny;
        double gz = shx * ux + shy * uy; // up component of the look direction
        const double gHoriz = std::sqrt( gx * gx + gy * gy );
        if ( gHoriz > 1e-12 )
        {
            const double slopeToward = ( dzdE * gx + dzdN * gy ) / gHoriz; // dz per horizontal metre
            const double alphaDeg = std::atan( slopeToward ) * kRadToDeg;
            // LAYOVER: slope toward the radar steeper than the beam from
            // vertical (α > 90° − θe); SHADOW: far slope below the
            // illumination cone (α < −θe). Reduces to the constant-geometry
            // conditions when the LOS elevation is constant.
            if ( alphaDeg > 90.0 - out->lookElevationDeg )
                out->maskClass = TerrainMaskClass::Layover;
            else if ( alphaDeg < -out->lookElevationDeg )
                out->maskClass = TerrainMaskClass::Shadow;
        }
        (void)gz;
    }
    return true;
}

// --------------------------------------------------------- samplers --------

bool bilinearSample( const float *buf, int w, int h, double xF, double yF, float *out )
{
    if ( buf == nullptr || out == nullptr || w <= 0 || h <= 0 )
        return false;
    if ( !( xF >= 0.0 && xF <= w - 1.0 && yF >= 0.0 && yF <= h - 1.0 ) )
        return false;
    const double x0 = std::floor( xF );
    const double y0 = std::floor( yF );
    const double dx = xF - x0;
    const double dy = yF - y0;
    const int ix = static_cast<int>( x0 );
    const int iy = static_cast<int>( y0 );
    const int ix1 = std::min( ix + 1, w - 1 );
    const int iy1 = std::min( iy + 1, h - 1 );
    const float v00 = buf[static_cast<size_t>( iy ) * w + ix];
    const float v10 = buf[static_cast<size_t>( iy ) * w + ix1];
    const float v01 = buf[static_cast<size_t>( iy1 ) * w + ix];
    const float v11 = buf[static_cast<size_t>( iy1 ) * w + ix1];
    if ( !std::isfinite( v00 ) || !std::isfinite( v10 )
         || !std::isfinite( v01 ) || !std::isfinite( v11 ) )
        return false;
    const double top = v00 + ( v10 - v00 ) * dx;
    const double bottom = v01 + ( v11 - v01 ) * dx;
    *out = static_cast<float>( top + ( bottom - top ) * dy );
    return std::isfinite( *out );
}

bool nearestSample( const float *buf, int w, int h, double xF, double yF, float *out )
{
    if ( buf == nullptr || out == nullptr || w <= 0 || h <= 0 )
        return false;
    if ( !( xF >= 0.0 && xF <= w - 1.0 && yF >= 0.0 && yF <= h - 1.0 ) )
        return false;
    const int ix = static_cast<int>( std::floor( xF + 0.5 ) );
    const int iy = static_cast<int>( std::floor( yF + 0.5 ) );
    const float v = buf[static_cast<size_t>( iy ) * w + ix];
    if ( !std::isfinite( v ) )
        return false;
    *out = v;
    return true;
}

} // namespace sicnu::sar
