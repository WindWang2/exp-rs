// terrain_hydrology.cpp — see terrain_hydrology.h for contracts.

#include "terrain_hydrology.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>
#include <vector>

namespace TerrainHydrology
{
namespace
{

// Same step set and order as terrain_flow.cpp (E, SE, S, SW, W, NW, N, NE).
struct Neighbor
{
    int dx;
    int dy;
    float dist;
    int code;
};

constexpr Neighbor kNeighbors[8] = {
    { 1, 0, 1.0f, 1 },
    { 1, 1, 1.41421353816986083984375f, 2 },
    { 0, 1, 1.0f, 4 },
    { -1, 1, 1.41421353816986083984375f, 8 },
    { -1, 0, 1.0f, 16 },
    { -1, -1, 1.41421353816986083984375f, 32 },
    { 0, -1, 1.0f, 64 },
    { 1, -1, 1.41421353816986083984375f, 128 },
};

inline bool isMissing( float z, float nodata )
{
    return z == nodata || std::isnan( z );
}

// Compass table: azimuth k*45° → (dx, dy) with +y = south. Index 0 = N.
struct Compass
{
    int dx;
    int dy;
};

constexpr Compass kCompass[8] = {
    { 0, -1 },  // 0°   N
    { 1, -1 },  // 45°  NE
    { 1, 0 },   // 90°  E
    { 1, 1 },   // 135° SE
    { 0, 1 },   // 180° S
    { -1, 1 },  // 225° SW
    { -1, 0 },  // 270° W
    { -1, -1 }, // 315° NW
};

constexpr double kSqrt2 = 1.4142135623730951;
constexpr double kDeg45 = 0.7853981633974483; // π/4

inline double azimuthToRadians( int k ) { return k * kDeg45; }

inline bool pollCancelled( const std::function<bool()> &cancelled, std::size_t tick )
{
    return cancelled && ( tick & 0xFFFu ) == 0u && cancelled();
}

// Steepest descent of the planar facet (k1, k2) of the cell at (col,row),
// restricted to the facet wedge. Math: the facet plane's gradient g satisfies
// g·e1 = −s1, g·e2 = −s2 where e1/e2 are the unit vectors at azimuths
// α1 = 45°·k1 and α2 = α1+45° (c = e1·e2 = cos 45°) and s1/s2 are the centre-
// to-neighbour slopes. Expressing the descent w = −g in the wedge basis,
// w = α·e1 + β·e2 with
//     α = 2(s1 − c·s2),  β = 2(s2 − c·s1),
// the descent lies in the wedge iff α ≥ 0 and β ≥ 0 (else it exits through
// the centre vertex and an adjacent facet owns it — Tarboton 1997). The
// slope is |w| = sqrt(α² + β² + 2αβc); the direction offset from e1 is
// acos(s1/|w|) (w·e1 = s1 by construction). A plane DEM therefore reproduces
// its exact gradient azimuth on every wedge that contains it.
// Returns the D∞ azimuth in degrees clockwise from north; false when no
// facet descends (pit / un-resolved flat / NoData rim).
bool steepestFacet( const float *filled, int width, int height, float nodata,
                    int col, int row, double *outAzimuth )
{
    const std::size_t i = static_cast<std::size_t>( row ) * width + col;
    const double z0 = filled[i];

    double bestSlope = 0.0;
    double bestAzimuth = 0.0;
    bool found = false;

    // Facets k = (compass k, compass k+1), fixed order N-NE, NE-E, …, NW-N.
    for ( int k = 0; k < 8; ++k )
    {
        const int k1 = k;
        const int k2 = ( k + 1 ) & 7;
        const int x1 = col + kCompass[k1].dx;
        const int y1 = row + kCompass[k1].dy;
        const int x2 = col + kCompass[k2].dx;
        const int y2 = row + kCompass[k2].dy;
        if ( x1 < 0 || y1 < 0 || x1 >= width || y1 >= height )
            continue;
        if ( x2 < 0 || y2 < 0 || x2 >= width || y2 >= height )
            continue;
        const float z1 = filled[static_cast<std::size_t>( y1 ) * width + x1];
        const float z2 = filled[static_cast<std::size_t>( y2 ) * width + x2];
        if ( isMissing( z1, nodata ) || isMissing( z2, nodata ) )
            continue;

        const double d1 = ( k1 & 1 ) ? kSqrt2 : 1.0; // odd compass indices are diagonal
        const double d2 = ( k2 & 1 ) ? kSqrt2 : 1.0;
        const double s1 = ( z0 - z1 ) / d1;
        const double s2 = ( z0 - z2 ) / d2;
        if ( s1 <= 0.0 && s2 <= 0.0 )
            continue; // no descent anywhere in this facet's wedge

        // Wedge membership with a rounding tolerance: boundary cases (the
        // descent exactly along an edge, e.g. planar surfaces) otherwise
        // reject on a 1-ulp negative alpha/beta.
        const double tol = 1e-12 * ( 1.0 + std::fabs( s1 ) + std::fabs( s2 ) );
        const double alpha = 2.0 * ( s1 - 0.5 * kSqrt2 * s2 );
        const double beta = 2.0 * ( s2 - 0.5 * kSqrt2 * s1 );
        if ( alpha < -tol || beta < -tol )
            continue; // descent exits through the centre vertex

        const double slope = std::sqrt( std::max(
            0.0, alpha * alpha + beta * beta + 2.0 * alpha * beta * 0.5 * kSqrt2 ) );
        if ( slope <= 0.0 )
            continue;
        double cosTheta = s1 / slope;
        cosTheta = std::max( 0.0, std::min( 1.0, cosTheta ) );
        const double theta = std::acos( cosTheta ); // ∈ [0°, 45°] inside the wedge

        // Strict improvement keeps the first facet on ties (determinism).
        if ( !found || slope > bestSlope )
        {
            found = true;
            bestSlope = slope;
            bestAzimuth = azimuthToRadians( k1 ) + theta;
        }
    }

    if ( !found )
        return false;
    *outAzimuth = std::fmod( bestAzimuth, 2.0 * M_PI ) * ( 180.0 / M_PI );
    return true;
}

} // namespace

bool resolveFlats( const float *dem, float *filled, int width, int height,
                   float nodata, FlatResolutionReport *report,
                   const std::function<bool()> &cancelled )
{
    if ( !dem || !filled || width <= 0 || height <= 0 )
        return false;
    if ( cancelled && cancelled() )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;

    // Relief-scaled epsilon: increments must survive float32 addition at the
    // DEM's own magnitude (2⁻²⁰ relief keeps them ≥ 2³ ulp above absorption
    // at the largest elevations, and better lower down), with an absolute
    // floor for constant/tiny DEMs.
    double zmin = std::numeric_limits<double>::infinity();
    double zmax = -std::numeric_limits<double>::infinity();
    std::size_t tick = 0;
    for ( std::size_t i = 0; i < n; ++i )
    {
        if ( isMissing( dem[i], nodata ) )
            continue;
        zmin = std::min( zmin, static_cast<double>( dem[i] ) );
        zmax = std::max( zmax, static_cast<double>( dem[i] ) );
        if ( pollCancelled( cancelled, ++tick ) )
            return false;
    }
    if ( !std::isfinite( zmin ) ) // all-NoData DEM: nothing to fill
    {
        if ( report )
        {
            report->epsilon = 0.0;
            report->raisedCells = 0;
        }
        return true;
    }
    const double relief = std::max( 0.0, zmax - zmin );
    double epsilon = std::max( relief * 9.5367431640625e-07, 1e-6 ); // 2⁻²⁰
    if ( epsilon >= std::numeric_limits<float>::max() / 4.0 )
        epsilon = std::numeric_limits<float>::max() / 4.0;

    std::vector<std::uint8_t> done( n, 0 );
    using QEntry = std::pair<float, std::size_t>; // (level, index), min-heap
    std::priority_queue<QEntry, std::vector<QEntry>, std::greater<QEntry>> queue;

    // Drain boundary: the perimeter plus every valid cell adjacent to NoData
    // (#848 seeding, same as fillDepressions).
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            const std::size_t i = static_cast<std::size_t>( y ) * width + x;
            filled[i] = dem[i];
            if ( isMissing( dem[i], nodata ) )
            {
                done[i] = 1;
                continue;
            }
            const bool atPerimeter = x == 0 || y == 0 || x == width - 1 || y == height - 1;
            bool bordersNoData = false;
            if ( !atPerimeter )
                for ( const Neighbor &nb : kNeighbors )
                {
                    const float vz = dem[static_cast<std::size_t>( y + nb.dy ) * width
                                         + ( x + nb.dx )];
                    if ( isMissing( vz, nodata ) )
                    {
                        bordersNoData = true;
                        break;
                    }
                }
            if ( atPerimeter || bordersNoData )
            {
                done[i] = 1;
                queue.emplace( dem[i], i );
            }
        }

    long long raised = 0;
    tick = 0;
    const float fEps = static_cast<float>( epsilon );
    while ( !queue.empty() )
    {
        const auto [level, i] = queue.top();
        queue.pop();
        if ( pollCancelled( cancelled, ++tick ) )
            return false;
        const int x = static_cast<int>( i % width );
        const int y = static_cast<int>( i / width );
        for ( const Neighbor &nb : kNeighbors )
        {
            const int nx = x + nb.dx;
            const int ny = y + nb.dy;
            if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                continue;
            const std::size_t j = static_cast<std::size_t>( ny ) * width + nx;
            if ( done[j] )
                continue;
            done[j] = 1;
            if ( isMissing( filled[j], nodata ) )
                continue; // NoData barrier
            if ( filled[j] <= level )
            {
                // Epsilon gradient: raise to just above the popped level so
                // the flat drains in strict float steps.
                filled[j] = level + fEps;
                ++raised;
            }
            queue.emplace( filled[j], j );
        }
    }

    if ( report )
    {
        report->epsilon = epsilon;
        report->raisedCells = raised;
    }
    return true;
}

bool flowDirectionInf( const float *filled, float *angle, int width, int height,
                       float nodata, const std::function<bool()> &cancelled )
{
    if ( !filled || !angle || width <= 0 || height <= 0 )
        return false;
    if ( cancelled && cancelled() )
        return false;
    std::size_t tick = 0;
    for ( int y = 0; y < height; ++y )
    {
        for ( int x = 0; x < width; ++x )
        {
            const std::size_t i = static_cast<std::size_t>( y ) * width + x;
            const float z = filled[i];
            if ( isMissing( z, nodata ) )
            {
                angle[i] = nodata;
                continue;
            }
            // Pit rule: a cell with no strictly lower valid neighbour is a
            // sink regardless of facet gradients (the planar facet fit would
            // otherwise invent a descent for cone apexes — the discrete
            // surface's minimum must terminate flow there, matching the D8
            // "0 = sink" semantics).
            bool hasLower = false;
            for ( int k = 0; k < 8 && !hasLower; ++k )
            {
                const int nx = x + kCompass[k].dx;
                const int ny = y + kCompass[k].dy;
                if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                    continue;
                const float zn =
                    filled[static_cast<std::size_t>( ny ) * width + nx];
                if ( !isMissing( zn, nodata ) && zn < z )
                    hasLower = true;
            }
            if ( !hasLower )
            {
                angle[i] = -1.0f;
                continue;
            }
            double azimuth = 0.0;
            if ( steepestFacet( filled, width, height, nodata, x, y, &azimuth ) )
            {
                angle[i] = static_cast<float>( azimuth );
            }
            else
            {
                // Degenerate facet geometry (e.g. radial pit walls on a
                // cone: every facet's descent exits through a vertex or
                // points at an equal cell). Fall back to the D8
                // steepest-descent neighbour — same rule and tie-break as
                // TerrainFlow::flowDirections — so every non-pit cell
                // drains and the accumulation mass identity holds.
                double bestSlope = 0.0;
                int bestAz = -1;
                for ( int k = 0; k < 8; ++k )
                {
                    const int nx = x + kCompass[k].dx;
                    const int ny = y + kCompass[k].dy;
                    if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                        continue;
                    const float zn =
                        filled[static_cast<std::size_t>( ny ) * width + nx];
                    if ( isMissing( zn, nodata ) || zn >= z )
                        continue;
                    const double dist = ( k & 1 ) ? kSqrt2 : 1.0;
                    const double slope = ( static_cast<double>( z ) - zn ) / dist;
                    if ( bestAz < 0 || slope > bestSlope )
                    {
                        bestSlope = slope;
                        bestAz = k * 45;
                    }
                }
                angle[i] = bestAz >= 0 ? static_cast<float>( bestAz ) : -1.0f;
            }
        }
        if ( pollCancelled( cancelled, ++tick ) )
            return false;
    }
    return true;
}

std::pair<int, int> dInfReceiver( const float *angles,
                                  int width, int height, float nodata,
                                  int col, int row )
{
    const std::size_t i = static_cast<std::size_t>( row ) * width + col;
    float a = angles[i];
    // Guard the 359.99…°→360.0f float round-up: azimuth 360 ≡ 0 (north).
    if ( a >= 360.0f )
        a = 0.0f;
    // Undecided cells (pits, un-resolved flats) and NoData keep themselves
    // as receiver (self-loop, mirroring the D8 "code 0 = sink" convention).
    if ( isMissing( a, nodata ) || a <= -1.0f )
        return { col, row };

    // Receiver = base node of the steepest facet nearer the facet exit point.
    // Re-derive the facet from the stored angle: the facet index is the
    // compass sector containing the azimuth; theta follows from the angle.
    // steepestFacet only emits angles whose facet base nodes are in-grid and
    // valid, so no further neighbour checks are needed here.
    const double azRad = a * ( M_PI / 180.0 );
    int k1 = static_cast<int>( std::floor( a / 45.0 ) ) & 7; // N-NE sector = 0 …
    // Reconstruct theta inside the facet (guard float drift at 360°).
    double theta = azRad - azimuthToRadians( k1 );
    if ( theta < 0.0 )
        theta = 0.0;
    if ( theta > kDeg45 )
        theta = kDeg45;

    const int k2 = ( k1 + 1 ) & 7;
    const int x1 = col + kCompass[k1].dx;
    const int y1 = row + kCompass[k1].dy;
    const int x2 = col + kCompass[k2].dx;
    const int y2 = row + kCompass[k2].dy;

    // Exit point of the ray at angle (α1+θ) across the base segment n1–n2 in
    // compass coordinates (dx = sin α, dy = −cos α; y grows southward).
    const double alpha1 = azimuthToRadians( k1 );
    const double alpha2 = azimuthToRadians( k2 );
    const double d1 = ( k1 & 1 ) ? kSqrt2 : 1.0;
    const double d2 = ( k2 & 1 ) ? kSqrt2 : 1.0;
    const double n1x = d1 * std::sin( alpha1 );
    const double n1y = -d1 * std::cos( alpha1 );
    const double n2x = d2 * std::sin( alpha2 );
    const double n2y = -d2 * std::cos( alpha2 );
    const double wx = std::sin( alpha1 + theta );
    const double wy = -std::cos( alpha1 + theta );
    const double cross = wx * ( n2y - n1y ) - wy * ( n2x - n1x );
    if ( std::fabs( cross ) < 1e-300 )
        return { x1, y1 }; // degenerate ray along the base: tie → first node
    const double t = ( n1x * n2y - n1y * n2x ) / cross;
    const double px = t * wx;
    const double py = t * wy;
    const double dist1 = std::hypot( px - n1x, py - n1y );
    const double dist2 = std::hypot( px - n2x, py - n2y );
    if ( dist1 <= dist2 )
        return { x1, y1 };
    return { x2, y2 };
}

bool dInfAccumulation( const float *angles, float *acc, int width, int height,
                       float nodata )
{
    if ( !angles || !acc || width <= 0 || height <= 0 )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;
    const auto isSink = [&]( std::size_t i ) {
        const float a = angles[i];
        return a <= -1.0f || std::isnan( a ) || a == nodata;
    };
    const auto isNoData = [&]( std::size_t i ) {
        const float a = angles[i];
        return std::isnan( a ) || a == nodata;
    };

    // Non-sink cells always route into their receiver — including when the
    // receiver is a pit (angle −1): the pit accumulates and stops there.
    // Sinks self-loop, so their accumulated water never leaves.
    const auto receiver = [&]( std::size_t i ) -> std::size_t {
        if ( isSink( i ) )
            return i;
        const int x = static_cast<int>( i % width );
        const int y = static_cast<int>( i / width );
        const auto [rx, ry] = dInfReceiver( angles, width, height, nodata, x, y );
        return static_cast<std::size_t>( ry ) * width + rx;
    };

    std::vector<int> indegree( n, 0 );
    for ( std::size_t i = 0; i < n; ++i )
    {
        acc[i] = 1.0f;
        const std::size_t down = receiver( i );
        if ( down != i )
            ++indegree[down];
    }
    for ( std::size_t i = 0; i < n; ++i )
        if ( isNoData( i ) )
            acc[i] = nodata; // sentinel preserved in the written raster

    std::vector<std::size_t> queue;
    queue.reserve( n );
    for ( std::size_t i = 0; i < n; ++i )
        if ( indegree[i] == 0 )
            queue.push_back( i );
    for ( std::size_t head = 0; head < queue.size(); ++head )
    {
        const std::size_t i = queue[head];
        const std::size_t down = receiver( i );
        if ( down != i )
        {
            acc[down] += acc[i];
            if ( --indegree[down] == 0 )
                queue.push_back( down );
        }
    }
    return true;
}

std::vector<Outlet> detectOutlets( const float *filled, const float *dir,
                                   int width, int height, float nodata )
{
    std::vector<Outlet> outlets;
    if ( !filled || !dir || width <= 0 || height <= 0 )
        return outlets;
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            const std::size_t i = static_cast<std::size_t>( y ) * width + x;
            const float z = filled[i];
            if ( isMissing( z, nodata ) )
                continue;
            const float d = dir[i];
            // Direction grid: valid D8 codes are 0..128; anything else
            // (NaN/±huge sentinels) is outside the routing graph.
            if ( !std::isfinite( d ) || d < 0.0f || d > 128.0f
                 || static_cast<float>( static_cast<int>( d ) ) != d )
                continue;
            if ( static_cast<int>( d ) != 0 )
                continue;
            const bool atRim = x == 0 || y == 0 || x == width - 1 || y == height - 1;
            bool bordersNoData = false;
            if ( !atRim )
                for ( const Neighbor &nb : kNeighbors )
                {
                    const int nx = x + nb.dx;
                    const int ny = y + nb.dy;
                    if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                        continue;
                    if ( isMissing( filled[static_cast<std::size_t>( ny ) * width + nx],
                                    nodata ) )
                    {
                        bordersNoData = true;
                        break;
                    }
                }
            outlets.push_back( { x, y, atRim || bordersNoData } );
        }
    return outlets;
}

bool streamNetwork( const float *dir, const float *acc, int width, int height,
                    const float *filled, float nodata, float threshold,
                    StreamNetwork *out )
{
    if ( !dir || !acc || !out || width <= 0 || height <= 0 )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;
    out->isStream.assign( n, 0 );
    out->strahler.assign( n, 0 );

    const auto isNoData = [&]( std::size_t i ) {
        return filled && isMissing( filled[i], nodata );
    };
    const auto isValidCode = []( float d ) {
        return std::isfinite( d ) && d >= 0.0f && d <= 128.0f
               && static_cast<float>( static_cast<int>( d ) ) == d;
    };

    for ( std::size_t i = 0; i < n; ++i )
        if ( !isNoData( i ) && acc[i] >= threshold && isValidCode( dir[i] ) )
            out->isStream[i] = 1;

    // Upstream count in the stream subgraph (neighbours whose D8 code points
    // into this cell), then a Kahn peel computing Strahler orders.
    std::vector<int> upstream( n, 0 );
    std::vector<int> maxOrder( n, 0 );
    std::vector<int> maxCount( n, 0 );
    for ( std::size_t i = 0; i < n; ++i )
    {
        if ( !out->isStream[i] )
            continue;
        const int code = static_cast<int>( dir[i] );
        if ( code == 0 )
            continue; // network outlet: no downstream
        const int x = static_cast<int>( i % width );
        const int y = static_cast<int>( i / width );
        for ( const Neighbor &nb : kNeighbors )
            if ( nb.code == code )
            {
                const int nx = x + nb.dx;
                const int ny = y + nb.dy;
                if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                    continue;
                const std::size_t down = static_cast<std::size_t>( ny ) * width + nx;
                if ( out->isStream[down] )
                    ++upstream[down];
                break;
            }
    }

    std::vector<std::size_t> queue;
    queue.reserve( n );
    for ( std::size_t i = 0; i < n; ++i )
    {
        if ( !out->isStream[i] )
            continue;
        out->strahler[i] = 1;
        if ( upstream[i] == 0 )
            queue.push_back( i );
    }
    for ( std::size_t head = 0; head < queue.size(); ++head )
    {
        const std::size_t i = queue[head];
        const int code = static_cast<int>( dir[i] );
        if ( code == 0 )
            continue;
        const int x = static_cast<int>( i % width );
        const int y = static_cast<int>( i / width );
        for ( const Neighbor &nb : kNeighbors )
            if ( nb.code == code )
            {
                const int nx = x + nb.dx;
                const int ny = y + nb.dy;
                if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                    break;
                const std::size_t down = static_cast<std::size_t>( ny ) * width + nx;
                if ( !out->isStream[down] )
                    break;
                const int order = out->strahler[i];
                if ( order > maxOrder[down] )
                {
                    maxOrder[down] = order;
                    maxCount[down] = 1;
                }
                else if ( order == maxOrder[down] )
                {
                    ++maxCount[down];
                }
                if ( --upstream[down] == 0 )
                {
                    out->strahler[down] =
                        static_cast<std::uint8_t>( maxOrder[down]
                                                   + ( maxCount[down] >= 2 ? 1 : 0 ) );
                    queue.push_back( down );
                }
                break;
            }
    }
    return true;
}

bool streamSegments( const float *dir, int width, int height, float nodata,
                     const StreamNetwork &network,
                     std::vector<StreamSegment> *out )
{
    if ( !dir || !out || width <= 0 || height <= 0 )
        return false;
    const std::size_t n = static_cast<std::size_t>( width ) * height;
    if ( network.isStream.size() != n || network.strahler.size() != n )
        return false;

    const auto isValidCode = []( float d ) {
        return std::isfinite( d ) && d >= 0.0f && d <= 128.0f
               && static_cast<float>( static_cast<int>( d ) ) == d;
    };
    const auto receiverOf = [&]( std::size_t i ) -> std::size_t {
        const int code = static_cast<int>( dir[i] );
        const int x = static_cast<int>( i % width );
        const int y = static_cast<int>( i / width );
        for ( const Neighbor &nb : kNeighbors )
            if ( nb.code == code )
            {
                const int nx = x + nb.dx;
                const int ny = y + nb.dy;
                if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                    return i;
                return static_cast<std::size_t>( ny ) * width + nx;
            }
        return i;
    };
    // Count upstream stream neighbours per cell (junction detection).
    std::vector<int> upstream( n, 0 );
    for ( std::size_t i = 0; i < n; ++i )
    {
        if ( !network.isStream[i] || !isValidCode( dir[i] ) )
            continue;
        const int code = static_cast<int>( dir[i] );
        if ( code == 0 )
            continue;
        const std::size_t down = receiverOf( i );
        if ( down != i && network.isStream[down] )
            ++upstream[down];
    }

    std::vector<std::uint8_t> consumed( n, 0 );
    out->clear();
    // Start cells: heads (upstream == 0), then junction continuations, in
    // row-major order — deterministic discovery.
    for ( int pass = 0; pass < 2; ++pass )
    {
        for ( std::size_t i = 0; i < n; ++i )
        {
            if ( !network.isStream[i] || consumed[i] || !isValidCode( dir[i] ) )
                continue;
            const bool isHead = upstream[i] == 0;
            const bool isJunction = upstream[i] >= 2;
            if ( pass == 0 && !isHead )
                continue;
            if ( pass == 1 && !isJunction )
                continue;

            StreamSegment seg;
            seg.order = network.strahler[i];
            std::size_t cur = i;
            while ( true )
            {
                consumed[cur] = 1;
                seg.cells.emplace_back( static_cast<int>( cur % width ),
                                        static_cast<int>( cur / width ) );
                const int code = static_cast<int>( dir[cur] );
                if ( code == 0 )
                    break; // network end
                const std::size_t down = receiverOf( cur );
                if ( down == cur || !network.isStream[down] )
                    break; // leaves the grid or the network
                // A junction terminates the incoming link (junction itself
                // belongs to its downstream continuation, consumed in pass 1
                // or already by an earlier link).
                if ( upstream[down] >= 2 )
                    break;
                cur = down;
            }
            out->push_back( std::move( seg ) );
        }
    }
    return true;
}

} // namespace TerrainHydrology
