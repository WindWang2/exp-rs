// tests/test_challenger_m1_2_harness.cpp
// Empirical Challenger Stress Harness for Milestone 1 Memory Governance & Leak Detection

#include "runtime/chunk/bounded_chunk_queue.h"
#include "runtime/chunk/chunk_pipeline.h"
#include "runtime/chunk/disk_tile_store.h"
#include "runtime/chunk/memory_planner.h"
#include "runtime/chunk/scratch_registry.h"
#include "runtime/exec/execution_governor.h"
#include "runtime/observability/execution_telemetry.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::runtime;
using namespace sicnu::runtime::chunk;
using namespace sicnu::runtime::exec;
using namespace sicnu::runtime::observability;

#define HARNESS_ASSERT( cond, msg )                                                    \
    do                                                                                 \
    {                                                                                  \
        if ( !( cond ) )                                                               \
        {                                                                              \
            std::cerr << "[-] ASSERTION FAILED: " << msg << "\n    at " << __FILE__    \
                      << ":" << __LINE__ << " (" #cond ")" << std::endl;               \
            std::exit( 1 );                                                            \
        }                                                                              \
    } while ( 0 )

// ── Suite 1: Buffer Reuse and Zero-Initialization Oracle ─────────────────────
void testBufferReuseAndZeroing()
{
    std::cout << "[*] Running Suite 1: Buffer Reuse and Zero-Initialization Oracle..." << std::endl;

    TileMemoryPool::Config cfg;
    cfg.maxAllocatedBytes = 0; // Unbounded
    cfg.maxPoolBytes = 1024 * 1024; // 1 MB pool
    TileMemoryPool pool( cfg );

    constexpr std::size_t kSize1 = 1024;
    auto buf1 = pool.acquireBuffer( kSize1 );
    HARNESS_ASSERT( buf1 != nullptr, "Initial acquire must succeed" );
    HARNESS_ASSERT( buf1->size() == kSize1, "Buffer size must match requested" );
    HARNESS_ASSERT( pool.allocatedBytes() == kSize1 * sizeof( float ), "Allocated bytes must match" );
    HARNESS_ASSERT( pool.totalAllocations() == 1, "Total allocations must be 1" );
    HARNESS_ASSERT( pool.poolMisses() == 1, "First acquire must be cache miss" );
    HARNESS_ASSERT( pool.poolHits() == 0, "Initial hits must be 0" );

    // Poison buffer with non-zero bit patterns: NaNs, Infinities, negative numbers
    for ( std::size_t i = 0; i < kSize1; ++i )
    {
        if ( i % 4 == 0 )
            ( *buf1 )[i] = std::numeric_limits<float>::quiet_NaN();
        else if ( i % 4 == 1 )
            ( *buf1 )[i] = std::numeric_limits<float>::infinity();
        else if ( i % 4 == 2 )
            ( *buf1 )[i] = -9999.75f;
        else
            ( *buf1 )[i] = static_cast<float>( i + 1 );
    }

    // Release buffer back to pool
    buf1.reset();
    HARNESS_ASSERT( pool.allocatedBytes() == 0, "Allocated bytes must drop to 0 after release" );
    HARNESS_ASSERT( pool.pooledBytes() >= kSize1 * sizeof( float ), "Pooled bytes must retain capacity" );
    HARNESS_ASSERT( pool.pooledCount() == 1, "Free pool must have 1 buffer" );

    // Re-acquire buffer of same size: MUST be a cache hit and MUST be completely zeroed
    auto buf2 = pool.acquireBuffer( kSize1 );
    HARNESS_ASSERT( buf2 != nullptr, "Re-acquire must succeed" );
    HARNESS_ASSERT( buf2->size() == kSize1, "Buffer size must match" );
    HARNESS_ASSERT( pool.poolHits() == 1, "Must be a cache hit" );
    HARNESS_ASSERT( pool.poolMisses() == 1, "Misses must not increase" );
    HARNESS_ASSERT( pool.pooledCount() == 0, "Free pool must be drained" );

    // Zero-initialization oracle: every single byte must be 0x00
    for ( std::size_t i = 0; i < kSize1; ++i )
    {
        HARNESS_ASSERT( ( *buf2 )[i] == 0.0f, "Element must be 0.0f" );
        std::uint32_t bits = 0;
        std::memcpy( &bits, &( *buf2 )[i], sizeof( float ) );
        HARNESS_ASSERT( bits == 0x00000000u, "Element bits must be strictly zero (no negative zero / NaNs)" );
    }

    // Dirty again and release
    std::fill( buf2->begin(), buf2->end(), 123.456f );
    buf2.reset();

    // Re-acquire with smaller size: 512 floats (fits within 1024 capacity)
    constexpr std::size_t kSizeSmall = 512;
    auto bufSmall = pool.acquireBuffer( kSizeSmall );
    HARNESS_ASSERT( bufSmall != nullptr, "Smaller acquire must succeed" );
    HARNESS_ASSERT( bufSmall->size() == kSizeSmall, "Smaller buffer size must match" );
    HARNESS_ASSERT( pool.poolHits() == 2, "Smaller acquire must reuse larger pooled buffer (hit)" );
    for ( std::size_t i = 0; i < kSizeSmall; ++i )
    {
        HARNESS_ASSERT( ( *bufSmall )[i] == 0.0f, "Smaller buffer elements must all be 0.0f" );
    }

    // Now acquire larger size: 4096 floats (exceeds 1024 capacity of pooled buffer if any)
    constexpr std::size_t kSizeLarge = 4096;
    auto bufLarge = pool.acquireBuffer( kSizeLarge );
    HARNESS_ASSERT( bufLarge != nullptr, "Large acquire must succeed" );
    HARNESS_ASSERT( bufLarge->size() == kSizeLarge, "Large buffer size must match" );
    HARNESS_ASSERT( pool.poolMisses() == 2, "Large acquire must be a cache miss" );

    // Best-fit selection oracle:
    // Drop both buffers: now pool has 1024-capacity buffer and 4096-capacity buffer
    bufSmall.reset();
    bufLarge.reset();
    HARNESS_ASSERT( pool.pooledCount() == 2, "Pool must have 2 cached buffers" );

    const std::uint64_t pooledBefore = pool.pooledBytes();
    // Request 512 floats: best fit should be the 1024 buffer (4096 bytes), NOT the 4096 buffer (16384 bytes)
    auto bufFit = pool.acquireBuffer( 512 );
    HARNESS_ASSERT( bufFit != nullptr, "Best fit acquire must succeed" );
    const std::uint64_t pooledAfter = pool.pooledBytes();
    // Decrement should be exactly the capacity of the 1024 buffer (4096 bytes)
    HARNESS_ASSERT( pooledBefore - pooledAfter == 1024 * sizeof( float ),
                    "Best fit algorithm must select smallest fitting buffer" );

    std::cout << "  -> Suite 1 PASSED." << std::endl;
}

// ── Suite 2: Max Allocated Bytes Bounding & Never-Starve Rule ─────────────────
void testMaxAllocatedBytesBounding()
{
    std::cout << "[*] Running Suite 2: Max Allocated Bytes Bounding & Never-Starve..." << std::endl;

    TileMemoryPool::Config cfg;
    cfg.maxAllocatedBytes = 16384; // 16 KB = 4096 floats
    cfg.maxPoolBytes = 0;
    TileMemoryPool pool( cfg );

    auto b1 = pool.acquireBuffer( 2048 ); // 8192 B
    auto b2 = pool.acquireBuffer( 2048 ); // 8192 B
    HARNESS_ASSERT( b1 && b2, "Initial allocations within budget must succeed" );
    HARNESS_ASSERT( pool.allocatedBytes() == 16384, "Pool must be exactly at maxAllocatedBytes" );

    // tryAcquireBuffer must fail immediately when full
    auto b3 = pool.tryAcquireBuffer( 1 );
    HARNESS_ASSERT( b3 == nullptr, "tryAcquireBuffer must return nullptr when capacity full" );

    // acquireBuffer with timeout must timeout
    auto b4 = pool.acquireBuffer( 1, nullptr, std::chrono::milliseconds( 30 ) );
    HARNESS_ASSERT( b4 == nullptr, "acquireBuffer with timeout must return nullptr when full" );

    // Waiter thread unblocked on release
    std::atomic<bool> waiterSucceeded{ false };
    std::shared_ptr<std::vector<float>> waiterBuf;
    std::thread waiter( [&] {
        waiterBuf = pool.acquireBuffer( 2048, nullptr, std::chrono::milliseconds( 1000 ) );
        if ( waiterBuf )
            waiterSucceeded = true;
    } );

    std::this_thread::sleep_for( std::chrono::milliseconds( 40 ) );
    HARNESS_ASSERT( !waiterSucceeded.load(), "Waiter must remain blocked while budget exhausted" );

    // Release b1 -> frees 8192 B -> waiter should wake and acquire
    b1.reset();
    waiter.join();
    HARNESS_ASSERT( waiterSucceeded.load(), "Waiter must unblock after buffer released" );
    HARNESS_ASSERT( pool.allocatedBytes() == 16384, "Total allocated must remain bounded at 16384" );

    // Test cancellation while waiting
    std::atomic<bool> cancelFlag{ false };
    std::atomic<bool> cancelledCaught{ false };
    std::thread cancelWaiter( [&] {
        try
        {
            auto b = pool.acquireBuffer( 1024, &cancelFlag );
        }
        catch ( const ChunkCancelled & )
        {
            cancelledCaught = true;
        }
    } );

    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    HARNESS_ASSERT( !cancelledCaught.load(), "Thread must be waiting before cancel raised" );
    cancelFlag.store( true );
    cancelWaiter.join();
    HARNESS_ASSERT( cancelledCaught.load(), "ChunkCancelled must be caught when cancelFlag set" );

    // Clean up active buffers to reach 0 allocatedBytes
    b2.reset();
    waiterBuf.reset();
    HARNESS_ASSERT( pool.allocatedBytes() == 0, "Allocated bytes must be 0 after all releases" );

    // Never-Starve Rule:
    // When pool is idle (allocatedBytes == 0), an oversized allocation must be admitted
    // Request 65536 floats = 262,144 bytes (> maxAllocatedBytes 16,384)
    auto oversized = pool.acquireBuffer( 65536 );
    HARNESS_ASSERT( oversized != nullptr, "Never-starve rule must admit oversized buffer when idle" );
    HARNESS_ASSERT( pool.allocatedBytes() == 65536 * sizeof( float ), "Oversized bytes accounted" );

    // While oversized is held, any further allocation must block / fail
    auto blocked = pool.tryAcquireBuffer( 1 );
    HARNESS_ASSERT( blocked == nullptr, "Oversized buffer must block subsequent allocations" );

    oversized.reset();
    HARNESS_ASSERT( pool.allocatedBytes() == 0, "Pool must be idle again" );

    std::cout << "  -> Suite 2 PASSED." << std::endl;
}

// ── Suite 3: Max Pool Bytes Bounding & Reclamation ────────────────────────────
void testMaxPoolBytesBounding()
{
    std::cout << "[*] Running Suite 3: Max Pool Bytes Bounding & Reclamation..." << std::endl;

    TileMemoryPool::Config cfg;
    cfg.maxAllocatedBytes = 0;
    cfg.maxPoolBytes = 8192; // 8 KB = two 1024-float buffers
    TileMemoryPool pool( cfg );

    std::vector<std::shared_ptr<std::vector<float>>> bufs;
    for ( int i = 0; i < 10; ++i )
    {
        bufs.push_back( pool.acquireBuffer( 1024 ) ); // 4096 bytes each
    }
    HARNESS_ASSERT( pool.allocatedBytes() == 10 * 4096, "All 10 buffers allocated" );

    // Release all 10 buffers sequentially
    for ( auto &b : bufs )
    {
        b.reset();
    }
    bufs.clear();

    HARNESS_ASSERT( pool.allocatedBytes() == 0, "All active buffers released" );
    HARNESS_ASSERT( pool.pooledBytes() == 8192, "Pooled bytes must be capped at maxPoolBytes (8192)" );
    HARNESS_ASSERT( pool.pooledCount() == 2, "Free pool must retain exactly 2 buffers, rest reclaimed" );

    // Test trimPool
    pool.trimPool( 4096 );
    HARNESS_ASSERT( pool.pooledBytes() <= 4096, "trimPool(4096) must reduce pooled bytes" );
    HARNESS_ASSERT( pool.pooledCount() == 1, "trimPool(4096) must leave 1 buffer" );

    pool.trimPool( 0 );
    HARNESS_ASSERT( pool.pooledBytes() == 0, "trimPool(0) must clear pooled bytes" );
    HARNESS_ASSERT( pool.pooledCount() == 0, "trimPool(0) must leave 0 buffers" );

    // Sub-buffer maxPoolBytes test: maxPoolBytes smaller than single buffer
    TileMemoryPool::Config subCfg;
    subCfg.maxPoolBytes = 1000; // smaller than one 1024-float buffer (4096 bytes)
    TileMemoryPool subPool( subCfg );

    auto subBuf = subPool.acquireBuffer( 1024 );
    subBuf.reset();
    HARNESS_ASSERT( subPool.pooledBytes() == 0, "Buffer exceeding maxPoolBytes must be freed to OS immediately" );
    HARNESS_ASSERT( subPool.pooledCount() == 0, "Free pool must remain empty" );

    // releaseBuffer manual recycling test
    auto extBuf = std::make_unique<std::vector<float>>( 1024, 0.0f );
    subPool.releaseBuffer( std::move( extBuf ) );
    HARNESS_ASSERT( subPool.pooledBytes() == 0, "releaseBuffer exceeding maxPoolBytes must discard" );

    std::cout << "  -> Suite 3 PASSED." << std::endl;
}

// ── Suite 4: Reverse-Destruction Safety ───────────────────────────────────────
void testReverseDestruction()
{
    std::cout << "[*] Running Suite 4: Reverse-Destruction Safety..." << std::endl;

    std::shared_ptr<std::vector<float>> b1, b2, b3, bAdopted;
    {
        TileMemoryPool pool;
        b1 = pool.acquireBuffer( 512 );
        b2 = pool.acquireBuffer( 1024 );
        b3 = pool.acquireBuffer( 2048 );

        std::vector<float> ext( 1024, 3.1415f );
        bAdopted = pool.adopt( std::move( ext ) );

        ( *b1 )[0] = 111.0f;
        ( *b2 )[0] = 222.0f;
        ( *b3 )[0] = 333.0f;
        ( *bAdopted )[0] = 444.0f;

        // pool goes out of scope and is destroyed here!
    }

    // Verify data remains accessible
    HARNESS_ASSERT( ( *b1 )[0] == 111.0f, "b1 data intact after pool destruction" );
    HARNESS_ASSERT( ( *b2 )[0] == 222.0f, "b2 data intact after pool destruction" );
    HARNESS_ASSERT( ( *b3 )[0] == 333.0f, "b3 data intact after pool destruction" );
    HARNESS_ASSERT( ( *bAdopted )[0] == 444.0f, "bAdopted data intact after pool destruction" );

    // Release buffers in reverse order post-pool-death: custom deleter should safely delete without crash
    bAdopted.reset();
    b3.reset();
    b1.reset();
    b2.reset();
    HARNESS_ASSERT( b1 == nullptr && b2 == nullptr && b3 == nullptr && bAdopted == nullptr,
                    "All buffers safely reset without crash" );

    // Reverse destruction with active waiter
    std::atomic<bool> threadCaughtShutdown{ false };
    {
        auto waitingPool = std::make_unique<TileMemoryPool>( TileMemoryPool::Config{ 4096, 0 } );
        auto holdBuf = waitingPool->acquireBuffer( 1024 ); // 4096 bytes, pool full

        std::thread waiter( [&] {
            try
            {
                auto blocked = waitingPool->acquireBuffer( 1024 );
            }
            catch ( const ChunkCancelled &e )
            {
                if ( std::string( e.what() ).find( "shutting down" ) != std::string::npos )
                    threadCaughtShutdown = true;
            }
        } );

        std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
        // Destroy pool while thread is waiting
        waitingPool.reset();
        waiter.join();
        HARNESS_ASSERT( threadCaughtShutdown.load(),
                        "Waiting thread must catch 'shutting down' ChunkCancelled on pool destruction" );
    }

    std::cout << "  -> Suite 4 PASSED." << std::endl;
}

// ── Suite 5: ExecutionGovernor Leak Detection & DiagnosticReport ──────────────
void testLeakDetection()
{
    std::cout << "[*] Running Suite 5: ExecutionGovernor Leak Detection..." << std::endl;

    auto &tel = ExecutionTelemetry::instance();

    // 1. Clean destruction: no leak report
    {
        const auto leaksBefore = tel.counters()["resource_leaks_detected"];
        {
            ExecutionGovernor::Config cfg;
            cfg.maxAllocatedBytes = 1024 * 1024;
            ExecutionGovernor gov( cfg );

            auto buf = gov.memoryPool().acquireBuffer( 256 );
            HARNESS_ASSERT( gov.hasOutstandingResources(), "Has resources while buffer is active" );
            buf.reset();
            HARNESS_ASSERT( !gov.hasOutstandingResources(), "No resources after buffer release" );
        }
        HARNESS_ASSERT( tel.counters()["resource_leaks_detected"] == leaksBefore,
                        "Clean run must not increment resource_leaks_detected" );
    }

    // 2. Memory pool leak detection
    {
        const auto leaksBefore = tel.counters()["resource_leaks_detected"];
        std::shared_ptr<std::vector<float>> leaked;
        {
            ExecutionGovernor::Config cfg;
            cfg.maxAllocatedBytes = 1024 * 1024;
            ExecutionGovernor gov( cfg );

            leaked = gov.memoryPool().acquireBuffer( 512 ); // 2048 B
            HARNESS_ASSERT( gov.hasOutstandingResources(), "Must report outstanding resources" );
        }
        // gov is destroyed with leaked buffer alive
        HARNESS_ASSERT( tel.counters()["resource_leaks_detected"] == leaksBefore + 1,
                        "Destructor must increment resource_leaks_detected on pool leak" );

        const std::string report = ExecutionGovernor::lastLeakReportJson();
        HARNESS_ASSERT( report.find( "exp.diag.v1" ) != std::string::npos, "Report must be exp.diag.v1" );
        HARNESS_ASSERT( report.find( "execution.resource_leak" ) != std::string::npos, "Code must match" );
        HARNESS_ASSERT( report.find( "memory pool outstanding 2048 B" ) != std::string::npos,
                        "Report must cite exact memory pool bytes" );

        leaked.reset(); // Safe reverse destruction
    }

    // 3. Multi-resource leak detection (scratch + write gate + memory pool)
    {
        const auto leaksBefore = tel.counters()["resource_leaks_detected"];
        std::shared_ptr<std::vector<float>> leakedBuf;
        chunk::ScratchLease leakedLease;
        {
            ExecutionGovernor::Config cfg;
            cfg.scratchBytes = 10000;
            cfg.writeInFlightBytes = 10000;
            cfg.maxAllocatedBytes = 10000;
            cfg.scratchRoot = "multi-leak-test";
            ExecutionGovernor gov( cfg );

            leakedLease = gov.scratch().acquire( "run-x", "t0", 1234 );
            gov.writeGate().acquire( 567 );
            leakedBuf = gov.memoryPool().acquireBuffer( 100 ); // 400 B

            HARNESS_ASSERT( gov.hasOutstandingResources(), "Must report outstanding" );
        }

        HARNESS_ASSERT( tel.counters()["resource_leaks_detected"] == leaksBefore + 1,
                        "Multi-leak must increment resource_leaks_detected" );

        const std::string report = ExecutionGovernor::lastLeakReportJson();
        HARNESS_ASSERT( report.find( "scratch outstanding 1234 B" ) != std::string::npos,
                        "Report must cite scratch leak" );
        HARNESS_ASSERT( report.find( "write-gate outstanding 567 B" ) != std::string::npos,
                        "Report must cite write-gate leak" );
        HARNESS_ASSERT( report.find( "memory pool outstanding 400 B" ) != std::string::npos,
                        "Report must cite memory pool leak" );

        leakedBuf.reset();
    }

    std::cout << "  -> Suite 5 PASSED." << std::endl;
}

// ── Suite 6: Watermark Hysteresis & Throttling ────────────────────────────────
void testWatermarkHysteresis()
{
    std::cout << "[*] Running Suite 6: Watermark Hysteresis & Throttling..." << std::endl;

    std::atomic<std::uint64_t> mockRss{ 500 * 1024 * 1024 }; // 500 MB

    ExecutionGovernor::Config cfg;
    cfg.rssWatermarkBytes = 800 * 1024 * 1024;    // 800 MB high
    cfg.rssLowWatermarkBytes = 600 * 1024 * 1024; // 600 MB low
    cfg.rssSampler = [&] { return mockRss.load(); };
    ExecutionGovernor gov( cfg );

    HARNESS_ASSERT( !gov.isWatermarkExceeded(), "500 MB < 800 MB: not exceeded" );

    // Below high watermark: throttleWait returns immediately
    gov.throttleWait();

    // Set RSS above high watermark
    mockRss.store( 850 * 1024 * 1024 );
    HARNESS_ASSERT( gov.isWatermarkExceeded(), "850 MB >= 800 MB: exceeded" );

    // Hysteresis test: thread blocks at 850 MB
    std::atomic<bool> throttleDone{ false };
    std::thread th( [&] {
        gov.throttleWait( nullptr, std::chrono::milliseconds( 5 ) );
        throttleDone = true;
    } );

    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    HARNESS_ASSERT( !throttleDone.load(), "Must remain throttled at 850 MB" );

    // Drop to 700 MB: below high (800) but ABOVE low (600) -> MUST REMAIN THROTTLED!
    mockRss.store( 700 * 1024 * 1024 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    HARNESS_ASSERT( !throttleDone.load(), "Must remain throttled in hysteresis zone (700 MB > 600 MB)" );

    // Drop strictly below low watermark (590 MB) -> MUST UNBLOCK
    mockRss.store( 590 * 1024 * 1024 );
    th.join();
    HARNESS_ASSERT( throttleDone.load(), "Must unblock once RSS < low watermark" );

    // Cancellation during throttle
    mockRss.store( 900 * 1024 * 1024 );
    std::atomic<bool> cancelFlag{ false };
    std::atomic<bool> cancelledThrown{ false };

    std::thread cancelTh( [&] {
        try
        {
            gov.throttleWait( &cancelFlag, std::chrono::milliseconds( 5 ) );
        }
        catch ( const ChunkCancelled & )
        {
            cancelledThrown = true;
        }
    } );

    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    HARNESS_ASSERT( !cancelledThrown.load(), "Must be blocked before cancel" );
    cancelFlag.store( true );
    cancelTh.join();
    HARNESS_ASSERT( cancelledThrown.load(), "Must throw ChunkCancelled immediately upon cancel" );

    std::cout << "  -> Suite 6 PASSED." << std::endl;
}

// ── Suite 7: High-Concurrency Stress Harness ─────────────────────────────────
void testConcurrencyStress()
{
    std::cout << "[*] Running Suite 7: High-Concurrency Stress Harness..." << std::endl;

    TileMemoryPool::Config cfg;
    cfg.maxAllocatedBytes = 64 * 1024; // 64 KB
    cfg.maxPoolBytes = 32 * 1024;      // 32 KB
    TileMemoryPool pool( cfg );

    constexpr int kNumThreads = 8;
    constexpr int kIterations = 200;
    std::vector<std::thread> workers;
    workers.reserve( kNumThreads );

    std::atomic<bool> canaryCorrupted{ false };
    std::atomic<std::uint64_t> completedOps{ 0 };

    for ( int t = 0; t < kNumThreads; ++t )
    {
        workers.emplace_back( [&pool, t, &canaryCorrupted, &completedOps] {
            std::mt19937 rng( 42 + t );
            std::uniform_int_distribution<std::size_t> sizeDist( 32, 512 );
            std::uniform_int_distribution<int> opDist( 0, 4 );

            for ( int iter = 0; iter < kIterations; ++iter )
            {
                const std::size_t count = sizeDist( rng );
                std::shared_ptr<std::vector<float>> buf;

                if ( opDist( rng ) == 0 )
                {
                    buf = pool.tryAcquireBuffer( count );
                }
                else
                {
                    buf = pool.acquireBuffer( count, nullptr, std::chrono::milliseconds( 100 ) );
                }

                if ( !buf )
                    continue;

                // Write canary pattern across all elements
                const float canary = static_cast<float>( ( t + 1 ) * 100000 + iter );
                std::fill( buf->begin(), buf->end(), canary );

                // Small randomized yield
                if ( iter % 5 == 0 )
                    std::this_thread::yield();

                // Verify integrity
                for ( std::size_t idx = 0; idx < buf->size(); ++idx )
                {
                    if ( ( *buf )[idx] != canary )
                    {
                        canaryCorrupted.store( true, std::memory_order_relaxed );
                        break;
                    }
                }

                completedOps++;
                // Buffer released upon loop iteration end
            }
        } );
    }

    for ( auto &w : workers )
        w.join();

    HARNESS_ASSERT( !canaryCorrupted.load(), "Canary corruption detected during concurrent stress!" );
    HARNESS_ASSERT( pool.allocatedBytes() == 0, "Allocated bytes must return to 0" );
    HARNESS_ASSERT( pool.pooledBytes() <= cfg.maxPoolBytes, "Pooled bytes must stay bounded" );
    HARNESS_ASSERT( pool.totalAllocations() == pool.poolHits() + pool.poolMisses(),
                    "Accounting invariant: total == hits + misses" );

    std::cout << "  -> Suite 7 PASSED (completed " << completedOps.load() << " ops across 8 threads)." << std::endl;
}

// ── Suite 8: Corner Cases, Zero Sizing & Exception Rollback ───────────────────
void testCornerCasesAndExceptionSafety()
{
    std::cout << "[*] Running Suite 8: Corner Cases, Zero Sizing & Exception Rollback..." << std::endl;

    TileMemoryPool::Config cfg;
    cfg.maxAllocatedBytes = 65536;
    cfg.maxPoolBytes = 32768;
    TileMemoryPool pool( cfg );

    // 1. Zero element allocation
    auto zeroBuf = pool.acquireBuffer( 0 );
    HARNESS_ASSERT( zeroBuf != nullptr, "acquireBuffer(0) must return valid buffer" );
    HARNESS_ASSERT( zeroBuf->empty(), "zeroBuf must be empty" );
    HARNESS_ASSERT( pool.allocatedBytes() == 0, "allocatedBytes for size 0 must be 0" );
    zeroBuf.reset();
    HARNESS_ASSERT( pool.allocatedBytes() == 0, "allocatedBytes must remain 0" );

    // 2. Adopt empty vector
    std::vector<float> emptyVec;
    auto adoptedEmpty = pool.adopt( std::move( emptyVec ) );
    HARNESS_ASSERT( adoptedEmpty != nullptr, "adopt empty must return valid buffer" );
    HARNESS_ASSERT( adoptedEmpty->empty(), "adopted buffer must be empty" );
    adoptedEmpty.reset();
    HARNESS_ASSERT( pool.allocatedBytes() == 0, "allocatedBytes must remain 0" );

    // 3. Exception rollback test: requesting impossibly large allocation
    bool caughtException = false;
    try
    {
        // 1ULL << 62 floats exceeds std::vector max_size, throws std::length_error
        auto impossible = pool.acquireBuffer( static_cast<std::size_t>( 1ULL << 62 ) );
    }
    catch ( const std::length_error & )
    {
        caughtException = true;
    }
    catch ( const std::bad_alloc & )
    {
        caughtException = true;
    }
    HARNESS_ASSERT( caughtException, "Allocation exceeding max_size must throw" );
    HARNESS_ASSERT( pool.allocatedBytes() == 0, "allocatedBytes must rollback to 0 after exception" );

    // 4. Concurrent trimPool/clearPool while workers acquire and release
    std::atomic<bool> churnRunning{ true };
    std::vector<std::thread> churnWorkers;
    for ( int i = 0; i < 4; ++i )
    {
        churnWorkers.emplace_back( [&] {
            while ( churnRunning.load() )
            {
                auto b = pool.acquireBuffer( 128 );
                if ( b )
                {
                    ( *b )[0] = 77.0f;
                    std::this_thread::yield();
                }
            }
        } );
    }

    // Main thread trims and clears repeatedly
    for ( int i = 0; i < 50; ++i )
    {
        pool.trimPool( 1024 );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        pool.clearPool();
    }

    churnRunning.store( false );
    for ( auto &w : churnWorkers )
        w.join();

    HARNESS_ASSERT( pool.allocatedBytes() == 0, "Allocated bytes must settle at 0 after churn" );
    HARNESS_ASSERT( pool.pooledBytes() <= cfg.maxPoolBytes, "Pooled bytes must stay bounded" );

    std::cout << "  -> Suite 8 PASSED." << std::endl;
}

// ── Suite 9: Dynamic Pipeline Throttling Under RSS Oscillation ───────────────
void testPipelineRssOscillation()
{
    std::cout << "[*] Running Suite 9: Dynamic Pipeline Throttling Under RSS Oscillation..." << std::endl;

    std::atomic<std::uint64_t> mockRss{ 100 * 1024 * 1024 }; // Start at 100 MB

    ExecutionGovernor::Config govCfg;
    govCfg.rssWatermarkBytes = 250 * 1024 * 1024;    // 250 MB
    govCfg.rssLowWatermarkBytes = 180 * 1024 * 1024; // 180 MB
    govCfg.rssSampler = [&] { return mockRss.load(); };
    ExecutionGovernor gov( govCfg );

    constexpr int kTotalTiles = 15;
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
        spec.width = 16;
        spec.height = 16;
        spec.bufferWidth = 16;
        spec.bufferHeight = 16;
        spec.bands = 1;
        p = chunk::TilePayload( spec, std::make_shared<std::vector<float>>( 256, 1.0f ) );
        return true;
    };

    auto consumer = [&]( chunk::TilePayload &&p ) {
        tilesConsumed.fetch_add( 1 );
        return true;
    };

    chunk::ChunkPipeline pipeline( producer, {}, consumer, pipeCfg );

    // Background thread oscillates RSS to trigger and release throttling mid-stream
    std::atomic<bool> pipelineDone{ false };
    std::thread rssOscillator( [&] {
        while ( !pipelineDone.load() )
        {
            mockRss.store( 300 * 1024 * 1024 ); // High watermark spike (throttled)
            std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
            mockRss.store( 150 * 1024 * 1024 ); // Below low watermark (unthrottled)
            std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        }
    } );

    pipeline.run();
    pipelineDone.store( true );
    rssOscillator.join();

    HARNESS_ASSERT( pipeline.completedTiles() == kTotalTiles, "All tiles must complete through throttled pipeline" );
    HARNESS_ASSERT( tilesConsumed.load() == kTotalTiles, "All tiles must be consumed" );

    std::cout << "  -> Suite 9 PASSED." << std::endl;
}

int main()
{
    std::cout << "========================================================\n"
              << "Milestone 1 Empirical Challenger Stress Verification\n"
              << "========================================================" << std::endl;

    testBufferReuseAndZeroing();
    testMaxAllocatedBytesBounding();
    testMaxPoolBytesBounding();
    testReverseDestruction();
    testLeakDetection();
    testWatermarkHysteresis();
    testConcurrencyStress();
    testCornerCasesAndExceptionSafety();
    testPipelineRssOscillation();

    std::cout << "\n[+] ALL 9 CHALLENGE SUITES PASSED EMPIRICALLY!" << std::endl;
    return 0;
}
