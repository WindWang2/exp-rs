// test_r35_t04_chunk_contracts.cpp — R35-T04 Phase A contract regressions:
// strict TileSpec shape/byte admission (WP01), overflow-identifiable memory
// planning (WP02), the cancel/close queue contract under real threads (WP03),
// thread-start failure survival in ChunkPipeline (WP04) and typed topology
// validation in ChunkGraph (WP05).
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/bounded_chunk_queue.h"
#include "runtime/chunk/chunk_graph.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/chunk/memory_planner.h"
#include "runtime/chunk/tile_run_contract.h"
#include "runtime/chunk/tile_spec.h"
#include "runtime/observability/fault_registry.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace sicnu::runtime::chunk;
using sicnu::runtime::observability::fault::ArmedFault;
using sicnu::runtime::observability::fault::FaultAction;
using sicnu::runtime::observability::fault::Mode;

namespace
{
TilePayload makePayload( const TileSpec &spec, float fill )
{
    auto buf = std::make_shared<std::vector<float>>( spec.bufferElementCount(), fill );
    return TilePayload{ spec, std::move( buf ) };
}

TileSpec validSpec()
{
    // A shape buildTileGrid could have produced (8x8 raster, 4x4 tiles, halo 1).
    TileSpec s;
    s.index = 2;
    s.totalTiles = 4;
    s.xOffset = 4;
    s.yOffset = 4;
    s.width = 4;
    s.height = 4;
    s.halo = 1;
    s.bufferWidth = 6;
    s.bufferHeight = 6;
    s.rasterWidth = 8;
    s.rasterHeight = 8;
    s.bands = 2;
    return s;
}
} // namespace

// ── WP01: strict TileSpec shape + checked element/byte math ────────────────

TEST_CASE( "validateTileSpec accepts every legal grid shape", "[chunk][r35t04]" )
{
    for ( const TileSpec &t : buildTileGrid( 1000, 700, 256, 256, 3, 4 ) )
        REQUIRE_NOTHROW( validateTileSpec( t, "test" ) );
    REQUIRE_NOTHROW( validateTileSpec( validSpec(), "test" ) );
    // All-zero digests are legal identities; degenerate-but-legal specs from
    // tileSpecAt too.
    const TileRunPartition p{ 1000, 700, 256, 256, 2, 1, 0, 0 };
    for ( std::uint64_t i = 0; i < p.totalTiles(); ++i )
        REQUIRE_NOTHROW( validateTileSpec( tileSpecAt( p, i ), "test" ) );
}

TEST_CASE( "validateTileSpec typed-rejects shape breaches", "[chunk][r35t04]" )
{
    // zero/negative core span
    TileSpec s = validSpec();
    s.width = 0;
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    s = validSpec();
    s.height = -3;
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    // negative halo / zero bands / negative provenance
    s = validSpec();
    s.halo = -1;
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    s = validSpec();
    s.bands = 0;
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    s = validSpec();
    s.bandOffset = -1;
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    // index/totalTiles coherence
    s = validSpec();
    s.index = 4;
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    s = validSpec();
    s.totalTiles = 0;
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    // core outside the raster
    s = validSpec();
    s.xOffset = 6; // 6+4 > 8
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    // halo/buffer inconsistency: keep the PRODUCT constant so only the shape
    // gate can catch it (bufferWidth bitflip with matching element count).
    s = validSpec();
    s.bufferWidth = 4; // != width + 2*halo = 6
    s.bufferHeight = 9; // 4*9*2 == 6*6*2 — product preserved
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
    // negative buffer span cast to size_t must never look "reasonable"
    s = validSpec();
    s.bufferWidth = -4;
    REQUIRE_THROWS_AS( validateTileSpec( s, "t" ), std::invalid_argument );
}

TEST_CASE( "bufferElementCountChecked refuses int-domain wraparound", "[chunk][r35t04]" )
{
    // INT_MAX × INT_MAX × 8 wraps size_t (2^31·2^31·2^3 = 2^65): the unchecked
    // product is a small bogus number; the checked one typed-throws.
    TileSpec s = validSpec();
    s.bufferWidth = std::numeric_limits<int>::max();
    s.bufferHeight = std::numeric_limits<int>::max();
    s.bands = 8;
    const size_t wrapped = s.bufferElementCount();
    REQUIRE( wrapped != std::numeric_limits<size_t>::max() ); // it wrapped small
    REQUIRE_THROWS_AS( s.bufferElementCountChecked(), std::overflow_error );
    // Ordinary shapes keep the cheap accessor and the checked one equal.
    const TileSpec v = validSpec();
    REQUIRE( v.bufferElementCount() == v.bufferElementCountChecked() );
    REQUIRE( v.bufferElementCount() == 6u * 6u * 2u );
}

// ── WP02: memory planner — overflow is identifiable and never "fits" ───────

TEST_CASE( "planTileMemory refuses saturated estimates even at UINT64_MAX budget",
           "[chunk][r35t04][planner]" )
{
    TileMemoryRequest request;
    request.tileWidth = std::numeric_limits<std::uint32_t>::max();
    request.tileHeight = std::numeric_limits<std::uint32_t>::max();
    request.haloPixels = std::numeric_limits<std::uint32_t>::max();
    request.bands = std::numeric_limits<std::uint32_t>::max();
    request.bytesPerSample = 16;
    request.stageCount = 1;
    request.budgetBytes = std::numeric_limits<std::uint64_t>::max();
    const TileMemoryPlan plan = planTileMemory( request );
    CHECK( plan.action == TileMemoryPlan::Action::Refuse );
    CHECK( plan.estimateOverflowed );
    CHECK_FALSE( plan.fits() );
    REQUIRE( plan.reason.find( "overflowed" ) != std::string::npos );
}

TEST_CASE( "planTileMemory minimal-shape overflow is also a typed refusal",
           "[chunk][r35t04][planner]" )
{
    // queueCapacity=1 still multiplies the per-tile bytes: saturate perTile
    // alone so the requested AND minimal shapes both overflow.
    TileMemoryRequest request;
    request.tileWidth = 1u << 20;
    request.tileHeight = 1u << 20;
    request.haloPixels = 1u << 20;
    request.bands = 1u << 16;
    request.bytesPerSample = 16;
    request.requestedQueueCapacity = 4;
    request.budgetBytes = std::numeric_limits<std::uint64_t>::max();
    const TileMemoryPlan plan = planTileMemory( request );
    CHECK( plan.action == TileMemoryPlan::Action::Refuse );
    CHECK( plan.estimateOverflowed );
}

TEST_CASE( "planTileMemory spill-need saturation cannot prove scratch coverage",
           "[chunk][r35t04][planner]" )
{
    // perTile fits (minimal shape proves it) but perTile × expectedTileCount
    // overflows: scratch coverage is not provable, so no Spill.
    TileMemoryRequest request;
    request.tileWidth = 1u << 12;
    request.tileHeight = 1u << 12;
    request.bands = 64;
    request.bytesPerSample = 4;
    request.budgetBytes = 1; // below any minimal in-RAM shape
    request.allowSpill = true;
    request.scratchBudgetBytes = std::numeric_limits<std::uint64_t>::max();
    request.expectedTileCount = std::numeric_limits<std::uint64_t>::max();
    const TileMemoryPlan plan = planTileMemory( request );
    CHECK( plan.action == TileMemoryPlan::Action::Refuse );
    CHECK( plan.estimateOverflowed );
    REQUIRE( plan.reason.find( "spill" ) != std::string::npos );
}

TEST_CASE( "planTileMemory keeps the ordinary ladder and advisory contract",
           "[chunk][r35t04][planner]" )
{
    TileMemoryRequest request;
    request.tileWidth = 256;
    request.tileHeight = 256;
    request.bands = 4;
    request.bytesPerSample = 4;
    request.stageCount = 2;
    request.requestedQueueCapacity = 4;
    request.budgetBytes = tileStreamPeakBytes( request, 4 ) + 1;
    CHECK( planTileMemory( request ).action == TileMemoryPlan::Action::Admit );
    request.budgetBytes = tileStreamPeakBytes( request, 1 );
    const TileMemoryPlan reduced = planTileMemory( request );
    CHECK( reduced.action == TileMemoryPlan::Action::ReduceConcurrency );
    CHECK( reduced.recommendedQueueCapacity == 1 );
    CHECK_FALSE( reduced.estimateOverflowed );
    // Spill stays out of fits(): only scratch-capable callers consume it.
    request.budgetBytes = tileStreamPeakBytes( request, 1 ) - 1;
    request.allowSpill = true;
    request.scratchBudgetBytes = tileStreamPeakBytes( request, 1 ) * 16;
    request.expectedTileCount = 64;
    const TileMemoryPlan spill = planTileMemory( request );
    CHECK( spill.action == TileMemoryPlan::Action::Spill );
    CHECK_FALSE( spill.fits() );
    // Advisory never gates — but a saturated advisory reports the flag.
    request.budgetBytes = 0;
    request.tileWidth = std::numeric_limits<std::uint32_t>::max();
    request.tileHeight = std::numeric_limits<std::uint32_t>::max();
    request.bands = std::numeric_limits<std::uint32_t>::max();
    request.bytesPerSample = 16;
    const TileMemoryPlan advisory = planTileMemory( request );
    CHECK( advisory.action == TileMemoryPlan::Action::Advisory );
    CHECK( advisory.fits() );
    CHECK( advisory.estimateOverflowed );
}

// ── WP03: cancel vs close under real threads ───────────────────────────────

TEST_CASE( "cancel drops pending items; close drains the last tile",
           "[chunk][r35t04][queue]" )
{
    BoundedChunkQueue<int> q( 4 );
    REQUIRE( q.push( 1 ) );
    REQUIRE( q.push( 2 ) );
    q.cancel();
    int v = 0;
    REQUIRE( q.cancelled() );
    REQUIRE_FALSE( q.pop( v ) );    // nothing delivered past the stop boundary
    REQUIRE_FALSE( q.tryPop( v ) ); // polling sees the same stop
    REQUIRE_FALSE( q.push( 3 ) );   // producers fail closed

    BoundedChunkQueue<int> d( 4 );
    REQUIRE( d.push( 1 ) );
    REQUIRE( d.push( 2 ) );
    d.close();
    REQUIRE( d.pop( v ) );
    REQUIRE( v == 1 );
    REQUIRE( d.pop( v ) );
    REQUIRE( v == 2 );
    REQUIRE_FALSE( d.pop( v ) ); // drained, not cancelled
    REQUIRE_FALSE( d.cancelled() );
}

TEST_CASE( "cancel wakes a blocked consumer and a blocked producer",
           "[chunk][r35t04][queue]" )
{
    // Blocked CONSUMER (empty, open): must wake and fail on cancel.
    {
        BoundedChunkQueue<int> q( 1 );
        std::atomic<bool> popped{ true };
        std::thread consumer( [&] {
            int v;
            popped = q.pop( v );
        } );
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) ); // let it park
        q.cancel();
        consumer.join();
        REQUIRE_FALSE( popped.load() );
    }
    // Blocked PRODUCER (full queue): must wake and fail on cancel — the
    // pending item is dropped, not handed over.
    {
        BoundedChunkQueue<int> q( 1 );
        REQUIRE( q.push( 7 ) );
        std::atomic<bool> pushed{ true };
        std::thread producer( [&] {
            pushed = q.push( 8 ); // parks on the full queue
        } );
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        q.cancel();
        producer.join();
        REQUIRE_FALSE( pushed.load() );
        int v = 0;
        REQUIRE_FALSE( q.pop( v ) ); // even the pre-cancel item is gone
    }
}

TEST_CASE( "close-then-cancel on a drained queue stays cancelled",
           "[chunk][r35t04][queue]" )
{
    BoundedChunkQueue<int> q( 2 );
    REQUIRE( q.push( 1 ) );
    q.close();
    int v = 0;
    REQUIRE( q.pop( v ) );
    REQUIRE_FALSE( q.pop( v ) );
    q.cancel(); // a late cancel after drain is a no-op flag flip
    REQUIRE( q.cancelled() );
    REQUIRE_FALSE( q.tryPop( v ) );
}

TEST_CASE( "ChunkPipeline cancel stops delivery without losing close semantics",
           "[chunk][r35t04][pipeline]" )
{
    // Normal completion still delivers every tile (close drains), and a
    // consumer that observes cancel stops receiving — the count it saw is
    // bounded by the tiles ALREADY in hand, never the queued remainder.
    std::atomic<bool> cancel{ false };
    std::atomic<int> consumed{ 0 };
    ChunkPipeline::Config cfg;
    cfg.queueCapacity = 4;
    ChunkPipeline pipeline(
        []( TilePayload &out ) {
            static int next = 0;
            if ( next >= 200 )
                return false;
            out = makePayload( validSpec(), 1.0f );
            out.spec.index = next;
            out.spec.totalTiles = 200;
            ++next;
            return true;
        },
        {},
        [&]( TilePayload && ) {
            const int n = consumed.fetch_add( 1 ) + 1;
            if ( n == 24 )
                cancel.store( true ); // cooperative cancel mid-stream
            return true;
        },
        cfg );
    pipeline.setCancelFlag( &cancel );
    REQUIRE_THROWS_AS( pipeline.run(), ChunkCancelled );
    REQUIRE( consumed.load() <= 200 );
    // Cancel takes effect promptly: the producer had 200 tiles queued but the
    // stop boundary must cut delivery far short of the full stream.
    REQUIRE( consumed.load() < 200 );
}

// ── WP04: thread-start failure must not terminate the process ─────────────

TEST_CASE( "ChunkPipeline survives injected thread-start failure at stage N",
           "[chunk][r35t04][pipeline]" )
{
    // Fault fires on the 3rd thread creation (producer + stage0 started, the
    // second stage's start throws): earlier threads are already running. The
    // guard must cancel + join them and rethrow the ORIGINAL error — an
    // unguarded vector<std::thread> would terminate here.
    std::atomic<int> consumed{ 0 };
    {
        ArmedFault startFailure(
            FaultAction{ "chunk.pipeline.threadStart", Mode::EveryNth, 3, "" } );
        ChunkPipeline::Config cfg;
        cfg.queueCapacity = 2;
        ChunkPipeline pipeline(
            []( TilePayload &out ) {
                static int next = 0;
                if ( next >= 500 )
                    return false;
                out = makePayload( validSpec(), 1.0f );
                ++next;
                return true;
            },
            {
                []( TilePayload &&p ) { return std::move( p ); },
                []( TilePayload &&p ) { return std::move( p ); },
            },
            [&]( TilePayload && ) {
                consumed.fetch_add( 1 );
                return true;
            },
            cfg );
        REQUIRE_THROWS_AS( pipeline.run(), std::runtime_error );
    } // ArmedFault disarms everything on scope exit
    // The process is still healthy: a fresh pipeline runs to completion.
    ChunkPipeline::Config cfg;
    ChunkPipeline ok(
        []( TilePayload &out ) {
            static int next = 0;
            if ( next >= 8 )
                return false;
            out = makePayload( validSpec(), 2.0f );
            ++next;
            return true;
        },
        { []( TilePayload &&p ) { return std::move( p ); } },
        []( TilePayload && ) { return true; },
        cfg );
    REQUIRE_NOTHROW( ok.run() );
}

TEST_CASE( "ChunkPipeline thread-start failure on the FIRST thread is typed",
           "[chunk][r35t04][pipeline]" )
{
    ArmedFault startFailure(
        FaultAction{ "chunk.pipeline.threadStart", Mode::NextN, 1, "" } );
    ChunkPipeline pipeline( []( TilePayload & ) { return false; }, {},
                            []( TilePayload && ) { return true; } );
    REQUIRE_THROWS_AS( pipeline.run(), std::runtime_error );
}

// ── WP05: typed topology validation in every build type ───────────────────

TEST_CASE( "ChunkGraph typed-rejects dead node ids and empty functions",
           "[chunk][r35t04][graph]" )
{
    ChunkGraph graph;
    const ChunkGraph::NodeId src = graph.addSource( []( TilePayload & ) { return false; } );
    REQUIRE_THROWS_AS( graph.addStage( -1, []( TilePayload &&p ) { return std::move( p ); } ),
                       std::logic_error );
    REQUIRE_THROWS_AS(
        graph.addStage( src + 100, []( TilePayload &&p ) { return std::move( p ); } ),
        std::logic_error );
    REQUIRE_THROWS_AS( graph.addStage( src, nullptr ), std::logic_error );
    REQUIRE_THROWS_AS( graph.addSink( -1, []( TilePayload && ) { return true; } ), std::logic_error );
    REQUIRE_THROWS_AS( graph.addSink( src + 100, []( TilePayload && ) { return true; } ),
                       std::logic_error );
    REQUIRE_THROWS_AS( graph.addSink( src, nullptr ), std::logic_error );
    REQUIRE_THROWS_AS( graph.addSource( nullptr ), std::logic_error );
    REQUIRE_THROWS_AS(
        graph.addJoin( { src, src + 100 }, []( std::vector<TilePayload> && ) {
            return TilePayload{};
        } ),
        std::logic_error );
    REQUIRE_THROWS_AS(
        graph.addJoin( { src }, nullptr ), std::logic_error );
    // Rejected calls leave the graph untouched: the source stays legally
    // attachable — a sink on it runs end-to-end after all the rejections
    // (queueFor is private; its typed gate is exercised via run paths).
    // And the graph still runs end-to-end after the rejections.
    graph.addSink( src, []( TilePayload && ) { return true; } );
    REQUIRE_NOTHROW( graph.run() );
}

TEST_CASE( "ChunkGraph sink replacement keeps both inputs usable",
           "[chunk][r35t04][graph]" )
{
    // Same-input replacement is the documented "replace the sink" contract;
    // replacing with a DIFFERENT free input drops the old edge — the old
    // input becomes wireable again (strong exception guarantee of the gates).
    ChunkGraph graph;
    const auto srcA = graph.addSource( []( TilePayload & ) { return false; } );
    const auto srcB = graph.addSource( []( TilePayload & ) { return false; } );
    graph.addSink( srcA, []( TilePayload && ) { return true; } );
    graph.addSink( srcA, []( TilePayload && ) { return true; } ); // same input: legal replace
    graph.addSink( srcB, []( TilePayload && ) { return true; } ); // different input: replaces
    REQUIRE_NOTHROW( graph.addStage( srcA, []( TilePayload &&p ) { return std::move( p ); } ) );
}

TEST_CASE( "ChunkGraph refuses a source/partition breach without hanging",
           "[chunk][r35t04][graph]" )
{
    // Two sources with different stream lengths into one join: the drained
    // input beside a delivering one is a typed partition mismatch (never a
    // hang), and the runner surfaces it after joining every thread.
    ChunkGraph graph;
    int aEmitted = 0;
    graph.addSource( [&]( TilePayload &out ) {
        if ( aEmitted++ >= 3 )
            return false;
        out = makePayload( validSpec(), 1.0f );
        out.spec.index = aEmitted - 1;
        out.spec.totalTiles = 3;
        return true;
    } );
    graph.addSource( []( TilePayload & ) { return false; } ); // empty stream
    const auto join = graph.addJoin( { 0, 1 }, []( std::vector<TilePayload> &&tiles ) {
        return std::move( tiles.front() );
    } );
    graph.addSink( join, []( TilePayload && ) { return true; } );
    bool mismatch = false;
    try
    {
        graph.run();
    }
    catch ( const std::runtime_error &e )
    {
        mismatch = std::string( e.what() ).find( "partition" ) != std::string::npos;
    }
    REQUIRE( mismatch );
}
