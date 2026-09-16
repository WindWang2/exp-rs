// test_terrain_hydrology.cpp — terrain-hydrology-11 Phase 1: flat resolution,
// D∞ flow, outlets, and stream networks against independent closed-form
// truths. Oracles are derived here by hand/independently (DFS receivers,
// analytic surfaces) — never by calling the kernel under test.

#include "processing/algorithms/terrain_flow.h"
#include "processing/algorithms/terrain_hydrology.h"
#include "synthetic_terrain_dem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>
#include <vector>

using namespace SyntheticTerrain;
using namespace TerrainHydrology;

namespace
{

constexpr float kNo = -9999.0f;

bool isMissingValue( float z, float nodata ) { return z == nodata || std::isnan( z ); }

/// Independent oracle: self-inclusive upstream drainage count per cell over
/// a functional downstream graph (@p receiver: index → receiver index, self
/// = sink). Memoised recursion over the REVERSE graph — a different
/// algorithm from the kernel's Kahn peel.
std::vector<float> upstreamAccumulation( const std::vector<std::size_t> &receiver,
                                         std::size_t n )
{
    std::vector<float> acc( n, 0.0f );
    std::vector<bool> done( n, false );
    // count(i) = 1 + Σ count(j) for all j draining into i.
    std::function<float( std::size_t )> count = [&]( std::size_t i ) -> float {
        if ( done[i] )
            return acc[i];
        acc[i] = 1.0f; // guard against (impossible) cycles: self only
        float total = 1.0f;
        for ( std::size_t j = 0; j < n; ++j )
            if ( receiver[j] == i && j != i )
                total += count( j );
        acc[i] = total;
        done[i] = true;
        return total;
    };
    for ( std::size_t i = 0; i < n; ++i )
        count( i );
    return acc;
}

} // namespace

TEST_CASE( "resolveFlats leaves a strict plane unchanged", "[terrain][hydrology][flats]" )
{
    const Grid g = plane( 9, 7, -1.0, 0.5, 100.0 );
    std::vector<float> filled( g.cells(), 0.0f );
    FlatResolutionReport report;
    REQUIRE( resolveFlats( g.z.data(), filled.data(), g.width, g.height, kNo, &report ) );
    // No depressions, no flats: every cell keeps its own elevation.
    for ( std::size_t i = 0; i < g.cells(); ++i )
        REQUIRE( filled[i] == g.z[i] );
    CHECK( report.raisedCells == 0 );
}

TEST_CASE( "resolveFlats drains a plateau with strict monotone steps",
           "[terrain][hydrology][flats]" )
{
    Grid g = plane( 11, 11, 0.0, 0.0, 50.0 ); // constant plateau
    g.at( 0, 0 ) = 40.0f;                     // one low rim cell = the drain
    std::vector<float> filled( g.cells(), 0.0f );
    FlatResolutionReport report;
    REQUIRE( resolveFlats( g.z.data(), filled.data(), g.width, g.height, kNo, &report ) );
    CHECK( report.epsilon > 0.0 );
    CHECK( report.raisedCells > 0 );

    // Monotone non-decreasing vs input; the drain cell itself untouched.
    for ( std::size_t i = 0; i < g.cells(); ++i )
        REQUIRE( filled[i] >= g.z[i] );
    CHECK( filled[0] == 40.0f );

    // Oracle: D8 on the resolved surface must give every valid INTERIOR
    // cell a strictly descending neighbour — the documented debt (interior
    // flats = permanent sinks) is closed. Boundary cells are the drain
    // itself: rim flats legitimately remain direction-0 outlets.
    // TerrainFlow::flowDirections is the *existing* D8 kernel, used here as
    // a read-back probe, not as the oracle.
    std::vector<float> dir( g.cells(), 0.0f );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), g.width, g.height, kNo ) );
    for ( int y = 1; y < g.height - 1; ++y )
        for ( int x = 1; x < g.width - 1; ++x )
        {
            if ( isMissingValue( filled[y * g.width + x], kNo ) )
                continue;
            INFO( "cell (" << x << "," << y << ") still a sink after flat resolution" );
            REQUIRE( dir[y * g.width + x] != 0.0f );
        }
}

TEST_CASE( "resolveFlats fills a pit to its spill and drains it",
           "[terrain][hydrology][flats]" )
{
    // 7x7 plane tilted toward +y (b = 1: rows descend southward) with a
    // 3x3 pit carved at the centre down to 10.0; surroundings at
    // z = 50 − row ≥ 47. The pit spills at ~the max of its rim level.
    Grid g = plane( 7, 7, 0.0, -1.0, 50.0 );
    for ( int y = 2; y <= 4; ++y )
        for ( int x = 2; x <= 4; ++x )
            g.at( x, y ) = 10.0f;
    std::vector<float> plainFill( g.cells(), 0.0f );
    std::vector<float> flatFill( g.cells(), 0.0f );
    REQUIRE( TerrainFlow::fillDepressions( g.z.data(), plainFill.data(), g.width, g.height, kNo ) );
    FlatResolutionReport report;
    REQUIRE( resolveFlats( g.z.data(), flatFill.data(), g.width, g.height, kNo, &report ) );

    // Both fill the pit to the spill level: the lowest rim of the 3×3 pit is
    // its south rim (row 5, z = 50 − 5 = 45). The epsilon gradient may raise
    // the flat a few steps above the spill along the drain path.
    const float spill = plainFill[3 * 7 + 3];
    CHECK( spill == Catch::Approx( 45.0 ).epsilon( 1e-6 ) );
    CHECK( flatFill[3 * 7 + 3] >= spill );
    CHECK( flatFill[3 * 7 + 3] < spill + 10.0f ); // bounded raise, no runaway
    for ( std::size_t i = 0; i < g.cells(); ++i )
        REQUIRE( flatFill[i] >= g.z[i] );
}

TEST_CASE( "resolveFlats honours NoData barriers and cancellation",
           "[terrain][hydrology][flats]" )
{
    Grid g = plateau( 8, 8, 12.0 );
    applyNodataCollar( g, 2 ); // interior 4x4 valid block
    std::vector<float> filled( g.cells(), 0.0f );
    FlatResolutionReport report;
    REQUIRE( resolveFlats( g.z.data(), filled.data(), g.width, g.height, kNo, &report ) );
    // NoData cells are barriers and stay untouched.
    for ( int y = 0; y < g.height; ++y )
        for ( int x = 0; x < g.width; ++x )
            if ( isMissingValue( g.at( x, y ), kNo ) )
                REQUIRE( filled[y * g.width + x] == kNo );

    // Interior valid cells drain to their own collar boundary: with the
    // collar as the drain boundary, interior cells rise in epsilon steps.
    CHECK( report.raisedCells > 0 );

    // Cancellation must abort the kernel.
    std::vector<float> out( g.cells(), 0.0f );
    const bool ok = resolveFlats(
        g.z.data(), out.data(), g.width, g.height, kNo, nullptr,
        [] { return true; } );
    CHECK_FALSE( ok );
}

TEST_CASE( "D∞ on planes equals the exact gradient azimuth",
           "[terrain][hydrology][dinf]" )
{
    struct Case
    {
        double a;
        double b;
        double azimuth; // compass degrees, clockwise from north
    };
    // Plane z = a·col + b·row descends along (−a, −b) in (col,row); the
    // compass azimuth of (dx,dy) is atan2(dx, −dy). All 8 principal cases,
    // expected azimuths derived by hand.
    const Case cases[] = {
        { -1.0, 0.0, 90.0 },   // z = −col: descent east
        { 1.0, 0.0, 270.0 },   // z = +col: descent west
        { 0.0, 1.0, 0.0 },     // z = +row: descent north
        { 0.0, -1.0, 180.0 },  // z = −row: descent south
        { 1.0, 1.0, 315.0 },   // descent NW
        { -1.0, -1.0, 135.0 }, // descent SE
        { 1.0, -1.0, 225.0 },  // descent SW
        { -1.0, 1.0, 45.0 },   // descent NE
    };
    for ( const Case &c : cases )
    {
        const Grid g = plane( 9, 9, c.a, c.b, 0.0 );
        std::vector<float> angles( g.cells(), 0.0f );
        REQUIRE( flowDirectionInf( g.z.data(), angles.data(), g.width, g.height, kNo ) );
        // Interior cells: every facet wedge containing the descent direction
        // reproduces the plane's exact azimuth — the reported value must be
        // it, within float rounding of the degree value.
        for ( int y = 1; y < g.height - 1; ++y )
            for ( int x = 1; x < g.width - 1; ++x )
            {
                const float a = angles[y * g.width + x];
                INFO( "plane a=" << c.a << " b=" << c.b << " cell (" << x << "," << y
                                 << ") angle=" << a );
                // Either the exact azimuth or its float-rounded value.
                REQUIRE( std::fabs( a - static_cast<float>( c.azimuth ) ) < 1e-3f );
            }
    }
}

TEST_CASE( "D∞ marks plateau cells undecided and NoData passthrough",
           "[terrain][hydrology][dinf]" )
{
    Grid g = plateau( 6, 6, 5.0 );
    pokeNodata( g, 2, 2 );
    std::vector<float> angles( g.cells(), 0.0f );
    REQUIRE( flowDirectionInf( g.z.data(), angles.data(), g.width, g.height, kNo ) );
    for ( int y = 0; y < g.height; ++y )
        for ( int x = 0; x < g.width; ++x )
        {
            const float a = angles[y * g.width + x];
            if ( isMissingValue( g.at( x, y ), kNo ) )
                CHECK( a == kNo );
            else
                CHECK( a == -1.0f );
        }
}

TEST_CASE( "D∞ accumulation mass identity on a plane (independent DFS oracle)",
           "[terrain][hydrology][dinf]" )
{
    // z = +col: every cell flows west; receiver = west neighbour (azimuth
    // 270 → theta 0 edge). Independent oracle: chain length per row is
    // (width − col): col=0 → width … col=width−1 → 1.
    const int w = 8;
    const int h = 5;
    const Grid g = plane( w, h, 1.0, 0.0, 0.0 );
    std::vector<float> angles( g.cells(), 0.0f );
    REQUIRE( flowDirectionInf( g.z.data(), angles.data(), w, h, kNo ) );
    std::vector<float> acc( g.cells(), 0.0f );
    REQUIRE( dInfAccumulation( angles.data(), acc.data(), w, h, kNo ) );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            INFO( "cell (" << x << "," << y << ")" );
            CHECK( acc[y * w + x] == Catch::Approx( static_cast<float>( w - x ) ) );
        }
}

TEST_CASE( "D∞ accumulation collects a full pit (independent receiver oracle)",
           "[terrain][hydrology][dinf]" )
{
    // Unfilled pit: every cell descends toward the centre; the centre is a
    // sink → its accumulation must equal the number of valid cells. The
    // oracle cross-checks EVERY cell against a hand-walked receiver chain.
    const int w = 11;
    const int h = 11;
    const Grid g = pit( w, h, 5.0, 5.0, 20.0, 2.0 );
    std::vector<float> angles( g.cells(), 0.0f );
    REQUIRE( flowDirectionInf( g.z.data(), angles.data(), w, h, kNo ) );

    std::vector<std::size_t> receiver( g.cells() );
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const auto [rx, ry] = dInfReceiver( angles.data(), w, h, kNo, x, y );
            receiver[y * w + x] = static_cast<std::size_t>( ry ) * w + rx;
        }
    const std::vector<float> oracle = upstreamAccumulation( receiver, g.cells() );

    std::vector<float> acc( g.cells(), 0.0f );
    REQUIRE( dInfAccumulation( angles.data(), acc.data(), w, h, kNo ) );
    for ( std::size_t i = 0; i < g.cells(); ++i )
    {
        INFO( "cell " << i << " acc=" << acc[i] << " oracle=" << oracle[i] );
        CHECK( acc[i] == Catch::Approx( oracle[i] ).epsilon( 1e-6 ) );
    }
    CHECK( acc[5 * w + 5] == Catch::Approx( static_cast<float>( g.cells() ) ) );
}

TEST_CASE( "detectOutlets finds the pit and the rim spill cells",
           "[terrain][hydrology][outlets]" )
{
    // Pit grid without fill: the centre is the only interior sink.
    const int w = 9;
    const int h = 9;
    const Grid g = pit( w, h, 4.0, 4.0, 10.0, 1.0 );
    std::vector<float> filled( g.cells(), 0.0f );
    REQUIRE( TerrainFlow::fillDepressions( g.z.data(), filled.data(), w, h, kNo ) );
    // After filling, the pit became a flat at spill level → still a sink.
    std::vector<float> dir( g.cells(), 0.0f );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), w, h, kNo ) );
    const auto outlets = detectOutlets( filled.data(), dir.data(), w, h, kNo );
    bool pitFound = false;
    for ( const Outlet &o : outlets )
        if ( o.col == 4 && o.row == 4 )
            pitFound = true;
    CHECK( pitFound );
    // After flat resolution the pit drains; only rim/outflow cells remain.
    std::vector<float> resolved( g.cells(), 0.0f );
    REQUIRE( resolveFlats( g.z.data(), resolved.data(), w, h, kNo ) );
    std::vector<float> dir2( g.cells(), 0.0f );
    REQUIRE( TerrainFlow::flowDirections( resolved.data(), dir2.data(), w, h, kNo ) );
    const auto outlets2 = detectOutlets( resolved.data(), dir2.data(), w, h, kNo );
    for ( const Outlet &o : outlets2 )
    {
        INFO( "outlet (" << o.col << "," << o.row << ") atRim=" << o.atRim );
        CHECK( o.atRim );
    }
    CHECK_FALSE( outlets2.empty() ); // water must leave somewhere
}

TEST_CASE( "streamNetwork Strahler orders a hand-built Y junction",
           "[terrain][hydrology][streams]" )
{
    // Hand-built D8 directions (ESRI codes) on a 7x7 grid:
    //   two order-1 branches join at (3,3) and continue south as order 2.
    //   Branch A: (3,0)→(3,1)→(3,2)→(3,3)  (codes 4 = S)
    //   Branch B: (1,2)→(2,2)? diag SE=2 → (3,3): (1,2)→(2,3)?… use
    //   B: (1,1)→(2,2)(SE=2)→(3,3)(SE=2)
    //   Main stem: (3,3)→(3,4)→(3,5)→(3,6) (S=4)
    const int w = 7;
    const int h = 7;
    const size_t n = static_cast<size_t>( w ) * h;
    std::vector<float> dir( n, 0.0f );
    auto set = [&]( int x, int y, int code ) { dir[y * w + x] = static_cast<float>( code ); };
    set( 3, 0, 4 );  // A head
    set( 3, 1, 4 );
    set( 3, 2, 4 );
    set( 1, 1, 2 );  // B head
    set( 2, 2, 2 );
    set( 3, 3, 4 );  // junction, continues south
    set( 3, 4, 4 );
    set( 3, 5, 4 );
    set( 3, 6, 0 );  // outlet

    StreamNetwork net;
    // Accumulation: consistent self-inclusive counts along the chains.
    // Index convention: acc[row * w + col].
    std::vector<float> acc( n, 1.0f );
    acc[0 * w + 3] = 4; acc[1 * w + 3] = 3; acc[2 * w + 3] = 2;          // A (col 3, rows 0-2)
    acc[1 * w + 1] = 3; acc[2 * w + 2] = 2;                              // B
    acc[3 * w + 3] = 8; acc[4 * w + 3] = 9; acc[5 * w + 3] = 10; acc[6 * w + 3] = 11;

    // Threshold 5: no branch-A cell qualifies (max acc 4) — the main stem
    // survives alone. Threshold 2 keeps everything.
    REQUIRE( streamNetwork( dir.data(), acc.data(), w, h, nullptr, 0.0f, 5.0f, &net ) );
    CHECK( net.isStream[3 * w + 0] == 0 );
    CHECK( net.isStream[3 * w + 3] != 0 );
    REQUIRE( streamNetwork( dir.data(), acc.data(), w, h, nullptr, 0.0f, 2.0f, &net ) );
    const auto isS = [&]( int x, int y ) { return net.isStream[y * w + x] != 0; };
    const auto ord = [&]( int x, int y ) { return net.strahler[y * w + x]; };
    CHECK( isS( 3, 0 ) );
    CHECK( isS( 3, 1 ) );
    CHECK( isS( 3, 2 ) ); // A
    CHECK( isS( 1, 1 ) );
    CHECK( isS( 2, 2 ) ); // B
    CHECK( isS( 3, 3 ) );
    CHECK( isS( 3, 4 ) );
    CHECK( isS( 3, 5 ) );
    CHECK( isS( 3, 6 ) );
    CHECK_FALSE( isS( 0, 0 ) );
    // Strahler: heads = 1; junction gets max(1,1)+1 = 2; stem keeps 2.
    CHECK( ord( 3, 0 ) == 1 );
    CHECK( ord( 1, 1 ) == 1 );
    CHECK( ord( 3, 3 ) == 2 );
    CHECK( ord( 3, 6 ) == 2 );
}

TEST_CASE( "streamSegments vectorizes the Y network into three links",
           "[terrain][hydrology][streams]" )
{
    const int w = 7;
    const int h = 7;
    const size_t n = static_cast<size_t>( w ) * h;
    std::vector<float> dir( n, 0.0f );
    auto set = [&]( int x, int y, int code ) { dir[y * w + x] = static_cast<float>( code ); };
    set( 3, 0, 4 );
    set( 3, 1, 4 );
    set( 3, 2, 4 );
    set( 1, 1, 2 );
    set( 2, 2, 2 );
    set( 3, 3, 4 );
    set( 3, 4, 4 );
    set( 3, 5, 4 );
    set( 3, 6, 0 );

    StreamNetwork net;
    // Same accumulation fixture; threshold 2 selects exactly the 9 network
    // cells (threshold 1 would sweep in every non-network cell as an
    // isolated single-cell link).
    std::vector<float> acc( n, 1.0f );
    acc[0 * w + 3] = 4; acc[1 * w + 3] = 3; acc[2 * w + 3] = 2;
    acc[1 * w + 1] = 3; acc[2 * w + 2] = 2;
    acc[3 * w + 3] = 8; acc[4 * w + 3] = 9; acc[5 * w + 3] = 10; acc[6 * w + 3] = 11;
    REQUIRE( streamNetwork( dir.data(), acc.data(), w, h, nullptr, 0.0f, 2.0f, &net ) );
    std::vector<StreamSegment> segments;
    REQUIRE( streamSegments( dir.data(), w, h, kNo, net, &segments ) );
    REQUIRE( segments.size() == 3 );

    // Every stream cell appears exactly once across the links.
    std::vector<int> seen( n, 0 );
    int streamCells = 0;
    for ( std::size_t i = 0; i < n; ++i )
        streamCells += net.isStream[i];
    for ( const StreamSegment &s : segments )
    {
        CHECK( s.cells.size() >= 2 ); // links run head/junction → junction/outlet
        for ( const auto [cx, cy] : s.cells )
            seen[static_cast<std::size_t>( cy ) * w + cx] += 1;
    }
    int covered = 0;
    int multi = 0;
    for ( std::size_t i = 0; i < n; ++i )
    {
        if ( seen[i] > 0 )
            covered++;
        if ( seen[i] > 1 )
            multi++;
    }
    CHECK( covered == streamCells );
    CHECK( multi == 0 );

    // Link orders: two tributaries of order 1 and the junction→outlet link 2.
    std::vector<int> orders;
    for ( const StreamSegment &s : segments )
        orders.push_back( s.order );
    std::sort( orders.begin(), orders.end() );
    CHECK( orders == std::vector<int>{ 1, 1, 2 } );
}

TEST_CASE( "hermetic scale: one-exit funnel drains the whole grid",
           "[.scale][terrain][hydrology]" )
{
    // Opt-in scale evidence (SICNU_TERRAIN_SCALE_TESTS=ON): a 2048² plane
    // z = col + row descends toward the NW corner, which is the single
    // grid-exit pit — the full mass must arrive there under both D8 and D∞.
    if ( !std::getenv( "SICNU_TERRAIN_SCALE_TESTS" ) )
        SKIP();
    const int w = 2048;
    const int h = 2048;
    const std::size_t n = static_cast<std::size_t>( w ) * h;
    Grid g = plane( w, h, 1.0, 1.0, 0.0 );
    std::vector<float> filled( n, 0.0f );
    FlatResolutionReport report;
    REQUIRE( resolveFlats( g.z.data(), filled.data(), w, h, kNo, &report ) );
    std::vector<float> dir( n, 0.0f );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), w, h, kNo ) );
    std::vector<float> acc( n, 0.0f );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), w, h,
                                            filled.data(), kNo ) );
    CHECK( acc[0] == Catch::Approx( static_cast<float>( n ) ) );

    std::vector<float> angles( n, 0.0f );
    REQUIRE( flowDirectionInf( filled.data(), angles.data(), w, h, kNo ) );
    std::vector<float> accInf( n, 0.0f );
    REQUIRE( dInfAccumulation( angles.data(), accInf.data(), w, h, kNo ) );
    // Monotonicity along the flow graph: a receiver's count is never below
    // its contributor's (mass never decreases downstream).
    for ( int y = 0; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const auto [rx, ry] = dInfReceiver( angles.data(), w, h, kNo, x, y );
            if ( rx == x && ry == y )
                continue;
            const float down = accInf[static_cast<std::size_t>( ry ) * w + rx];
            const float up = accInf[static_cast<std::size_t>( y ) * w + x];
            if ( down < up )
            {
                FAIL( "mass decreased downstream at (" << x << "," << y
                                                       << "): " << up << " -> " << down );
            }
        }
    // The corner funnel collects everything under D∞ as well.
    CHECK( accInf[0] == Catch::Approx( static_cast<float>( n ) ) );
}

TEST_CASE( "streamNetwork excludes DEM NoData cells", "[terrain][hydrology][streams]" )
{
    const int w = 5;
    const int h = 5;
    Grid g = plane( w, h, 1.0, 0.0, 0.0 ); // descends east
    pokeNodata( g, 3, 2 );
    std::vector<float> filled( g.cells(), 0.0f );
    REQUIRE( TerrainFlow::fillDepressions( g.z.data(), filled.data(), w, h, kNo ) );
    std::vector<float> dir( g.cells(), 0.0f );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), w, h, kNo ) );
    std::vector<float> acc( g.cells(), 0.0f );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), w, h,
                                            filled.data(), kNo ) );
    StreamNetwork net;
    REQUIRE( streamNetwork( dir.data(), acc.data(), w, h, filled.data(), kNo, 1.0f, &net ) );
    CHECK( net.isStream[2 * w + 3] == 0 ); // NoData never joins the network
    CHECK( net.strahler[2 * w + 3] == 0 );
}
