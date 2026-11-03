// test_r27_t02_chunk_graph.cpp — R27-T2 track tests for the chunk graph
// build-time topology contract (#1743 / RIO-1 + RIO-3):
//
//   * addJoin({A,A}) and any intra-call duplicate input must throw
//     std::logic_error BEFORE the join node is registered (strong
//     exception guarantee: a rejected join leaves the inputs consumable).
//   * an empty join input vector is a runtime rejection, not an assert.
//   * the LEGAL shapes keep working after the guards: aligned A+B join,
//     cooperative cancellation, bounded-queue backpressure, and the
//     explicit-copy fan-out idiom (two independent sources over one grid).
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/bounded_chunk_queue.h"
#include "runtime/chunk/chunk_graph.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/chunk/tile_spec.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace sicnu::runtime::chunk;

namespace
{
TilePayload makePayload( const TileSpec &spec, float fill )
{
    auto buf = std::make_shared<std::vector<float>>( spec.bufferElementCount(), fill );
    return TilePayload{ spec, std::move( buf ) };
}

TileSpec rowSpec( int index, int total, int width = 8, int bands = 1 )
{
    TileSpec s;
    s.index = index;
    s.totalTiles = total;
    s.xOffset = index * width;
    s.yOffset = 0;
    s.width = width;
    s.height = 8;
    s.bufferWidth = width;
    s.bufferHeight = 8;
    s.rasterWidth = width * total;
    s.rasterHeight = 8;
    s.bands = bands;
    return s;
}

/// Source emitting exactly @p total tiles filled with @p fill.
auto countedSource( int total, float fill )
{
    return [i = 0, total, fill]( TilePayload &out ) mutable
    {
        if ( i >= total )
            return false;
        out = makePayload( rowSpec( i, total ), fill );
        ++i;
        return true;
    };
}
} // namespace

// ---------------------------------------------------------------------------
// #1743 / RIO-1: intra-call duplicate join inputs are rejected at build time
// ---------------------------------------------------------------------------

TEST_CASE( "#1743 addJoin({A,A}) throws std::logic_error before registering",
           "[chunk][graph][r27t02]" )
{
    ChunkGraph graph;
    const auto src = graph.addSource( countedSource( 2, 1.f ) );
    REQUIRE_THROWS_AS( graph.addJoin( { src, src }, []( std::vector<TilePayload> && )
                                      { return TilePayload{}; } ),
                       std::logic_error );
}

TEST_CASE( "#1743 addJoin rejects a non-adjacent duplicate ({A,B,A})",
           "[chunk][graph][r27t02]" )
{
    ChunkGraph graph;
    const auto a = graph.addSource( countedSource( 2, 1.f ) );
    const auto b = graph.addSource( countedSource( 2, 2.f ) );
    REQUIRE_THROWS_AS( graph.addJoin( { a, b, a }, []( std::vector<TilePayload> && )
                                      { return TilePayload{}; } ),
                       std::logic_error );
}

TEST_CASE( "#1743 a rejected duplicate join leaves the inputs consumable (strong guarantee)",
           "[chunk][graph][r27t02]" )
{
    ChunkGraph graph;
    const auto a = graph.addSource( countedSource( 2, 1.f ) );
    const auto b = graph.addSource( countedSource( 2, 2.f ) );
    // The duplicate join must not have registered A as consumed...
    REQUIRE_THROWS_AS( graph.addJoin( { a, a }, []( std::vector<TilePayload> && )
                                      { return TilePayload{}; } ),
                       std::logic_error );
    // ...so the LEGAL single consumer of A still attaches and runs.
    const auto join = graph.addJoin( { a, b },
                                     []( std::vector<TilePayload> &&tiles )
                                     {
                                         REQUIRE( tiles.size() == 2 );
                                         return makePayload( tiles[0].spec,
                                                             tiles[0].pixels->front()
                                                                 + tiles[1].pixels->front() );
                                     } );
    REQUIRE( join > 0 );
    int sunk = 0;
    graph.addSink( join,
                   [&sunk]( TilePayload && )
                   {
                       ++sunk;
                       return true;
                   } );
    graph.run();
    REQUIRE( sunk == 2 );
}

TEST_CASE( "#1743 addJoin rejects out-of-range node ids at runtime",
           "[chunk][graph][r27t02]" )
{
    ChunkGraph graph;
    const auto src = graph.addSource( countedSource( 1, 1.f ) );
    REQUIRE_THROWS_AS( graph.addJoin( { src, src + 100 }, []( std::vector<TilePayload> && )
                                      { return TilePayload{}; } ),
                       std::logic_error );
    REQUIRE_THROWS_AS( graph.addJoin( { -1 }, []( std::vector<TilePayload> && )
                                      { return TilePayload{}; } ),
                       std::logic_error );
}

TEST_CASE( "#1803 RIO-3 addJoin({}) is a runtime rejection, not an assert",
           "[chunk][graph][r27t02]" )
{
    ChunkGraph graph;
    const auto src = graph.addSource( countedSource( 1, 1.f ) );
    (void)src;
    // NDEBUG release builds compile asserts out; the empty-join contract
    // must hold there too.
    REQUIRE_THROWS_AS( graph.addJoin( {}, []( std::vector<TilePayload> && )
                                      { return TilePayload{}; } ),
                       std::logic_error );
}

// ---------------------------------------------------------------------------
// Legal shapes that must keep working (regression guards for the new gate)
// ---------------------------------------------------------------------------

TEST_CASE( "#1743 legal A+B join stays tuple-aligned after the guard",
           "[chunk][graph][r27t02]" )
{
    ChunkGraph graph;
    const auto a = graph.addSource( countedSource( 4, 1.f ) );
    const auto b = graph.addSource( countedSource( 4, 10.f ) );
    const auto join = graph.addJoin( { a, b },
                                     []( std::vector<TilePayload> &&tiles )
                                     {
                                         TilePayload out = makePayload( tiles[0].spec, 0.f );
                                         for ( size_t i = 0; i < out.pixels->size(); ++i )
                                             ( *out.pixels )[i] = ( *tiles[0].pixels )[i]
                                                                  + ( *tiles[1].pixels )[i];
                                         return out;
                                     } );
    std::vector<int> seenIndexes;
    graph.addSink( join,
                   [&seenIndexes]( TilePayload &&tile )
                   {
                       seenIndexes.push_back( tile.spec.index );
                       return true;
                   } );
    graph.run();
    REQUIRE( seenIndexes == std::vector<int>{ 0, 1, 2, 3 } );
}

TEST_CASE( "#1743 explicit-copy fan-out idiom: two independent sources over one grid join cleanly",
           "[chunk][graph][r27t02]" )
{
    // Fan-out of ONE queue is forbidden (silent tile split); the sanctioned
    // idiom is two sources producing the SAME grid partition. The duplicate
    // gate must not confuse that legal shape with {A,A}.
    ChunkGraph graph;
    const auto left = graph.addSource( countedSource( 3, 5.f ) );
    const auto right = graph.addSource( countedSource( 3, 7.f ) );
    const auto join = graph.addJoin( { left, right },
                                     []( std::vector<TilePayload> &&tiles )
                                     { return makePayload( tiles[0].spec, 12.f ); } );
    int sunk = 0;
    graph.addSink( join,
                   [&sunk]( TilePayload &&tile )
                   {
                       REQUIRE( tile.pixels );
                       REQUIRE( tile.pixels->front() == 12.f );
                       ++sunk;
                       return true;
                   } );
    graph.run();
    REQUIRE( sunk == 3 );
}

TEST_CASE( "#1743 cooperative cancel still unwinds a join graph",
           "[chunk][graph][r27t02]" )
{
    ChunkGraph graph;
    auto cancel = std::make_shared<std::atomic<bool>>( false );
    graph.setCancelFlag( cancel.get() );
    const auto a = graph.addSource( [n = 0, cancel]( TilePayload &out ) mutable
                                    {
                                        if ( n >= 64 )
                                            return false;
                                        if ( n == 4 )
                                            cancel->store( true );
                                        out = makePayload( rowSpec( n, 64 ), 1.f );
                                        ++n;
                                        return true;
                                    } );
    const auto b = graph.addSource( [n = 0]( TilePayload &out ) mutable
                                    {
                                        if ( n >= 64 )
                                            return false;
                                        out = makePayload( rowSpec( n, 64 ), 1.f );
                                        ++n;
                                        return true;
                                    } );
    const auto join = graph.addJoin( { a, b },
                                     []( std::vector<TilePayload> &&tiles )
                                     { return makePayload( tiles[0].spec, 1.f ); } );
    graph.addSink( join, []( TilePayload && ) { return true; } );
    REQUIRE_THROWS_AS( graph.run(), ChunkCancelled );
}

TEST_CASE( "#1743 fan-out guard still fires across separate calls after a duplicate join",
           "[chunk][graph][r27t02]" )
{
    // The duplicate gate must not weaken the F-A-10 inter-call guard: a
    // join registered first still owns its inputs.
    ChunkGraph graph;
    const auto a = graph.addSource( countedSource( 2, 1.f ) );
    const auto b = graph.addSource( countedSource( 2, 1.f ) );
    (void)graph.addJoin( { a, b }, []( std::vector<TilePayload> && )
                         { return TilePayload{}; } );
    REQUIRE_THROWS_AS( graph.addStage( a, []( TilePayload &&p ) { return p; } ),
                       std::logic_error );
    REQUIRE_THROWS_AS( graph.addSink( a, []( TilePayload && ) { return true; } ),
                       std::logic_error );
}
