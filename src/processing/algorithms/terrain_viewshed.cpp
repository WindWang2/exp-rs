// terrain_viewshed.cpp — see terrain_viewshed.h for contracts.

#include "terrain_viewshed.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace TerrainVisibility
{
namespace
{

constexpr int kRingInvalid = -1;
constexpr float kHorizonBlocked = std::numeric_limits<float>::infinity();
constexpr float kHorizonOpen = -std::numeric_limits<float>::infinity();

struct Step
{
    int dx;
    int dy;
};

// Fixed 8-neighbourhood (same step set as the terrain flow family).
constexpr Step kSteps[8] = {
    { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
};

inline bool isMissing( float z, float nodata )
{
    return z == nodata || std::isnan( z );
}

} // namespace

bool viewshedR3( const float *dem, int width, int height, float nodata,
                 double cellSizeX, double cellSizeY, const ViewshedParams &params,
                 std::vector<std::uint8_t> *visible, std::uint8_t nodataByte,
                 const std::function<bool()> &cancelled )
{
    if ( !dem || !visible || width <= 0 || height <= 0 || cellSizeX <= 0.0
         || cellSizeY <= 0.0 )
        return false;
    if ( cancelled && cancelled() )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;
    visible->assign( n, 0 );

    const int ox = static_cast<int>( std::lround( params.obsCol ) );
    const int oy = static_cast<int>( std::lround( params.obsRow ) );
    if ( ox < 0 || oy < 0 || ox >= width || oy >= height )
        return false;
    const std::size_t oi = static_cast<std::size_t>( oy ) * width + ox;
    if ( isMissing( dem[oi], nodata ) )
        return false;
    const double zaObs = dem[oi] + params.observerHeight;

    // Pass 1: map distance, curvature-adjusted elevation and the integer
    // distance ring of every cell (invalid ring for NoData).
    const double radius = params.radius > 0.0 ? params.radius : 0.0;
    std::vector<int> ring( n, kRingInvalid );
    std::vector<float> za( n, 0.0f );
    std::vector<float> distMap( n, 0.0f );
    int maxRing = 0;
    for ( int y = 0; y < height; ++y )
    {
        const double dy = ( y - oy ) * cellSizeY;
        for ( int x = 0; x < width; ++x )
        {
            const std::size_t i = static_cast<std::size_t>( y ) * width + x;
            if ( isMissing( dem[i], nodata ) )
                continue;
            const double dx = ( x - ox ) * cellSizeX;
            const double d = std::hypot( dx, dy );
            if ( radius > 0.0 && d > radius )
                continue; // outside the analysis radius: stays invisible
            const double adjusted = dem[i] - params.curvatureFactor * d * d;
            za[i] = static_cast<float>( adjusted );
            distMap[i] = static_cast<float>( d );
            const int r = static_cast<int>( std::ceil( d ) );
            ring[i] = r;
            maxRing = std::max( maxRing, r );
        }
    }

    // Pass 2: CSR grouping of cells by ring (counting sort) for the sweep.
    std::vector<std::size_t> ringStart( static_cast<std::size_t>( maxRing ) + 2, 0 );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            const int r = ring[static_cast<std::size_t>( y ) * width + x];
            if ( r >= 1 )
                ++ringStart[static_cast<std::size_t>( r ) + 1];
        }
    for ( std::size_t r = 1; r + 1 < ringStart.size(); ++r )
        ringStart[r + 1] += ringStart[r];
    std::vector<std::uint32_t> cellsByRing( ringStart.back() );
    {
        std::vector<std::size_t> cursor( ringStart.begin(), ringStart.end() );
        for ( int y = 0; y < height; ++y )
            for ( int x = 0; x < width; ++x )
            {
                const std::size_t i = static_cast<std::size_t>( y ) * width + x;
                const int r = ring[i];
                if ( r >= 1 )
                    cellsByRing[cursor[static_cast<std::size_t>( r )]++] =
                        static_cast<std::uint32_t>( i );
            }
    }

    // Pass 3: ring sweep. horizonMax[i] = max terrain angle along the best
    // path from the observer to i (including i itself); +inf = blocked.
    std::vector<float> horizonMax( n, kHorizonBlocked );
    std::uint8_t *out = visible->data();
    out[oi] = 1;
    horizonMax[oi] = kHorizonOpen;

    for ( int r = 1; r <= maxRing; ++r )
    {
        const std::size_t begin = ringStart[static_cast<std::size_t>( r )];
        const std::size_t end = ringStart[static_cast<std::size_t>( r ) + 1];
        for ( std::size_t slot = begin; slot < end; ++slot )
        {
            const std::size_t i = cellsByRing[slot];
            const int x = static_cast<int>( i % width );
            const int y = static_cast<int>( i / width );

            float pathMax = kHorizonBlocked;
            for ( const Step &st : kSteps )
            {
                const int px = x + st.dx;
                const int py = y + st.dy;
                if ( px < 0 || py < 0 || px >= width || py >= height )
                    continue;
                const std::size_t p = static_cast<std::size_t>( py ) * width + px;
                if ( ring[p] < r && ring[p] != kRingInvalid )
                    pathMax = std::min( pathMax, horizonMax[p] );
            }
            if ( pathMax == kHorizonBlocked )
                continue; // every path enters through NoData: stays invisible

            const float d = distMap[i];
            const float terrainAngle =
                ( za[i] - static_cast<float>( zaObs ) ) / std::max( d, 1e-12f );
            out[i] = ( ( za[i] + static_cast<float>( params.targetHeight )
                         - static_cast<float>( zaObs ) )
                           / std::max( d, 1e-12f ) )
                     > pathMax
                 ? 1
                 : 0;
            horizonMax[i] = std::max( pathMax, terrainAngle );
        }
        if ( cancelled && ( ( r & 0x3F ) == 0 ) && cancelled() )
            return false;
    }
    return true;
}

bool cumulativeViewshed( const float *dem, int width, int height, float nodata,
                         double cellSizeX, double cellSizeY,
                         const std::vector<ViewshedParams> &observers,
                         std::vector<std::uint16_t> *counts, std::uint16_t nodataWord,
                         const std::function<bool()> &cancelled )
{
    if ( !dem || !counts || width <= 0 || height <= 0 || cellSizeX <= 0.0
         || cellSizeY <= 0.0 )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;
    counts->assign( n, nodataWord );

    // Initialize: NoData cells carry the sentinel, valid cells start at 0 —
    // independent of how many observers follow (also correct for an empty
    // observer list).
    for ( std::size_t i = 0; i < n; ++i )
        if ( !isMissing( dem[i], nodata ) )
            ( *counts )[i] = 0;

    std::vector<std::uint8_t> scratch( n, 0 );
    for ( const ViewshedParams &observer : observers )
    {
        if ( !viewshedR3( dem, width, height, nodata, cellSizeX, cellSizeY, observer,
                          &scratch, 0, cancelled ) )
            return false;
        for ( std::size_t i = 0; i < n; ++i )
            if ( scratch[i] )
                ++( *counts )[i];
    }
    return true;
}

bool horizonProfile( const float *dem, int width, int height, float nodata,
                     double cellSizeX, double cellSizeY, double obsCol, double obsRow,
                     double observerHeight, double maxDistance, double curvatureFactor,
                     double azimuthStepDeg, HorizonProfile *out,
                     const std::function<bool()> &cancelled )
{
    if ( !dem || !out || width <= 0 || height <= 0 || cellSizeX <= 0.0
         || cellSizeY <= 0.0 || azimuthStepDeg <= 0.0 || azimuthStepDeg > 45.0 )
        return false;
    if ( cancelled && cancelled() )
        return false;
    const int ox = static_cast<int>( std::lround( obsCol ) );
    const int oy = static_cast<int>( std::lround( obsRow ) );
    if ( ox < 0 || oy < 0 || ox >= width || oy >= height )
        return false;
    const std::size_t oi = static_cast<std::size_t>( oy ) * width + ox;
    if ( isMissing( dem[oi], nodata ) )
        return false;
    const double zaObs = dem[oi] + observerHeight;

    const double stepDeg = azimuthStepDeg;
    const int sectors = static_cast<int>( std::lround( 360.0 / stepDeg ) );
    out->azimuthStepDeg = stepDeg;
    out->azimuths.assign( static_cast<std::size_t>( sectors ), 0.0 );
    out->angles.assign( static_cast<std::size_t>( sectors ), -90.0 );

    const double minCell = std::min( cellSizeX, cellSizeY );
    const double frameLimit = 2.0 * std::hypot( width * cellSizeX, height * cellSizeY );
    const double limit = maxDistance > 0.0 ? std::min( maxDistance, frameLimit ) : frameLimit;

    for ( int s = 0; s < sectors; ++s )
    {
        const double azDeg = s * stepDeg;
        const double azRad = azDeg * ( M_PI / 180.0 );
        const double ux = std::sin( azRad );  // +col component
        const double uy = -std::cos( azRad ); // +row component (south)
        double bestTan = -std::numeric_limits<double>::infinity();

        // March from the observer outward; nearest-cell sampling with a
        // visited-stamp guard against double-counting slow-moving steps.
        const double dStep = minCell * 0.5;
        std::uint32_t lastCell = static_cast<std::uint32_t>( oi );
        for ( double t = dStep; t <= limit; t += dStep )
        {
            const double fx = ox + 0.5 + ( t * ux ) / cellSizeX;
            const double fy = oy + 0.5 + ( t * uy ) / cellSizeY;
            const int cx = static_cast<int>( std::floor( fx ) );
            const int cy = static_cast<int>( std::floor( fy ) );
            if ( cx < 0 || cy < 0 || cx >= width || cy >= height )
                break;
            const std::uint32_t cell =
                static_cast<std::uint32_t>( cy ) * width + cx;
            if ( cell == lastCell )
                continue;
            lastCell = cell;
            if ( isMissing( dem[cell], nodata ) )
                break; // NoData is the horizon in this direction
            // Centre-to-centre distance (the march t only decides WHICH
            // cell is sampled; the angle uses the exact cell-centre span,
            // removing nearest-cell quantization from the angle).
            const int ccx = static_cast<int>( cell % width );
            const int ccy = static_cast<int>( cell / width );
            const double dc = std::hypot( ( ccx - ox ) * cellSizeX,
                                          ( ccy - oy ) * cellSizeY );
            const double adjusted =
                static_cast<double>( dem[cell] ) - curvatureFactor * dc * dc;
            const double angleTan = ( adjusted - zaObs ) / dc;
            bestTan = std::max( bestTan, angleTan );
        }
        out->azimuths[static_cast<std::size_t>( s )] = azDeg;
        out->angles[static_cast<std::size_t>( s )] =
            bestTan == -std::numeric_limits<double>::infinity()
                ? -90.0
                : std::atan( bestTan ) * ( 180.0 / M_PI );
        if ( cancelled && ( ( s & 0x3F ) == 0 ) && cancelled() )
            return false;
    }
    return true;
}

} // namespace TerrainVisibility
