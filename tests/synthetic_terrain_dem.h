// synthetic_terrain_dem.h — closed-form synthetic DEM factory for the terrain
// suites (track terrain-hydrology-11, work package H).
//
// Every generator is a closed-form surface so tests can compute expected
// values by hand/independently — never by calling the implementation under
// test. Coordinates: col ∈ [0,width), row ∈ [0,height), cell units; +y (row)
// points south, matching the raster convention used by the terrain family.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace SyntheticTerrain
{

struct Grid
{
    int width = 0;
    int height = 0;
    float cellSize = 1.0f;
    float nodata = -9999.0f;
    std::vector<float> z; // row-major

    float &at( int col, int row ) { return z[static_cast<std::size_t>( row ) * width + col]; }
    float at( int col, int row ) const
    {
        return z[static_cast<std::size_t>( row ) * width + col];
    }
    std::size_t cells() const { return static_cast<std::size_t>( width ) * height; }
};

/// z = a·col + b·row + c  (a/b in metres per cell).
inline Grid plane( int width, int height, double a, double b, double c,
                   float cellSize = 1.0f )
{
    Grid g;
    g.width = width;
    g.height = height;
    g.cellSize = cellSize;
    g.z.resize( static_cast<std::size_t>( width ) * height );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
            g.at( x, y ) = static_cast<float>( a * x + b * y + c );
    return g;
}

/// Cone opening downward (peak at (cx,cy)): z = h0 − slope·r,
/// r = hypot(col−cx, row−cy) in cell units. slope > 0.
inline Grid cone( int width, int height, double cx, double cy, double h0,
                  double slope, float cellSize = 1.0f )
{
    Grid g;
    g.width = width;
    g.height = height;
    g.cellSize = cellSize;
    g.z.resize( static_cast<std::size_t>( width ) * height );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            const double r = std::hypot( x - cx, y - cy );
            g.at( x, y ) = static_cast<float>( h0 - slope * r );
        }
    return g;
}

/// Pit / bowl (rim at h0): z = −h0 + slope·r — the cone's exact negative
/// offset, giving a single closed depression centred at (cx,cy).
inline Grid pit( int width, int height, double cx, double cy, double h0,
                 double slope, float cellSize = 1.0f )
{
    Grid g;
    g.width = width;
    g.height = height;
    g.cellSize = cellSize;
    g.z.resize( static_cast<std::size_t>( width ) * height );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            const double r = std::hypot( x - cx, y - cy );
            g.at( x, y ) = static_cast<float>( -h0 + slope * r );
        }
    return g;
}

/// Linear V-ridge running north–south through col x0:
/// z = h0 − k·|col − x0|. Crest cells are exactly the x0 column.
inline Grid ridge( int width, int height, double x0, double h0, double k,
                   float cellSize = 1.0f )
{
    Grid g;
    g.width = width;
    g.height = height;
    g.cellSize = cellSize;
    g.z.resize( static_cast<std::size_t>( width ) * height );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
            g.at( x, y ) = static_cast<float>( h0 - k * std::fabs( x - x0 ) );
    return g;
}

/// V-valley (channel) running north–south through col x0, tilted so the
/// thalweg flows north→south (increasing row): z = −k·|col − x0| + g·row.
inline Grid channel( int width, int height, double x0, double k, double gDown,
                     float cellSize = 1.0f )
{
    Grid g;
    g.width = width;
    g.height = height;
    g.cellSize = cellSize;
    g.z.resize( static_cast<std::size_t>( width ) * height );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
            g.at( x, y ) = static_cast<float>( -k * std::fabs( x - x0 ) + gDown * y );
    return g;
}

/// Rectangular plateau (constant c) — the flat-resolution fixture.
inline Grid plateau( int width, int height, double c, float cellSize = 1.0f )
{
    return plane( width, height, 0.0, 0.0, c, cellSize );
}

/// Apply a NoData collar of @p thickness cells around the grid.
inline void applyNodataCollar( Grid &g, int thickness )
{
    for ( int y = 0; y < g.height; ++y )
        for ( int x = 0; x < g.width; ++x )
            if ( x < thickness || y < thickness || x >= g.width - thickness
                 || y >= g.height - thickness )
                g.at( x, y ) = g.nodata;
}

/// Set a single cell to NoData.
inline void pokeNodata( Grid &g, int col, int row ) { g.at( col, row ) = g.nodata; }

} // namespace SyntheticTerrain
