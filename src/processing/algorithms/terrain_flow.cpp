// terrain_flow.cpp — see terrain_flow.h for contracts.

#include "terrain_flow.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

namespace TerrainFlow
{
namespace
{

struct Neighbor
{
    int dx;
    int dy;
    float dist;
    int code;
};

// ESRI D8 codes, fixed resolution order: E, SE, S, SW, W, NW, N, NE.
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

// A decoded D8 code must be a finite float in [0, 128] before any float→int
// cast: GDAL Float32 sentinels (e.g. -3.4028235e38f) are outside the range
// of int and casting them is undefined behavior ([conv.fpint], #853).
inline bool isCastableFlowCode( float d )
{
    return std::isfinite( d ) && d >= 0.0f && d <= 128.0f;
}

} // namespace

bool fillDepressions( const float *dem, float *filled, int width, int height, float nodata )
{
    if ( !dem || !filled || width <= 0 || height <= 0 )
        return false;
    const size_t n = static_cast<size_t>( width ) * height;
    std::vector<uint8_t> done( n, 0 );

    using QEntry = std::pair<float, size_t>; // (level, index) — min-heap by level
    std::priority_queue<QEntry, std::vector<QEntry>, std::greater<QEntry>> queue;

    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * width + x;
            filled[i] = dem[i];
            if ( dem[i] == nodata || std::isnan( dem[i] ) )
            {
                done[i] = 1;
                continue;
            }
            // The drain boundary is the raster perimeter AND every valid
            // cell adjacent (8-neighbourhood, matching the flood/routing
            // step set) to a NoData cell (#848): reprojected/clipped DEMs
            // carry NoData borders, and seeding only the rectangular rim
            // left those interiors without a single queue seed — the flood
            // silently filled nothing and every depression survived into
            // the D8 graph. Water overflowing a NoData edge leaves the
            // known surface, so NoData-adjacent valid cells spill at their
            // own elevation.
            const bool atPerimeter =
                x == 0 || y == 0 || x == width - 1 || y == height - 1;
            bool bordersNoData = false;
            if ( !atPerimeter )
            {
                for ( const Neighbor &nb : kNeighbors )
                {
                    const float vz = dem[static_cast<size_t>( y + nb.dy ) * width
                                         + ( x + nb.dx )];
                    if ( vz == nodata || std::isnan( vz ) )
                    {
                        bordersNoData = true;
                        break;
                    }
                }
            }
            if ( atPerimeter || bordersNoData )
            {
                done[i] = 1;
                queue.emplace( dem[i], i );
            }
        }

    while ( !queue.empty() )
    {
        const auto [level, i] = queue.top();
        queue.pop();
        const int x = static_cast<int>( i % width );
        const int y = static_cast<int>( i / width );
        for ( const Neighbor &nb : kNeighbors )
        {
            const int nx = x + nb.dx;
            const int ny = y + nb.dy;
            if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                continue;
            const size_t j = static_cast<size_t>( ny ) * width + nx;
            if ( done[j] )
                continue;
            done[j] = 1;
            if ( filled[j] == nodata || std::isnan( filled[j] ) )
                continue; // NoData barrier: marked visited, never routed across
            filled[j] = std::max( filled[j], level );
            queue.emplace( filled[j], j );
        }
    }
    return true;
}

bool flowDirections( const float *filled, float *dir, int width, int height, float nodata )
{
    if ( !filled || !dir || width <= 0 || height <= 0 )
        return false;
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * width + x;
            const float z = filled[i];
            if ( z == nodata || std::isnan( z ) )
            {
                dir[i] = nodata;
                continue;
            }
            dir[i] = 0.0f;
            float bestSlope = 0.0f;
            int bestCode = 0;
            for ( const Neighbor &nb : kNeighbors )
            {
                const int nx = x + nb.dx;
                const int ny = y + nb.dy;
                if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                    continue;
                const float zn = filled[static_cast<size_t>( ny ) * width + nx];
                if ( zn == nodata || std::isnan( zn ) )
                    continue; // routing into NoData is not allowed
                const float slope = ( z - zn ) / nb.dist;
                if ( slope > bestSlope )
                {
                    bestSlope = slope;
                    bestCode = nb.code;
                }
            }
            dir[i] = static_cast<float>( bestCode );
        }
    return true;
}

bool flowAccumulation( const float *dir, float *acc, int width, int height )
{
    return flowAccumulation( dir, acc, width, height, nullptr, 0.0f );
}

bool flowAccumulation( const float *dir, float *acc, int width, int height,
                       const float *filled, float nodata )
{
    if ( !dir || !acc || width <= 0 || height <= 0 )
        return false;
    const size_t n = static_cast<size_t>( width ) * height;

    const bool maskNodata = filled != nullptr;
    const auto isDirNoData = []( float d ) {
        // Magnitude gate BEFORE the cast — an out-of-range float→int cast is
        // UB, and Float32 sentinels (±3.4e38, ±Inf, NaN) arrive here from
        // arbitrary dir buffers (#853).
        if ( !isCastableFlowCode( d ) )
            return true;
        const int code = static_cast<int>( d );
        if ( static_cast<float>( code ) != d )
            return true;
        switch ( code )
        {
            case 0:
            case 1:
            case 2:
            case 4:
            case 8:
            case 16:
            case 32:
            case 64:
            case 128:
                return false;
            default:
                return true;
        }
    };
    const auto isNodata = [&]( size_t i ) {
        if ( maskNodata && ( filled[i] == nodata || std::isnan( filled[i] ) ) )
            return true;
        return isDirNoData( dir[i] );
    };

    const auto downstreamOf = [&]( size_t i ) -> size_t {
        const int code = static_cast<int>( dir[i] );
        if ( code == 0 )
            return i; // self-loop marks a sink (no downstream)
        const int x = static_cast<int>( i % width );
        const int y = static_cast<int>( i / width );
        for ( const Neighbor &nb : kNeighbors )
            if ( nb.code == code )
            {
                const int nx = x + nb.dx;
                const int ny = y + nb.dy;
                if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                    return i;
                return static_cast<size_t>( ny ) * width + nx;
            }
        return i;
    };

    // NoData cells never enter the routing graph: they are neither sources
    // nor receivers, and their accumulator carries the sentinel so the
    // written GeoTIFF marks them NoData instead of a 1.0 phantom ridge
    // (#783 — ocean/masked cells used to drain nothing yet report 1.0).
    std::vector<int> indegree( n, 0 );
    for ( size_t i = 0; i < n; ++i )
    {
        if ( isNodata( i ) )
        {
            acc[i] = ( maskNodata && ( filled[i] == nodata || std::isnan( filled[i] ) ) ) ? nodata : dir[i];
            continue;
        }
        acc[i] = 1.0f;
        const size_t down = downstreamOf( i );
        if ( down != i && !isNodata( down ) )
            ++indegree[down];
    }
    std::vector<size_t> queue;
    for ( size_t i = 0; i < n; ++i )
        if ( !isNodata( i ) && indegree[i] == 0 )
            queue.push_back( i );
    for ( size_t head = 0; head < queue.size(); ++head )
    {
        const size_t i = queue[head];
        const size_t down = downstreamOf( i );
        if ( down != i && !isNodata( down ) )
        {
            acc[down] += acc[i];
            if ( --indegree[down] == 0 )
                queue.push_back( down );
        }
    }
    return true;
}

bool watershedLabels( const float *dir, int width, int height,
                      const std::vector<std::pair<int, int>> &pourPoints,
                      std::vector<float> *labels )
{
    if ( dir == nullptr || labels == nullptr || width <= 0 || height <= 0 )
        return false;
    const size_t n = static_cast<size_t>( width ) * height;
    labels->assign( n, 0.0f );
    if ( pourPoints.empty() )
        return true;

    // BFS upstream: from every labeled cell, label the neighbors whose D8
    // direction points INTO it. FIFO queue seeded in pour-point order keeps
    // the labelling deterministic (first pour point wins a shared cell).
    std::vector<size_t> queue;
    queue.reserve( n );
    for ( size_t k = 0; k < pourPoints.size(); ++k )
    {
        const int px = pourPoints[k].first;
        const int py = pourPoints[k].second;
        if ( px < 0 || py < 0 || px >= width || py >= height )
            return false;
        const size_t idx = static_cast<size_t>( py ) * width + px;
        if ( ( *labels )[idx] == 0.0f ) // duplicate pour points keep their first label
        {
            ( *labels )[idx] = static_cast<float>( k + 1 );
            queue.push_back( idx );
        }
    }
    for ( size_t head = 0; head < queue.size(); ++head )
    {
        const size_t cur = queue[head];
        const float label = ( *labels )[cur];
        const int cx = static_cast<int>( cur % width );
        const int cy = static_cast<int>( cur / width );
        // A neighbour drains into cur only when its D8 code is the REVERSE
        // of the step cur->neighbour (E<->W, SE<->NW, S<->N, SW<->NE).
        auto reverseCode = []( int code ) {
            switch ( code )
            {
            case 1: return 16;   // E -> W
            case 2: return 32;   // SE -> NW
            case 4: return 64;   // S -> N
            case 8: return 128;  // SW -> NE
            case 16: return 1;   // W -> E
            case 32: return 2;   // NW -> SE
            case 64: return 4;   // N -> S
            default: return 8;   // NE -> SW (128)
            }
        };
        for ( const Neighbor &nb : kNeighbors )
        {
            const int nx = cx + nb.dx;
            const int ny = cy + nb.dy;
            if ( nx < 0 || ny < 0 || nx >= width || ny >= height )
                continue;
            const size_t nIdx = static_cast<size_t>( ny ) * width + nx;
            if ( ( *labels )[nIdx] != 0.0f )
                continue;
            // NoData neighbours carry a NaN direction (flowDirections marks
            // them 0/NaN outside the routing graph) — skip before the
            // float→int cast, whose range must also be validated (#853:
            // finite-but-huge dir values are UB, isfinite alone is not
            // enough).
            if ( !isCastableFlowCode( dir[nIdx] ) )
                continue;
            const int code = static_cast<int>( dir[nIdx] );
            if ( code != reverseCode( nb.code ) )
                continue;
            ( *labels )[nIdx] = label;
            queue.push_back( nIdx );
        }
    }
    return true;
}

} // namespace TerrainFlow
