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
#include <cstdint>
#include <cstring>
#include <memory>
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

    // Consumer abort is fail-closed (11.0 convergence): run() must throw
    // ChunkConsumerAborted (a ChunkCancelled subtype) — a stalled or failing
    // sink can never be misread as a completed stream.
    REQUIRE_THROWS_AS( pipeline.run(), ChunkConsumerAborted );
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

// ---------------------------------------------------------------------------
// ChunkGraph — multi-input tile DAG (LSEE 10.0, ADR 0148)
// ---------------------------------------------------------------------------

#include "runtime/chunk/chunk_graph.h"
#include "runtime/chunk/memory_planner.h"
#include "runtime/chunk/multi_pass_reduction.h"

namespace
{
/// Emits `count` row-major tiles filled with `fill`. Each call returns an
/// independent counter (no shared statics).
ChunkGraph::SourceFn countingSource( int count, float fill, int bandOffset = 0, int timeIndex = 0 )
{
    auto next = std::make_shared<std::atomic<int>>( 0 );
    return [count, fill, bandOffset, timeIndex, next]( TilePayload &out ) {
        const int index = next->fetch_add( 1 );
        if ( index >= count )
            return false;
        TileSpec s = baseSpec( index, count );
        s.bandOffset = bandOffset;
        s.timeIndex = timeIndex;
        out = makePayload( s, fill );
        return true;
    };
}
} // namespace

TEST_CASE( "ChunkGraph joins two sources tuple-aligned in row-major order", "[chunk][graph]" )
{
    ChunkGraph::Config config;
    config.queueCapacity = 2;
    ChunkGraph graph( config );

    constexpr int kTiles = 64;
    auto srcA = graph.addSource( countingSource( kTiles, 1.0f ) );
    auto srcB = graph.addSource( countingSource( kTiles, 2.0f ) );
    auto join = graph.addJoin( { srcA, srcB }, []( std::vector<TilePayload> &&tiles ) {
        TileSpec spec = tiles.front().spec;
        spec.bands = 1;
        auto sum = std::make_shared<std::vector<float>>( spec.bufferElementCount() );
        const auto &a = *tiles[0].pixels;
        const auto &b = *tiles[1].pixels;
        for ( size_t i = 0; i < sum->size(); ++i )
            ( *sum )[i] = a[i] + b[i];
        return TilePayload{ spec, std::move( sum ) };
    } );

    std::vector<float> sums;
    graph.addSink( join, [&]( TilePayload && payload ) {
        sums.push_back( payload.pixels->at( 0 ) );
        return true;
    } );
    graph.run();

    REQUIRE( sums.size() == static_cast<size_t>( kTiles ) );
    // Tuple alignment: every output tile is 1.0 + 2.0 regardless of timing.
    for ( float value : sums )
        REQUIRE( value == 3.0f );
    REQUIRE( graph.completedTiles() == static_cast<size_t>( kTiles ) );
}

TEST_CASE( "ChunkGraph bandOffset/timeIndex ride through stages", "[chunk][graph]" )
{
    ChunkGraph graph;
    auto src = graph.addSource( countingSource( 4, 1.0f, /*bandOffset=*/2, /*timeIndex=*/7 ) );
    auto stage = graph.addStage( src, []( TilePayload && p ) { return std::move( p ); } );
    std::vector<std::pair<int, int>> identity;
    graph.addSink( stage, [&]( TilePayload && payload ) {
        identity.emplace_back( payload.spec.bandOffset, payload.spec.timeIndex );
        return true;
    } );
    graph.run();
    REQUIRE( identity.size() == 4 );
    for ( const auto &entry : identity )
    {
        REQUIRE( entry.first == 2 );
        REQUIRE( entry.second == 7 );
    }
}

TEST_CASE( "ChunkGraph join partition mismatch is typed and never deadlocks", "[chunk][graph]" )
{
    ChunkGraph graph;
    auto srcA = graph.addSource( countingSource( 8, 1.0f ) );
    auto srcB = graph.addSource( countingSource( 5, 2.0f ) ); // short producer
    auto join = graph.addJoin( { srcA, srcB }, []( std::vector<TilePayload> && tiles ) {
        TileSpec spec = tiles.front().spec;
        return makePayload( spec, 0.0f );
    } );
    std::atomic<bool> sinkCalled{ false };
    graph.addSink( join, [&]( TilePayload && ) { sinkCalled = true; return true; } );
    REQUIRE_THROWS_AS( graph.run(), ChunkPartitionMismatch );
}

TEST_CASE( "ChunkGraph stage failure cancels every node and rethrows", "[chunk][graph]" )
{
    ChunkGraph graph;
    auto srcA = graph.addSource( countingSource( 32, 1.0f ) );
    auto srcB = graph.addSource( countingSource( 32, 1.0f ) );
    auto join = graph.addJoin( { srcA, srcB }, []( std::vector<TilePayload> && tiles ) {
        return makePayload( tiles.front().spec, 0.0f );
    } );
    auto stage = graph.addStage( join, []( TilePayload && ) -> TilePayload {
        throw std::runtime_error( "kernel boom" );
    } );
    graph.addSink( stage, []( TilePayload && ) { return true; } );
    REQUIRE_THROWS_AS( graph.run(), std::runtime_error );
}

TEST_CASE( "ChunkGraph consumer abort unwinds the whole graph", "[chunk][graph]" )
{
    ChunkGraph graph;
    auto src = graph.addSource( countingSource( 1000, 1.0f ) );
    std::atomic<int> seen{ 0 };
    graph.addSink( src, [&]( TilePayload && ) {
        return ++seen < 5; // abort after 5 — same contract as ChunkPipeline's
                           // consumer abort: the run ends with a cancel error
    } );
    REQUIRE_THROWS_AS( graph.run(), ChunkGraphCancelled );
    REQUIRE( seen == 5 );
    REQUIRE( graph.completedTiles() == 5 );
}

TEST_CASE( "ChunkGraph external cancel flag stops the stream", "[chunk][graph]" )
{
    std::atomic<bool> cancel{ false };
    ChunkGraph graph;
    graph.setCancelFlag( &cancel );
    auto src = graph.addSource( countingSource( 100000, 1.0f ) );
    graph.addSink( src, [&]( TilePayload && ) {
        cancel = true; // cancel from the consumer mid-stream
        return true;
    } );
    REQUIRE_THROWS_AS( graph.run(), ChunkGraphCancelled );
}

TEST_CASE( "ChunkGraph slow-input backpressure: fast source parks in a bounded queue", "[chunk][graph]" )
{
    ChunkGraph::Config config;
    config.queueCapacity = 1;
    ChunkGraph graph( config );
    auto fast = graph.addSource( countingSource( 16, 1.0f ) );
    int next = 0;
    ChunkGraph::SourceFn slowFn = [&next]( TilePayload &out ) {
        std::this_thread::sleep_for( std::chrono::microseconds( 200 ) );
        if ( next >= 16 )
            return false;
        out = makePayload( baseSpec( next, 16 ), 2.0f );
        ++next;
        return true;
    };
    auto slow = graph.addSource( slowFn );
    auto join = graph.addJoin( { fast, slow }, []( std::vector<TilePayload> && tiles ) {
        return makePayload( tiles.front().spec, tiles[0].pixels->at( 0 ) + tiles[1].pixels->at( 0 ) );
    } );
    int sinkCount = 0;
    graph.addSink( join, [&]( TilePayload && ) { ++sinkCount; return true; } );
    graph.run();
    REQUIRE( sinkCount == 16 );
}

// ---------------------------------------------------------------------------
// Memory planner (LSEE 10.0, ADR 0148 §3)
// ---------------------------------------------------------------------------

TEST_CASE( "planTileMemory admits the requested shape within budget", "[chunk][planner]" )
{
    TileMemoryRequest request;
    request.tileWidth = 256;
    request.tileHeight = 256;
    request.bands = 1;
    request.bytesPerSample = 4;
    request.stageCount = 1;
    request.requestedQueueCapacity = 2;
    request.budgetBytes = tileStreamPeakBytes( request, 2 ) + 1;
    const TileMemoryPlan plan = planTileMemory( request );
    REQUIRE( plan.action == TileMemoryPlan::Action::Admit );
    REQUIRE( plan.reason.empty() );
}

TEST_CASE( "planTileMemory reduces queue capacity before refusing", "[chunk][planner]" )
{
    TileMemoryRequest request;
    request.tileWidth = 256;
    request.tileHeight = 256;
    request.haloPixels = 4;
    request.bands = 8;
    request.bytesPerSample = 4;
    request.stageCount = 1;
    request.requestedQueueCapacity = 4;
    const std::uint64_t q4 = tileStreamPeakBytes( request, 4 );
    const std::uint64_t q1 = tileStreamPeakBytes( request, 1 );
    REQUIRE( q1 < q4 );
    request.budgetBytes = q1 + 8; // fits only at the minimal shape
    const TileMemoryPlan plan = planTileMemory( request );
    REQUIRE( plan.action == TileMemoryPlan::Action::ReduceConcurrency );
    REQUIRE( plan.recommendedQueueCapacity == 1 );
    REQUIRE( plan.estimatedPeakBytes == q1 );
    REQUIRE( plan.reason.find( "reduced to queueCapacity=1" ) != std::string::npos );
}

TEST_CASE( "planTileMemory spills when scratch covers the intermediate", "[chunk][planner]" )
{
    TileMemoryRequest request;
    request.tileWidth = 256;
    request.tileHeight = 256;
    request.bands = 16;
    request.bytesPerSample = 4;
    request.expectedTileCount = 10;
    request.allowSpill = true;
    request.budgetBytes = tileStreamPeakBytes( request, 1 ) - 1; // below minimum
    request.scratchBudgetBytes = tileStreamPeakBytes( request, 1 ) * 100;
    const TileMemoryPlan plan = planTileMemory( request );
    REQUIRE( plan.action == TileMemoryPlan::Action::Spill );
    REQUIRE( plan.spillBytes > 0 );
    REQUIRE( plan.reason.find( "scratch" ) != std::string::npos );
}

TEST_CASE( "planTileMemory refuses with a structured need/have reason", "[chunk][planner]" )
{
    TileMemoryRequest request;
    request.tileWidth = 256;
    request.tileHeight = 256;
    request.bands = 16;
    request.allowSpill = false;
    request.budgetBytes = 1024; // 1 KiB: far below one tile
    const TileMemoryPlan plan = planTileMemory( request );
    REQUIRE( plan.action == TileMemoryPlan::Action::Refuse );
    REQUIRE( plan.reason.find( "needs" ) != std::string::npos );
    REQUIRE( plan.reason.find( "budget" ) != std::string::npos );
    REQUIRE( plan.reason.find( "tile 256x256" ) != std::string::npos );
}

TEST_CASE( "planTileMemory advisory mode never gates", "[chunk][planner]" )
{
    TileMemoryRequest request;
    request.budgetBytes = 0; // advisory
    request.requestedQueueCapacity = 2;
    const TileMemoryPlan plan = planTileMemory( request );
    REQUIRE( plan.action == TileMemoryPlan::Action::Advisory );
    REQUIRE( plan.fits() );
    REQUIRE( plan.estimatedPeakBytes == plan.requestedPeakBytes );
}

TEST_CASE( "planner arithmetic saturates instead of wrapping", "[chunk][planner]" )
{
    REQUIRE( saturatingMul( UINT64_MAX, 2 ) == UINT64_MAX );
    REQUIRE( saturatingMul( 1ull << 32, 1ull << 32 ) == UINT64_MAX );
    REQUIRE( saturatingAdd( UINT64_MAX, 1 ) == UINT64_MAX );

    TileMemoryRequest huge;
    huge.tileWidth = UINT32_MAX;
    huge.tileHeight = UINT32_MAX;
    huge.bands = UINT32_MAX;
    huge.bytesPerSample = 8;
    huge.budgetBytes = 1024;
    const TileMemoryPlan plan = planTileMemory( huge );
    REQUIRE( plan.action == TileMemoryPlan::Action::Refuse );
    REQUIRE( plan.requestedPeakBytes == UINT64_MAX );
}

// ---------------------------------------------------------------------------
// multi_pass_reduction pass-1 primitives (F-B-5)
// ---------------------------------------------------------------------------

TEST_CASE( "reduceTiles folds deterministically in tile order and honors cancel",
           "[chunk][reduction][lsee10]" )
{
    std::vector<int> tiles{ 1, 2, 3, 4 };
    const auto result = reduceTiles( tiles, 0, []( int acc, const int &tile ) {
        return acc + tile;
    } );
    REQUIRE( result.state == 10 );
    REQUIRE( result.tilesFolded == 4 );
    REQUIRE_FALSE( result.cancelled );

    // Cancel after the first tile: the PARTIAL state survives (the caller
    // can unwind without losing the accumulated prefix).
    int polls = 0;
    const auto partial = reduceTiles( tiles, 100, []( int acc, const int &tile ) {
        return acc + tile;
    }, [&polls] { return ++polls >= 2; } );
    REQUIRE( partial.cancelled );
    REQUIRE( partial.tilesFolded == 1 );
    REQUIRE( partial.state == 101 );
}

TEST_CASE( "reduceStream folds a producer sequence to the same state",
           "[chunk][reduction][lsee10]" )
{
    const std::vector<int> tiles{ 5, 6, 7 };
    std::size_t cursor = 0;
    const auto result = reduceStream(
        [&]() -> const int * {
            return cursor < tiles.size() ? &tiles[cursor++] : nullptr;
        },
        0, []( int acc, const int &tile ) { return acc * 10 + tile; } );
    REQUIRE( result.state == 567 );
    REQUIRE_FALSE( result.cancelled );
}

// ---------------------------------------------------------------------------
// ChunkGraph construction guards (F-A-10 / F-A-18)
// ---------------------------------------------------------------------------

TEST_CASE( "ChunkGraph refuses a second consumer on the same node (fan-out guard)",
           "[chunk][graph][lsee10]" )
{
    ChunkGraph graph;
    auto src = graph.addSource( countingSource( 4, 1.0f ) );
    (void)graph.addStage( src, []( TilePayload && p ) { return std::move( p ); } );
    // A second consumer of src would silently split tiles between consumers.
    REQUIRE_THROWS_AS( graph.addStage( src, []( TilePayload && p ) { return std::move( p ); } ),
                       std::logic_error );
}

TEST_CASE( "ChunkGraph rejects run() without a sink and double run()", "[chunk][graph][lsee10]" )
{
    ChunkGraph noSink;
    noSink.addSource( countingSource( 2, 1.0f ) );
    REQUIRE_THROWS_AS( noSink.run(), std::logic_error );

    ChunkGraph graph;
    auto src = graph.addSource( countingSource( 2, 1.0f ) );
    graph.addSink( src, []( TilePayload && ) { return true; } );
    graph.run();
    REQUIRE_THROWS_AS( graph.run(), std::logic_error );
}

TEST_CASE( "ChunkGraphCancelled is caught as the pipeline's ChunkCancelled (shared base)",
           "[chunk][graph][lsee10]" )
{
    ChunkGraph graph;
    auto src = graph.addSource( countingSource( 100, 1.0f ) );
    graph.addSink( src, []( TilePayload && ) { return false; } ); // abort at tile 1
    try
    {
        graph.run();
        FAIL( "expected a cancellation" );
    }
    catch ( const ChunkCancelled & )
    {
        // A pipeline-era catch clause keeps working (F-A-8).
    }
}
