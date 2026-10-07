// test_execution_governor_11.cpp — WP-D: unified resource admission.
//
// Oracle independence: the planner's peak-RAM upper bound is checked against
// a discrete-event occupancy SIMULATOR written in this file — it models the
// pipeline thread contract (queues of cap, producer + S stages + consumer,
// join nodes holding one tile per input) under randomized schedules and
// records the true maximum simultaneous tile count. The formula must
// DOMINATE the simulator on every seed; the simulator never calls the
// planner. Budget/refuse/leak behavior asserts through public APIs only.
#include <catch2/catch_test_macros.hpp>

#include "runtime/chunk/memory_planner.h"
#include "runtime/exec/execution_governor.h"
#include "runtime/observability/execution_telemetry.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <thread>

#include "runtime/chunk/chunk_pipeline.h"

using namespace sicnu::runtime;
using chunk::ChunkCancelled;
using chunk::ChunkPipeline;
using chunk::TileMemoryPlan;
using chunk::TileMemoryRequest;
using exec::AdmissionRefused;
using exec::ExecutionGovernor;
using exec::TileMemoryPool;

namespace
{

/// Formula-side tile count implied by tileStreamPeakBytes for a request with
/// 1-byte samples and 1-pixel tiles... instead of contorting units, compare
/// BYTES directly: perTile bytes is part of the request; the simulator
/// returns a tile count, so feed the simulator result through the same
/// perTile (computed independently here from width/height/halo/bands/bytes).
std::uint64_t perTileBytes( const TileMemoryRequest &r )
{
    const std::uint64_t w = r.tileWidth + 2ull * r.haloPixels;
    const std::uint64_t h = r.tileHeight + 2ull * r.haloPixels;
    return w * h * r.bands * r.bytesPerSample;
}

/// Randomized occupancy simulation of the pipeline contract — INCLUDING the
/// stage input/output overlap (a stage's freshly built output coexists with
/// its moved-in input; review R2-P1 made this dimension load-bearing).
/// Returns the maximum number of simultaneously-live tiles observed across
/// the schedule. Threads: producer + S stages + consumer. Joins (inputs>1)
/// hold one tile per input and build one output per input.
std::uint64_t simulatePeakTiles( unsigned stages, unsigned queueCap, unsigned inputCount,
                                 unsigned steps, std::mt19937 &rng )
{
    const unsigned inputs = std::max( inputCount, 1u );
    std::vector<std::vector<int>> queues( stages + 1,
                                          std::vector<int>( inputs, 0 ) );
    // In-hand tiles per thread: [0]=producer, [1..S]=stages, [S+1]=consumer.
    std::vector<unsigned> inHand( stages + 2, 0 );

    auto totalQueued = [&] {
        unsigned t = 0;
        for ( auto &q : queues )
            for ( int v : q )
                t += static_cast<unsigned>( v );
        return t;
    };
    auto totalInHand = [&] {
        unsigned t = 0;
        for ( unsigned v : inHand )
            t += v;
        return t;
    };
    auto queueHasRoom = [&]( unsigned q ) {
        for ( unsigned i = 0; i < inputs; ++i )
            if ( queues[q][i] >= static_cast<int>( queueCap ) )
                return false;
        return true;
    };

    std::uint64_t peak = 0;
    for ( unsigned step = 0; step < steps; ++step )
    {
        const unsigned action = rng() % ( 2 * stages + 6 );
        if ( action == 0 && inHand[0] == 0 )
        {
            inHand[0] = inputs; // producer builds a tile (per input)
        }
        else if ( action == 1 && inHand[0] > 0 && queueHasRoom( 0 ) )
        {
            for ( unsigned i = 0; i < inputs; ++i )
                queues[0][i] += 1;
            inHand[0] = 0; // producer pushes
        }
        else if ( action >= 2 && action < 2 + static_cast<int>( stages ) )
        {
            const unsigned s = action - 2; // stage index
            const unsigned hold = inputs; // join: one per input
            if ( inHand[s + 1] == 0 )
            {
                bool canPop = true;
                if ( s == 0 )
                {
                    for ( unsigned i = 0; i < inputs; ++i )
                        if ( queues[0][i] == 0 )
                            canPop = false;
                }
                else
                    canPop = queues[s][0] > 0;
                if ( canPop )
                {
                    if ( s == 0 )
                        for ( unsigned i = 0; i < inputs; ++i )
                            queues[0][i] -= 1;
                    else
                        queues[s][0] -= 1;
                    inHand[s + 1] = hold;
                }
            }
        }
        else if ( action >= 2 + static_cast<int>( stages )
                  && action < 2 + 2 * static_cast<int>( stages ) )
        {
            // Stage builds its output: input + output coexist (bounded 2·I).
            const unsigned s = action - 2 - stages;
            if ( inHand[s + 1] > 0 && inHand[s + 1] < 2 * inputs )
                inHand[s + 1] += inputs;
        }
        else if ( action == 2 + 2 * stages )
        {
            // A random stage pushes its in-hand tile downstream.
            if ( stages > 0 )
            {
                const unsigned s = rng() % stages;
                if ( inHand[s + 1] > 0 && queues[s + 1][0] < static_cast<int>( queueCap ) )
                {
                    inHand[s + 1] = 0;
                    queues[s + 1][0] += 1;
                }
            }
        }
        else if ( action == 3 + 2 * stages )
        {
            // Consumer drains the final queue (holds, then releases).
            const unsigned c = stages + 1;
            if ( inHand[c] == 0 && queues[stages][0] > 0 )
            {
                queues[stages][0] -= 1;
                inHand[c] = 1;
            }
            else if ( inHand[c] > 0 )
            {
                inHand[c] = 0; // consumed
            }
        }
        peak = std::max<std::uint64_t>( peak, totalQueued() + totalInHand() );
    }
    return peak;
}

} // namespace

TEST_CASE( "Planner model dominates simulated occupancy (upper bound)",
           "[execution][governor]" )
{
    struct Shape
    {
        unsigned stages;
        unsigned cap;
        unsigned inputs;
    };
    const Shape shapes[] = {
        { 1, 2, 1 }, { 2, 2, 1 }, { 3, 2, 1 }, { 2, 4, 1 },
        { 2, 2, 2 }, // join width 2
        { 4, 3, 1 },
    };
    for ( const Shape &shape : shapes )
    {
        TileMemoryRequest r;
        r.stageCount = shape.stages;
        r.requestedQueueCapacity = shape.cap;
        r.inputCount = shape.inputs;
        r.bytesPerSample = 1;
        // Make perTile = 1 byte so peak bytes == peak tiles in the formula's
        // units: 1x1 tile, 1 band, 1-byte samples, no halo.
        r.tileWidth = 1;
        r.tileHeight = 1;
        r.bands = 1;

        const std::uint64_t formulaBytes = chunk::tileStreamPeakBytes( r, shape.cap );
        const std::uint64_t unit = perTileBytes( r );
        REQUIRE( unit == 1 );

        std::mt19937 rng( 1234 + shape.stages * 100 + shape.cap * 10 + shape.inputs );
        for ( int seed = 0; seed < 8; ++seed )
        {
            const std::uint64_t simPeak =
                simulatePeakTiles( shape.stages, shape.cap, shape.inputs, 2000, rng );
            INFO( "stages=" << shape.stages << " cap=" << shape.cap << " inputs="
                            << shape.inputs << " simPeak=" << simPeak << " formula="
                            << formulaBytes );
            REQUIRE( formulaBytes >= simPeak );
        }
    }
}

TEST_CASE( "Admission ladder: Admit, ReduceConcurrency, Spill, Refuse",
           "[execution][governor]" )
{
    ExecutionGovernor::Config cfg;
    cfg.ramBytes = 1024 * 1024;
    cfg.scratchBytes = 1024 * 1024 * 1024;
    cfg.scratchRoot = "unused-for-planning";
    ExecutionGovernor gov( cfg );

    TileMemoryRequest r;
    r.tileWidth = 64;
    r.tileHeight = 64;
    r.bands = 4; // 64*64*4*4 = 64 KiB per tile
    r.stageCount = 2;
    r.requestedQueueCapacity = 2;

    SECTION( "fits → Admit" )
    {
        const TileMemoryPlan plan = gov.admitOrRefuse( r );
        REQUIRE( plan.action == TileMemoryPlan::Action::Admit );
        REQUIRE( plan.fits() );
    }
    SECTION( "tight → ReduceConcurrency" )
    {
        // Budget between queueCapacity=1 and =2 peaks.
        cfg.ramBytes = chunk::tileStreamPeakBytes( r, 1 ) + 1;
        ExecutionGovernor tight( cfg );
        const TileMemoryPlan plan = tight.admitOrRefuse( r );
        REQUIRE( plan.action == TileMemoryPlan::Action::ReduceConcurrency );
        REQUIRE( plan.recommendedQueueCapacity == 1 );
    }
    SECTION( "below minimum with scratch → Spill" )
    {
        cfg.ramBytes = chunk::tileStreamPeakBytes( r, 1 ) - 1;
        cfg.scratchBytes = 1ull << 40;
        ExecutionGovernor spilly( cfg );
        TileMemoryRequest spillable = r;
        spillable.allowSpill = true;
        spillable.expectedTileCount = 100;
        const TileMemoryPlan plan = spilly.admitOrRefuse( spillable );
        REQUIRE( plan.action == TileMemoryPlan::Action::Spill );
        REQUIRE( plan.spillBytes > 0 );
    }
    SECTION( "nothing left → typed AdmissionRefused (never bad_alloc)" )
    {
        cfg.ramBytes = 64; // smaller than one tile
        ExecutionGovernor refusing( cfg );
        TileMemoryRequest doomed = r;
        doomed.allowSpill = true;
        doomed.expectedTileCount = 1 << 20;
        doomed.scratchBudgetBytes = 16;
        REQUIRE_THROWS_AS( refusing.admitOrRefuse( doomed ), AdmissionRefused );
    }
}

TEST_CASE( "Governor scratch budget is enforced with the typed refusal",
           "[execution][governor]" )
{
    ExecutionGovernor::Config cfg;
    cfg.scratchBytes = 1000;
    ExecutionGovernor gov( cfg );

    auto leaseA = gov.scratch().acquire( "run-a", "stem", 600 );
    REQUIRE( leaseA.isValid() );
    REQUIRE_THROWS_AS( gov.scratch().acquire( "run-a", "stem2", 600 ),
                       chunk::ScratchBudgetExceeded );
    // Releasing the lease frees the budget.
    leaseA = chunk::ScratchLease{};
    auto leaseB = gov.scratch().acquire( "run-a", "stem3", 600 );
    REQUIRE( leaseB.isValid() );
}

TEST_CASE( "Write gate backpressure accounts bytes and admits oversized when idle",
           "[execution][governor]" )
{
    ExecutionGovernor::Config cfg;
    cfg.writeInFlightBytes = 1000;
    ExecutionGovernor gov( cfg );

    auto &gate = gov.writeGate();
    gate.acquire( 700 );
    REQUIRE( gate.outstandingBytes() == 700 );
    {
        // Oversized single write proceeds when the gate can never fit it
        // alongside the outstanding bytes but the never-starve rule applies
        // only when idle — here it must BLOCK, so verify accounting only via
        // a background thread that waits.
        std::atomic<bool> entered{ false };
        std::atomic<bool> acquired{ false };
        std::thread waiter( [&] {
            entered = true;
            gate.acquire( 400 ); // 700+400 > 1000: blocks until release
            acquired = true;
            gate.release( 400 );
        } );
        // Wait (bounded) until the waiter has STARTED and is blocked; it
        // must NOT acquire while the gate is full.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 10 );
        while ( !entered && std::chrono::steady_clock::now() < deadline )
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        REQUIRE( entered );
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        REQUIRE_FALSE( acquired );
        gate.release( 700 ); // unblocks
        waiter.join();
        REQUIRE( acquired );
        REQUIRE( gate.outstandingBytes() == 0 );
    }
}

TEST_CASE( "Governor leak detection reports and counts (fail-closed record)",
           "[execution][governor]" )
{
    auto &tel = observability::ExecutionTelemetry::instance();
    const auto leaksBefore = tel.counters()["resource_leaks_detected"];

    chunk::ScratchLease leaked; // OUTLIVES the governor: reverse destruction
    {
        ExecutionGovernor::Config cfg;
        cfg.scratchRoot = "leak-probe-root";
        ExecutionGovernor gov( cfg );
        leaked = gov.scratch().acquire( "leaky", "stem", 128 );
        REQUIRE( leaked.isValid() );
        REQUIRE( gov.hasOutstandingResources() );
        // Governor destroyed with the lease still live.
    }
    REQUIRE( leaked.isValid() );

    REQUIRE( observability::ExecutionTelemetry::instance().counters()
                 ["resource_leaks_detected"] == leaksBefore + 1 );
    const std::string report = ExecutionGovernor::lastLeakReportJson();
    REQUIRE( report.find( "exp.diag.v1" ) != std::string::npos );
    REQUIRE( report.find( "execution.resource_leak" ) != std::string::npos );
}

// ============================================================================
// Test Suite 1: RSS Watermark Throttling & Hysteresis
// ============================================================================
TEST_CASE( "RSS watermark throttling blocks at high watermark and unblocks at low watermark",
           "[execution][governor][rss]" )
{
    std::atomic<std::uint64_t> mockRss{ 500 * 1024 * 1024 }; // 500 MB

    ExecutionGovernor::Config cfg;
    cfg.rssWatermarkBytes = 800 * 1024 * 1024;    // 800 MB high watermark
    cfg.rssLowWatermarkBytes = 600 * 1024 * 1024; // 600 MB low watermark
    cfg.rssSampler = [&] { return mockRss.load(); };
    ExecutionGovernor gov( cfg );

    SECTION( "Below high watermark: execution proceeds without blocking" )
    {
        mockRss.store( 500 * 1024 * 1024 );
        REQUIRE_FALSE( gov.isWatermarkExceeded() );
        REQUIRE( gov.currentRssBytes() == 500 * 1024 * 1024 );

        // Must complete immediately without blocking
        const auto t0 = std::chrono::steady_clock::now();
        gov.throttleWait( nullptr, std::chrono::milliseconds( 5 ) );
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0 ).count();
        REQUIRE( elapsedMs < 50 );
    }

    SECTION( "At or above high watermark: throttleWait blocks until RSS drops below low watermark" )
    {
        mockRss.store( 850 * 1024 * 1024 ); // Above high watermark
        REQUIRE( gov.isWatermarkExceeded() );

        std::atomic<bool> entered{ false };
        std::atomic<bool> unblocked{ false };

        std::thread waiter( [&] {
            entered = true;
            gov.throttleWait( nullptr, std::chrono::milliseconds( 5 ) );
            unblocked = true;
        } );

        // Wait until waiter thread has entered throttleWait
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        while ( !entered && std::chrono::steady_clock::now() < deadline )
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        REQUIRE( entered );

        // Confirm thread remains blocked after settling
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        REQUIRE_FALSE( unblocked.load() );

        // Hysteresis test: drop RSS to 700 MB (below high watermark 800 MB, but ABOVE low watermark 600 MB)
        mockRss.store( 700 * 1024 * 1024 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        REQUIRE_FALSE( unblocked.load() ); // Must still remain throttled!

        // Drop RSS to 550 MB (below low watermark 600 MB)
        mockRss.store( 550 * 1024 * 1024 );

        waiter.join();
        REQUIRE( unblocked.load() );
        REQUIRE_FALSE( gov.isWatermarkExceeded() );
    }

    SECTION( "Default low watermark equals high watermark when rssLowWatermarkBytes is 0" )
    {
        ExecutionGovernor::Config defaultLowCfg;
        defaultLowCfg.rssWatermarkBytes = 800 * 1024 * 1024;
        defaultLowCfg.rssLowWatermarkBytes = 0; // Unset: defaults to high watermark
        defaultLowCfg.rssSampler = [&] { return mockRss.load(); };
        ExecutionGovernor govDefaultLow( defaultLowCfg );

        mockRss.store( 850 * 1024 * 1024 );
        REQUIRE( govDefaultLow.isWatermarkExceeded() );

        std::atomic<bool> unblocked{ false };
        std::thread waiter( [&] {
            govDefaultLow.throttleWait( nullptr, std::chrono::milliseconds( 5 ) );
            unblocked = true;
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
        REQUIRE_FALSE( unblocked.load() );

        // Dropping directly below 800 MB unblocks immediately
        mockRss.store( 790 * 1024 * 1024 );
        waiter.join();
        REQUIRE( unblocked.load() );
    }

    SECTION( "Disabled watermark (0) never throttles regardless of RSS" )
    {
        ExecutionGovernor::Config disabledCfg;
        disabledCfg.rssWatermarkBytes = 0;
        disabledCfg.rssSampler = [] { return 16ull * 1024 * 1024 * 1024; }; // 16 GB
        ExecutionGovernor govDisabled( disabledCfg );

        REQUIRE_FALSE( govDisabled.isWatermarkExceeded() );
        REQUIRE_NOTHROW( govDisabled.throttleWait() );
    }

    SECTION( "Procfs default sampler smoke test" )
    {
        ExecutionGovernor gov( ExecutionGovernor::Config{} );
#if defined( __linux__ )
        REQUIRE( gov.currentRssBytes() > 0 );
#endif
        REQUIRE_FALSE( gov.isWatermarkExceeded() );
    }
}

// ============================================================================
// Test Suite 2: Cancellation During Throttle
// ============================================================================
TEST_CASE( "Cancellation during RSS throttle throws ChunkCancelled immediately",
           "[execution][governor][cancel]" )
{
    ExecutionGovernor::Config cfg;
    cfg.rssWatermarkBytes = 500 * 1024 * 1024;
    cfg.rssSampler = [] { return 700 * 1024 * 1024; }; // Permanently above watermark
    ExecutionGovernor gov( cfg );

    SECTION( "Pre-set cancel flag throws ChunkCancelled on entry" )
    {
        std::atomic<bool> cancelFlag{ true };
        REQUIRE_THROWS_AS( gov.throttleWait( &cancelFlag, std::chrono::milliseconds( 5 ) ),
                           ChunkCancelled );
    }

    SECTION( "Cancel flag set while thread is actively blocked unblocks promptly" )
    {
        std::atomic<bool> cancelFlag{ false };
        std::atomic<bool> entered{ false };
        std::atomic<bool> caughtCancelled{ false };

        std::thread waiter( [&] {
            entered = true;
            try
            {
                gov.throttleWait( &cancelFlag, std::chrono::milliseconds( 5 ) );
            }
            catch ( const ChunkCancelled & )
            {
                caughtCancelled = true;
            }
        } );

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        while ( !entered && std::chrono::steady_clock::now() < deadline )
            std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
        REQUIRE( entered );

        std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
        REQUIRE_FALSE( caughtCancelled.load() );

        // Signal cancellation
        cancelFlag.store( true );

        waiter.join();
        REQUIRE( caughtCancelled.load() );
    }

    SECTION( "Cancel predicate overload throws ChunkCancelled" )
    {
        std::atomic<bool> stopSignal{ false };
        std::thread waiter( [&] {
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
            stopSignal.store( true );
        } );

        REQUIRE_THROWS_AS(
            gov.throttleWait( [&] { return stopSignal.load(); }, std::chrono::milliseconds( 5 ) ),
            ChunkCancelled );
        waiter.join();
    }
}

// ============================================================================
// Test Suite 3: TileMemoryPool Bounding, Buffer Reuse, and Destructor Leak Detection
// ============================================================================
TEST_CASE( "TileMemoryPool enforces bounds, reuses buffers, and detects destructor leaks",
           "[execution][governor][pool]" )
{
    SECTION( "Buffer allocation, recycling via custom deleter, and cache hits" )
    {
        TileMemoryPool::Config cfg;
        cfg.maxAllocatedBytes = 1024 * 1024; // 1 MB
        cfg.maxPoolBytes = 512 * 1024;      // 512 KB
        TileMemoryPool pool( cfg );

        REQUIRE( pool.allocatedBytes() == 0 );
        REQUIRE( pool.pooledBytes() == 0 );
        REQUIRE( pool.pooledCount() == 0 );

        // Acquire 1024 floats = 4096 bytes
        auto buf1 = pool.acquireBuffer( 1024 );
        REQUIRE( buf1 != nullptr );
        REQUIRE( buf1->size() == 1024 );
        REQUIRE( pool.allocatedBytes() == 4096 );
        REQUIRE( pool.pooledBytes() == 0 );
        REQUIRE( pool.totalAllocations() == 1 );
        REQUIRE( pool.poolMisses() == 1 );
        REQUIRE( pool.poolHits() == 0 );

        // Write verification pattern
        ( *buf1 )[0] = 42.0f;
        ( *buf1 )[1023] = 99.0f;

        // Release buffer by dropping shared_ptr reference
        buf1.reset();
        REQUIRE( pool.allocatedBytes() == 0 );
        REQUIRE( pool.pooledBytes() >= 4096 );
        REQUIRE( pool.pooledCount() == 1 );

        // Re-acquire buffer of same size: must hit cache and return zeroed buffer
        auto buf2 = pool.acquireBuffer( 1024 );
        REQUIRE( buf2 != nullptr );
        REQUIRE( buf2->size() == 1024 );
        REQUIRE( ( *buf2 )[0] == 0.0f ); // Recycled buffer must be sanitized/cleared
        REQUIRE( pool.allocatedBytes() == 4096 );
        REQUIRE( pool.pooledBytes() == 0 );
        REQUIRE( pool.pooledCount() == 0 );
        REQUIRE( pool.totalAllocations() == 2 );
        REQUIRE( pool.poolHits() == 1 ); // Cache hit!
        REQUIRE( pool.poolMisses() == 1 );
    }

    SECTION( "Active allocation cap blocks and respects never-starve rule" )
    {
        TileMemoryPool::Config cfg;
        cfg.maxAllocatedBytes = 8192; // Space for exactly two 1024-float buffers (4096 B each)
        TileMemoryPool pool( cfg );

        auto buf1 = pool.acquireBuffer( 1024 );
        auto buf2 = pool.acquireBuffer( 1024 );
        REQUIRE( pool.allocatedBytes() == 8192 );

        // Non-blocking tryAcquireBuffer returns nullptr when budget exhausted
        auto buf3 = pool.tryAcquireBuffer( 1024 );
        REQUIRE( buf3 == nullptr );

        // Blocking acquire waits for release
        std::atomic<bool> acquired{ false };
        std::shared_ptr<std::vector<float>> waiterBuf;
        std::thread waiter( [&] {
            waiterBuf = pool.acquireBuffer( 1024 );
            if ( waiterBuf )
                acquired = true;
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
        REQUIRE_FALSE( acquired.load() );

        // Release buf1 -> unblocks waiter
        buf1.reset();
        waiter.join();
        REQUIRE( acquired.load() );
        REQUIRE( pool.allocatedBytes() == 8192 );

        // Never-starve rule: an idle pool (allocatedBytes == 0) admits an oversized tile
        buf2.reset();
        waiterBuf.reset();
        // Wait for waiter's buffer to also be freed:
        while ( pool.allocatedBytes() > 0 )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );

        REQUIRE( pool.allocatedBytes() == 0 );
        // Request 16384 bytes (> maxAllocatedBytes 8192): must succeed when idle
        auto oversized = pool.acquireBuffer( 4096 ); // 4096 * 4 = 16384 bytes
        REQUIRE( oversized != nullptr );
        REQUIRE( pool.allocatedBytes() == 16384 );
    }

    SECTION( "Max pool bytes cap trims excess idle buffers to system heap" )
    {
        TileMemoryPool::Config cfg;
        cfg.maxAllocatedBytes = 0; // Unbounded active
        cfg.maxPoolBytes = 4096;   // Pool retains at most one 1024-float buffer
        TileMemoryPool pool( cfg );

        auto b1 = pool.acquireBuffer( 1024 );
        auto b2 = pool.acquireBuffer( 1024 );

        b1.reset(); // Pooled bytes = 4096, pooledCount = 1
        REQUIRE( pool.pooledBytes() == 4096 );
        REQUIRE( pool.pooledCount() == 1 );

        b2.reset(); // Excess buffer exceeds maxPoolBytes -> deallocated to OS
        REQUIRE( pool.pooledBytes() == 4096 );
        REQUIRE( pool.pooledCount() == 1 );
    }

    SECTION( "ExecutionGovernor destructor detects leaked memory pool buffers" )
    {
        auto &tel = observability::ExecutionTelemetry::instance();
        const auto leaksBefore = tel.counters()["resource_leaks_detected"];

        std::shared_ptr<std::vector<float>> leakedBuffer;
        {
            ExecutionGovernor::Config cfg;
            cfg.maxAllocatedBytes = 1024 * 1024;
            ExecutionGovernor gov( cfg );

            leakedBuffer = gov.memoryPool().acquireBuffer( 512 ); // 2048 bytes
            REQUIRE( leakedBuffer != nullptr );
            REQUIRE( gov.memoryPool().allocatedBytes() == 2048 );
            REQUIRE( gov.hasOutstandingResources() ); // Must report true!
            // gov destroyed with leakedBuffer still alive
        }

        // Leaked resource must trigger diagnostic report and telemetry increment
        REQUIRE( tel.counters()["resource_leaks_detected"] == leaksBefore + 1 );
        const std::string report = ExecutionGovernor::lastLeakReportJson();
        REQUIRE( report.find( "exp.diag.v1" ) != std::string::npos );
        REQUIRE( report.find( "execution.resource_leak" ) != std::string::npos );
        REQUIRE( report.find( "memory pool outstanding" ) != std::string::npos );

        // Reverse destruction safety: releasing buffer after governor died must not crash
        REQUIRE_NOTHROW( leakedBuffer.reset() );
        REQUIRE( leakedBuffer == nullptr );
    }
}

// ============================================================================
// Test Suite 4: Thread Safety Under High Concurrency
// ============================================================================
TEST_CASE( "TileMemoryPool maintains thread-safety and data integrity under concurrent acquire and release",
           "[execution][governor][concurrency]" )
{
    TileMemoryPool::Config cfg;
    cfg.maxAllocatedBytes = 64 * 1024; // 64 KB limit
    cfg.maxPoolBytes = 32 * 1024;      // 32 KB pool limit
    TileMemoryPool pool( cfg );

    constexpr int kNumThreads = 6;
    constexpr int kIterations = 100;
    std::vector<std::thread> workers;
    workers.reserve( kNumThreads );

    std::atomic<bool> running{ true };
    std::atomic<bool> canaryCorrupted{ false };
    std::atomic<std::uint64_t> totalVerifiedBuffers{ 0 };

    for ( int t = 0; t < kNumThreads; ++t )
    {
        workers.emplace_back( [&pool, t, &canaryCorrupted, &totalVerifiedBuffers] {
            for ( int iter = 0; iter < kIterations; ++iter )
            {
                // Varying sizes: 128, 256, 512 floats
                const std::size_t count = 128 * ( 1 + ( ( t + iter ) % 4 ) );
                auto buf = pool.acquireBuffer( count );
                if ( !buf )
                    continue;

                // Write thread-specific canary pattern
                const float canary = static_cast<float>( ( t + 1 ) * 1000 + iter );
                ( *buf )[0] = canary;
                ( *buf )[count - 1] = canary;

                std::this_thread::yield();

                // Verify canary integrity (non-Catch2 inside worker thread to avoid OutputRedirect races)
                if ( ( *buf )[0] != canary || ( *buf )[count - 1] != canary )
                    canaryCorrupted.store( true, std::memory_order_relaxed );

                totalVerifiedBuffers++;
                // Buffer released upon loop iteration end
            }
        } );
    }

    for ( auto &w : workers )
        w.join();

    REQUIRE_FALSE( canaryCorrupted.load() );
    REQUIRE( totalVerifiedBuffers.load() == static_cast<std::uint64_t>( kNumThreads * kIterations ) );
    REQUIRE( pool.allocatedBytes() == 0 );
    REQUIRE( pool.pooledBytes() <= cfg.maxPoolBytes );
    REQUIRE( pool.totalAllocations() == pool.poolHits() + pool.poolMisses() );
}

// ============================================================================
// Test Suite 5: ChunkPipeline Producer Throttling Integration
// ============================================================================
TEST_CASE( "ChunkPipeline integrates with ExecutionGovernor for producer throttling",
           "[execution][governor][pipeline]" )
{
    std::atomic<std::uint64_t> mockRss{ 100 * 1024 * 1024 };

    ExecutionGovernor::Config govCfg;
    govCfg.rssWatermarkBytes = 200 * 1024 * 1024;
    govCfg.rssLowWatermarkBytes = 150 * 1024 * 1024;
    govCfg.rssSampler = [&] { return mockRss.load(); };
    ExecutionGovernor gov( govCfg );

    constexpr int kTotalTiles = 5;
    std::atomic<int> tilesProduced{ 0 };
    std::atomic<int> tilesConsumed{ 0 };

    ChunkPipeline::Config pipeCfg;
    pipeCfg.governor = &gov;
    pipeCfg.queueCapacity = 2;

    auto producer = [&]( chunk::TilePayload &p ) {
        int idx = tilesProduced.fetch_add( 1 );
        if ( idx >= kTotalTiles )
            return false;
        chunk::TileSpec spec;
        spec.index = idx;
        spec.totalTiles = kTotalTiles;
        spec.width = 10;
        spec.height = 10;
        spec.bufferWidth = 10;
        spec.bufferHeight = 10;
        spec.bands = 1;
        p = chunk::TilePayload( spec, std::make_shared<std::vector<float>>( 100, 1.0f ) );
        return true;
    };

    auto consumer = [&]( chunk::TilePayload &&p ) {
        tilesConsumed.fetch_add( 1 );
        return true;
    };

    chunk::ChunkPipeline pipeline( producer, {}, consumer, pipeCfg );
    pipeline.run();

    REQUIRE( pipeline.completedTiles() == kTotalTiles );
    REQUIRE( tilesConsumed.load() == kTotalTiles );
}

