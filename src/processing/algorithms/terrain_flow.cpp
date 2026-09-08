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
    { 1, 1, std::sqrt( 2.0f ), 2 },
    { 0, 1, 1.0f, 4 },
    { -1, 1, std::sqrt( 2.0f ), 8 },
    { -1, 0, 1.0f, 16 },
    { -1, -1, std::sqrt( 2.0f ), 32 },
    { 0, -1, 1.0f, 64 },
    { 1, -1, std::sqrt( 2.0f ), 128 },
};

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
                continue;
            const bool boundary =
                x == 0 || y == 0 || x == width - 1 || y == height - 1;
            if ( boundary )
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
            dir[i] = 0.0f;
            const float z = filled[i];
            if ( z == nodata || std::isnan( z ) )
                continue;
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
    if ( !dir || !acc || width <= 0 || height <= 0 )
        return false;
    const size_t n = static_cast<size_t>( width ) * height;
    std::vector<int> indegree( n, 0 );

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

    for ( size_t i = 0; i < n; ++i )
    {
        acc[i] = 1.0f;
        const size_t down = downstreamOf( i );
        if ( down != i )
            ++indegree[down];
    }

    std::vector<size_t> queue;
    for ( size_t i = 0; i < n; ++i )
        if ( indegree[i] == 0 )
            queue.push_back( i );
    for ( size_t head = 0; head < queue.size(); ++head )
    {
        const size_t i = queue[head];
        const size_t down = downstreamOf( i );
        if ( down != i )
        {
            acc[down] += acc[i];
            if ( --indegree[down] == 0 )
                queue.push_back( down );
        }
    }
    return true;
}

} // namespace TerrainFlow
