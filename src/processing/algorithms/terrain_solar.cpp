// terrain_solar.cpp — see terrain_solar.h for contracts.

#include "terrain_solar.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace TerrainSolar
{
namespace
{

inline bool isMissing( float z, float nodata )
{
    return z == nodata || std::isnan( z );
}

constexpr double kDegToRad = M_PI / 180.0;

} // namespace

bool shadowDuration( const float *dem, int width, int height, float nodata,
                     double cellSizeX, double cellSizeY,
                     const std::vector<SunSample> &track,
                     ShadowDurationResult *out,
                     const std::function<bool()> &cancelled )
{
    if ( !dem || !out || width <= 0 || height <= 0 || cellSizeX <= 0.0
         || cellSizeY <= 0.0 )
        return false;
    if ( cancelled && cancelled() )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;

    // Accept daylight, weighted samples; quantize azimuth to 1° sectors so a
    // single sweep per sector serves every sample in it.
    std::vector<std::vector<std::size_t>> sectorSamples( 360 );
    double weightSum = 0.0;
    std::size_t accepted = 0;
    for ( std::size_t k = 0; k < track.size(); ++k )
    {
        const SunSample &s = track[k];
        if ( !std::isfinite( s.azimuthDeg ) || !std::isfinite( s.elevationDeg )
             || !std::isfinite( s.weight ) )
            continue;
        if ( s.elevationDeg <= 0.0 || s.elevationDeg > 90.0 || s.weight <= 0.0 )
            continue;
        int sector = static_cast<int>( std::floor( std::fmod( s.azimuthDeg, 360.0 ) ) );
        if ( sector < 0 )
            sector += 360;
        if ( sector >= 360 )
            sector = 359;
        sectorSamples[static_cast<std::size_t>( sector )].push_back( k );
        weightSum += s.weight;
        ++accepted;
    }
    if ( accepted == 0 )
        return false;

    out->shadowFraction.assign( n, 0.0f );
    for ( std::size_t i = 0; i < n; ++i )
        if ( isMissing( dem[i], nodata ) )
            out->shadowFraction[i] = nodata;

    std::vector<double> shadowAccum( n, 0.0 );
    std::vector<std::int32_t> stamp( n, -1 );
    std::vector<std::uint32_t> rayCells;
    std::vector<double> rayPos;
    rayCells.reserve( static_cast<std::size_t>( width + height ) * 2 );
    rayPos.reserve( static_cast<std::size_t>( width + height ) * 2 );
    std::vector<double> carry;

    const double step = std::min( cellSizeX, cellSizeY );
    const double maxT = 2.0 * std::hypot( width * cellSizeX, height * cellSizeY );
    int sectorsUsed = 0;

    for ( int sector = 0; sector < 360; ++sector )
    {
        const std::vector<std::size_t> &samples =
            sectorSamples[static_cast<std::size_t>( sector )];
        if ( samples.empty() )
            continue;
        ++sectorsUsed;
        const double azRad = ( static_cast<double>( sector ) + 0.5 ) * kDegToRad;
        const double ux = std::sin( azRad );  // +col component
        const double uy = -std::cos( azRad ); // +row component (south)
        const std::size_t sampleCount = samples.size();
        carry.assign( sampleCount, -std::numeric_limits<double>::infinity() );
        std::vector<double> tans( sampleCount );
        for ( std::size_t k = 0; k < sampleCount; ++k )
            tans[k] = std::tan( track[samples[k]].elevationDeg * kDegToRad );

        // One backward sweep per ray: at cell c the carry holds
        // max over cells x strictly beyond c of (z_x − p_x·tanε_k); the
        // parallel-ray shadow test is carry_k > z_c − p_c·tanε_k.
        auto evaluateRay = [&]( double startX, double startY ) {
            rayCells.clear();
            rayPos.clear();
            double t = 0.0;
            std::uint32_t last = std::numeric_limits<std::uint32_t>::max();
            while ( t <= maxT )
            {
                const double fx = startX + t * ux / cellSizeX;
                const double fy = startY + t * uy / cellSizeY;
                const int cx = static_cast<int>( std::floor( fx ) );
                const int cy = static_cast<int>( std::floor( fy ) );
                if ( cx < 0 || cy < 0 || cx >= width || cy >= height )
                    break;
                const std::uint32_t id = static_cast<std::uint32_t>( cy ) * width + cx;
                if ( id != last )
                {
                    last = id;
                    rayCells.push_back( id );
                    rayPos.push_back( t );
                }
                t += step;
            }
            std::fill( carry.begin(), carry.end(),
                       -std::numeric_limits<double>::infinity() );
            for ( std::size_t idx = rayCells.size(); idx-- > 0; )
            {
                const std::uint32_t id = rayCells[idx];
                const double p = rayPos[idx];
                const float z = dem[id];
                if ( isMissing( z, nodata ) )
                {
                    // NoData barrier: nothing beyond it shadows nearer cells
                    // along this ray.
                    std::fill( carry.begin(), carry.end(),
                               -std::numeric_limits<double>::infinity() );
                    continue;
                }
                if ( stamp[id] != sector )
                {
                    stamp[id] = sector;
                    for ( std::size_t k = 0; k < sampleCount; ++k )
                        if ( carry[k] > static_cast<double>( z ) - p * tans[k] )
                            shadowAccum[id] += track[samples[k]].weight;
                }
                for ( std::size_t k = 0; k < sampleCount; ++k )
                    carry[k] =
                        std::max( carry[k], static_cast<double>( z ) - p * tans[k] );
            }
        };

        // Rays enter through the grid edges whose outward side lies upstream
        // of the direction: ux > 0 → left edge, ux < 0 → right edge,
        // uy > 0 → top edge, uy < 0 → bottom edge. Scanning the (up to two)
        // upstream edges in full plus the sector stamp gives every cell
        // exactly one ray evaluation.
        if ( ux > 0.0 )
            for ( int y = 0; y < height; ++y )
                evaluateRay( 0.5, y + 0.5 );
        if ( ux < 0.0 )
            for ( int y = 0; y < height; ++y )
                evaluateRay( width - 0.5, y + 0.5 );
        if ( uy > 0.0 )
            for ( int x = 0; x < width; ++x )
                evaluateRay( x + 0.5, 0.5 );
        if ( uy < 0.0 )
            for ( int x = 0; x < width; ++x )
                evaluateRay( x + 0.5, height - 0.5 );
        if ( cancelled && ( ( sector & 0x0F ) == 0 ) && cancelled() )
            return false;
    }

    for ( std::size_t i = 0; i < n; ++i )
        if ( !isMissing( dem[i], nodata ) )
            out->shadowFraction[i] = static_cast<float>( shadowAccum[i] / weightSum );
    out->weightSum = weightSum;
    out->sampleCount = accepted;
    out->sectorsUsed = static_cast<std::size_t>( sectorsUsed );
    return true;
}

bool sunPositionDeg( int dayOfYear, double solarHour, double latitudeDeg,
                     double *azimuthDeg, double *elevationDeg )
{
    if ( !azimuthDeg || !elevationDeg )
        return false;
    if ( dayOfYear < 1 || dayOfYear > 365 )
        return false;
    if ( !std::isfinite( solarHour ) || solarHour < 0.0 || solarHour > 24.0 )
        return false;
    if ( !std::isfinite( latitudeDeg ) || latitudeDeg < -90.0 || latitudeDeg > 90.0 )
        return false;

    // Spencer (1971) four-term declination series in radians.
    const double gamma = 2.0 * M_PI / 365.0
                         * ( dayOfYear - 1 + ( solarHour - 12.0 ) / 24.0 );
    const double decl =
        0.006918 - 0.399912 * std::cos( gamma ) + 0.070257 * std::sin( gamma )
        - 0.006758 * std::cos( 2.0 * gamma ) + 0.000907 * std::sin( 2.0 * gamma )
        - 0.002697 * std::cos( 3.0 * gamma ) + 0.00148 * std::sin( 3.0 * gamma );
    const double lat = latitudeDeg * kDegToRad;
    const double hourAngle = ( solarHour - 12.0 ) * 15.0 * kDegToRad;

    const double sinElev = std::sin( lat ) * std::sin( decl )
                           + std::cos( lat ) * std::cos( decl ) * std::cos( hourAngle );
    const double elev = std::asin( std::clamp( sinElev, -1.0, 1.0 ) );

    const double cosAz = ( std::sin( decl ) - std::sin( elev ) * std::sin( lat ) )
                         / ( std::cos( elev ) * std::cos( lat ) );
    double az = std::acos( std::clamp( cosAz, -1.0, 1.0 ) ); // 0..π from north
    if ( hourAngle > 0.0 )
        az = 2.0 * M_PI - az; // afternoon: west of the meridian

    *elevationDeg = elev / kDegToRad;
    *azimuthDeg = az / kDegToRad;
    return true;
}

} // namespace TerrainSolar
