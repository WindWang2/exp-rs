// test_chunk_graph.cpp — Unit tests for the Phase B chunk execution contracts:
// tile grid geometry, bounded queue semantics (backpressure / cancel / close),
// and the streaming pipeline runner (ordering, halo specs, failure and cancel
// propagation, progress accounting, bounded memory).
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/bounded_chunk_queue.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/chunk/tile_spec.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
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

TileSpec baseSpec( int index, int total, int halo = 0, int bands = 1 )
{
    TileSpec s;
    s.index = index;
    s.totalTiles = total;
    s.xOffset = index * 16;
    s.yOffset = 0;
    s.width = 16;
    s.height = 16;
    s.halo = halo;
    s.bufferWidth = s.width + 2 * halo;
    s.bufferHeight = s.height + 2 * halo;
    s.rasterWidth = 16 * total;
    s.rasterHeight = 16;
    s.bands = bands;
    return s;
}
} // namespace

TEST_CASE( "buildTileGrid covers the raster exactly with edge clamping", "[chunk][grid]" )
{
    const auto tiles = buildTileGrid( 1000, 700, 256, 256, 2, 3 );
    REQUIRE( tiles.size() == 4 * 3 );
    REQUIRE( tiles.front().index == 0 );
    REQUIRE( tiles.back().index == static_cast<int>( tiles.size() ) - 1 );
    for ( const TileSpec &t : tiles )
    {
        REQUIRE( t.totalTiles == 12 );
        REQUIRE( t.width > 0 );
        REQUIRE( t.height > 0 );
        REQUIRE( t.bufferWidth == t.width + 4 );
        REQUIRE( t.bufferHeight == t.height + 4 );
        REQUIRE( t.bands == 3 );
        REQUIRE( t.xOffset + t.width <= 1000 );
        REQUIRE( t.yOffset + t.height <= 700 );
    }
    // Row-major order.
    REQUIRE( tiles[0].xOffset == 0 );
    REQUIRE( tiles[1].xOffset == 256 );
    REQUIRE( tiles[0].yOffset == 0 );
    // Row-major visit order means tiles[4] starts the second row.
    REQUIRE( tiles[4].yOffset == 256 );
    REQUIRE( tiles[4].xOffset == 0 );
    // Edge tiles clamped: right column width 1000-768=232.
    REQUIRE( tiles[3].width == 232 );
    REQUIRE( tiles[11].height == 700 - 512 );
}

TEST_CASE( "BoundedChunkQueue enforces capacity (backpressure)", "[chunk][queue]" )
{
    BoundedChunkQueue<int> q( 2 );
    REQUIRE( q.push( 1 ) );
    REQUIRE( q.push( 2 ) );

    std::atomic<bool> thirdPushed{ false };
    std::thread pusher( [&] {
        thirdPushed = q.push( 3 );
    } );
    // Give the pusher a moment: it must be BLOCKED on the bounded queue.
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    REQUIRE( thirdPushed.load() == false );
    int v = 0;
    REQUIRE( q.pop( v ) );
    REQUIRE( v == 1 );
    pusher.join();
    REQUIRE( thirdPushed.load() );
    REQUIRE( q.pop( v ) );
    REQUIRE( v == 2 );
    REQUIRE( q.pop( v ) );
    REQUIRE( v == 3 );
    REQUIRE_FALSE( q.tryPop( v ) );
}

TEST_CASE( "BoundedChunkQueue close releases blocked push and pop", "[chunk][queue]" )
{
    BoundedChunkQueue<int> q( 1 );
    REQUIRE( q.push( 1 ) );

    std::atomic<bool> pushReturned{ false };
    std::thread pusher( [&] {
        pushReturned = q.push( 2 ); // blocks (full), then fails on close
    } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    q.close();
    pusher.join();
    REQUIRE_FALSE( pushReturned.load() );

    int v = 0;
    REQUIRE( q.pop( v ) ); // item enqueued before close survives
    REQUIRE( v == 1 );
    REQUIRE_FALSE( q.pop( v ) ); // closed and drained
    REQUIRE( q.isClosed() );
    REQUIRE_FALSE( q.cancelled() );
}

TEST_CASE( "BoundedChunkQueue cancel wakes waiters and flags cancellation", "[chunk][queue]" )
{
    BoundedChunkQueue<int> q( 1 );
    std::atomic<bool> popped{ true };
    std::thread popper( [&] {
        int v;
        popped = q.pop( v ); // blocks (empty), then fails on cancel
    } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    q.cancel();
    popper.join();
    REQUIRE_FALSE( popped.load() );
    REQUIRE( q.cancelled() );
    int v = 0;
    REQUIRE_FALSE( q.push( 1 ) );
    REQUIRE_FALSE( q.pop( v ) );
}

TEST_CASE( "ChunkPipeline preserves tile order through stages", "[chunk][pipeline]" )
{
    std::atomic<bool> cancel{ false };
    std::vector<int> seenOrder;
    std::mutex seenMutex;

    ChunkPipeline::Config cfg;
    cfg.queueCapacity = 2;
    ChunkPipeline pipeline(
        []( TilePayload &out ) {
            static int next = 0;
            const int total = 64;
            if ( next >= total )
                return false;
            out = makePayload( baseSpec( next, total ), static_cast<float>( next ) );
            ++next;
            return true;
        },
        {
            // Identity stage x3: payloads traverse three bounded queues.
            []( TilePayload &&p ) { return std::move( p ); },
            []( TilePayload &&p ) { return std::move( p ); },
            []( TilePayload &&p ) { return std::move( p ); },
        },
        [&]( TilePayload &&p ) {
            std::lock_guard<std::mutex> lock( seenMutex );
            seenOrder.push_back( static_cast<int>( p.pixels->at( 0 ) ) );
            return true;
        },
        cfg );
    pipeline.setCancelFlag( &cancel );
    pipeline.run();

    REQUIRE( seenOrder.size() == 64 );
    for ( size_t i = 0; i < seenOrder.size(); ++i )
        REQUIRE( seenOrder[i] == static_cast<int>( i ) ); // FIFO preserved end-to-end
    REQUIRE( pipeline.completedTiles() == 64 );
}

TEST_CASE( "ChunkPipeline stages transform payloads and can filter", "[chunk][pipeline]" )
{
    std::vector<float> sums;
    std::mutex sumsMutex;

    ChunkPipeline pipeline(
        []( TilePayload &out ) {
            static int next = 0;
            if ( next >= 8 )
                return false;
            const bool even = next % 2 == 0;
            out = makePayload( baseSpec( next, 8 ), even ? 1.0f : -1.0f );
            ++next;
            return true;
        },
        {
            // Filter: drop negative-fill tiles (odd indices).
            []( TilePayload &&p ) {
                if ( p.pixels->at( 0 ) < 0 )
                    return TilePayload{};
                return std::move( p );
            },
            // Transform: add 10.
            []( TilePayload &&p ) {
                std::fill( p.pixels->begin(), p.pixels->end(), p.pixels->at( 0 ) + 10 );
                return std::move( p );
            },
        },
        [&]( TilePayload &&p ) {
            std::lock_guard<std::mutex> lock( sumsMutex );
            sums.push_back( p.pixels->at( 0 ) );
            return true;
        } );
    pipeline.run();

    REQUIRE( sums.size() == 4 );
    for ( float v : sums )
        REQUIRE( v == 11.0f );
}

TEST_CASE( "ChunkPipeline propagates stage failure without deadlock", "[chunk][pipeline]" )
{
    std::atomic<bool> cancel{ false };
    ChunkPipeline::Config cfg;
    cfg.queueCapacity = 1;
    ChunkPipeline pipeline(
        []( TilePayload &out ) {
            static int next = 0;
            if ( next >= 1000 )
                return false;
            out = makePayload( baseSpec( next++, 1000 ), 1.0f );
            return true;
        },
        {
            []( TilePayload &&p ) {
                if ( p.spec.index == 5 )
                    throw std::runtime_error( "stage exploded on tile 5" );
                return std::move( p );
            },
        },
        []( TilePayload && ) { return true; },
        cfg );
    pipeline.setCancelFlag( &cancel );

    bool threw = false;
    try
    {
        pipeline.run();
    }
    catch ( const std::runtime_error &e )
    {
        threw = std::string( e.what() ).find( "stage exploded" ) != std::string::npos;
    }
    REQUIRE( threw );
}

TEST_CASE( "ChunkPipeline propagates producer failure without deadlock", "[chunk][pipeline]" )
{
    ChunkPipeline pipeline(
        []( TilePayload & ) -> bool { throw std::logic_error( "no raster" ); },
        { []( TilePayload &&p ) { return std::move( p ); } },
        []( TilePayload && ) { return true; } );
    REQUIRE_THROWS_AS( pipeline.run(), std::logic_error );
}

TEST_CASE( "ChunkPipeline consumer abort cancels the stream", "[chunk][pipeline]" )
{
    std::atomic<bool> cancel{ false };
    ChunkPipeline::Config cfg;
    cfg.queueCapacity = 1;
    ChunkPipeline pipeline(
        []( TilePayload &out ) {
            static int next = 0;
            if ( next >= 1000 )
                return false;
            out = makePayload( baseSpec( next++, 1000 ), 1.0f );
            return true;
        },
        {},
        []( TilePayload && ) { return false; }, // abort after first tile
        cfg );
    pipeline.setCancelFlag( &cancel );

    // Consumer abort is a cooperative cancel, not an error: run() returns
    // normally (or throws ChunkCancelled depending on the race window) and
    // must always terminate.
    try
    {
        pipeline.run();
    }
    catch ( const ChunkCancelled & )
    {
    }
    REQUIRE( pipeline.completedTiles() <= 1 );
}

TEST_CASE( "ChunkPipeline external cancel flag stops the stream", "[chunk][pipeline]" )
{
    std::atomic<bool> cancel{ false };
    std::atomic<int> produced{ 0 };
    ChunkPipeline::Config cfg;
    cfg.queueCapacity = 1;
    ChunkPipeline pipeline(
        [&]( TilePayload &out ) {
            out = makePayload( baseSpec( produced.fetch_add( 1 ), 1 << 30 ), 1.0f );
            return true;
        },
        {},
        []( TilePayload && ) { return true; },
        cfg );
    pipeline.setCancelFlag( &cancel );

    std::thread canceller( [&] {
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        cancel = true;
    } );
    REQUIRE_THROWS_AS( pipeline.run(), ChunkCancelled );
    canceller.join();
    REQUIRE( produced.load() < ( 1 << 24 ) ); // terminated, did not run away
}

TEST_CASE( "ChunkPipeline reports progress against totalTiles", "[chunk][pipeline]" )
{
    std::atomic<bool> cancel{ false };
    std::vector<double> ticks;
    std::mutex ticksMutex;
    ChunkPipeline::Config cfg;
    cfg.queueCapacity = 2;
    ChunkPipeline pipeline(
        []( TilePayload &out ) {
            static int next = 0;
            if ( next >= 32 )
                return false;
            out = makePayload( baseSpec( next++, 32 ), 1.0f );
            return true;
        },
        {},
        []( TilePayload && ) { return true; },
        cfg );
    pipeline.setCancelFlag( &cancel );
    pipeline.setProgressCallback( [&]( double p ) {
        std::lock_guard<std::mutex> lock( ticksMutex );
        ticks.push_back( p );
    } );
    pipeline.run();
    REQUIRE( ticks.size() == 32 );
    REQUIRE( ticks.back() == 1.0 );
    // Monotonically nondecreasing.
    for ( size_t i = 1; i < ticks.size(); ++i )
        REQUIRE( ticks[i] >= ticks[i - 1] );
}

TEST_CASE( "ChunkPipeline validates buffer/spec consistency", "[chunk][pipeline]" )
{
    ChunkPipeline pipeline(
        []( TilePayload &out ) {
            TileSpec s = baseSpec( 0, 1 );
            out = TilePayload{ s, std::make_shared<std::vector<float>>( 7 ) }; // wrong size
            return true;
        },
        {},
        []( TilePayload && ) { return true; } );
    REQUIRE_THROWS_AS( pipeline.run(), std::logic_error );
}
