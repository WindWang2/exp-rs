// tests/test_r15_C_chunk_admission.cpp — Track 15 WP-C11/C12: the RAM
// admission ladder and the VRAM ledger are admission AUTHORITIES — they must
// never admit a shape above the budget, and their arithmetic must never wrap.
//
// Oracles (.planning/r15-c/ORACLES.md):
//   C11/C12 F1: a ReduceConcurrency plan is admitted ONLY at the planner's
//     recommended queue capacity. A mid-range budget (fits at capacity 1,
//     not at capacity 2) must run at capacity 1 — the in-flight tile census
//     proves it (≤3 live tiles, not 4).
//   C11 F2: VramLedger::tryReserve refuses an absurd (INT_MAX) estimate
//     instead of wrapping the int sum negative and admitting everything.
//   C11 F3: a refused FIRST-TIME reservation leaves no 0-MiB holder behind
//     (snapshot() must not count failed attempts as live sessions).
//   C11 F4: release() honors the amount handed in instead of always
//     erasing the whole holder.
#include <catch2/catch_test_macros.hpp>

#include "operators/framework/chunked_run.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/runtime/device_planner.h"
#include "runtime/chunk/memory_planner.h"

#include <json/json.h>

#include <QDir>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace sicnu::operators;
using namespace sicnu::runtime::chunk;

/// 16x16x1 float32 tiles: perTile = 1024 B; the stream model gives
/// peak(capacity) = (capacity + 3) * 1024 B → 5120 B at capacity 2 and
/// 4096 B at capacity 1. A budget of 4608 B fits ONLY the reduced shape.
constexpr int kTileW = 16;
constexpr int kTileH = 16;
constexpr int kBands = 1;
constexpr std::uint64_t kPerTile = 1024;
constexpr std::uint64_t kPeakAtCapacity2 = 5 * kPerTile;
constexpr std::uint64_t kPeakAtCapacity1 = 4 * kPerTile;
constexpr std::uint64_t kMidRangeBudget = 4608;

TileRunPartition censusPartition( int tiles )
{
    TileRunPartition p;
    p.rasterWidth = kTileW;
    p.rasterHeight = kTileH * static_cast<int>( tiles );
    p.tileWidth = kTileW;
    p.tileHeight = kTileH;
    p.bands = kBands;
    return p;
}

std::vector<float> censusKernel( const TileSpec &s )
{
    return std::vector<float>( static_cast<std::size_t>( s.bufferElementCount() ),
                               static_cast<float>( s.index ) );
}

} // namespace

TEST_CASE( "R15-C C11: the planner reports ReduceConcurrency for the mid-range budget",
           "[chunk][admission][r15-c]" )
{
    TileMemoryRequest request;
    request.tileWidth = kTileW;
    request.tileHeight = kTileH;
    request.bands = kBands;
    request.stageCount = 0;
    request.requestedQueueCapacity = 2;
    request.budgetBytes = kMidRangeBudget;

    const TileMemoryPlan plan = planTileMemory( request );
    CHECK( plan.action == TileMemoryPlan::Action::ReduceConcurrency );
    CHECK( plan.recommendedQueueCapacity == 1 );
    CHECK( plan.requestedPeakBytes == kPeakAtCapacity2 );
    CHECK( plan.estimatedPeakBytes == kPeakAtCapacity1 );
    CHECK( plan.fits() );

    // Below the minimal shape the ladder still refuses typed.
    TileMemoryRequest tooSmall = request;
    tooSmall.budgetBytes = kPeakAtCapacity1 - 1;
    CHECK( planTileMemory( tooSmall ).action == TileMemoryPlan::Action::Refuse );
}

TEST_CASE( "R15-C C11/C12: a reduced admission runs at the recommended capacity",
           "[chunk][admission][r15-c]" )
{
    QTemporaryDir dir;
    RSOperatorContext ctx( dir.filePath( QStringLiteral( "work" ) ).toStdString() );
    ChunkedRunOptions options;
    options.mode = ChunkedRunOptions::Mode::Pipeline;
    options.scratchRoot = dir.filePath( QStringLiteral( "scratch" ) ).toStdString();
    options.pipelineQueueCapacity = 2; // the requested shape does NOT fit
    options.ramBudgetBytes = kMidRangeBudget;

    // In-flight census: a tile is "live" from its kernel call until the sink
    // finished with it. The sink holds its FIRST tile until the producer can
    // no longer run ahead (produced reaches 4, or goes quiet for 500 ms), so
    // the peak is deterministic: capacity 2 → 4 live tiles, capacity 1 → 3.
    std::atomic<int> inFlight{ 0 };
    std::atomic<int> maxInFlight{ 0 };
    std::atomic<int> produced{ 0 };
    std::atomic<int> lastSeenProduced{ -1 };
    std::atomic<bool> firstTile{ true };
    std::vector<std::uint64_t> order;
    std::mutex orderMutex;

    const ChunkTileKernel kernel = [&]( const TileSpec &s ) {
        const int live = inFlight.fetch_add( 1, std::memory_order_relaxed ) + 1;
        int prev = maxInFlight.load( std::memory_order_relaxed );
        while ( live > prev && !maxInFlight.compare_exchange_weak( prev, live ) )
        {
        }
        produced.fetch_add( 1, std::memory_order_relaxed );
        return censusKernel( s );
    };

    const std::function<void( const TilePayload & )> sink = [&]( const TilePayload &p ) {
        {
            std::lock_guard<std::mutex> lock( orderMutex );
            order.push_back( p.spec.index );
        }
        if ( firstTile.exchange( false ) )
        {
            // Hold the stream until the producer cannot run ahead any further.
            const auto started = std::chrono::steady_clock::now();
            int quiet = 0;
            while ( produced.load( std::memory_order_relaxed ) < 4 )
            {
                const int now = produced.load( std::memory_order_relaxed );
                quiet = ( now == lastSeenProduced.load( std::memory_order_relaxed ) ) ? quiet + 1
                                                                                     : 0;
                lastSeenProduced.store( now, std::memory_order_relaxed );
                if ( quiet >= 5 ) // ~500 ms without progress: the producer is parked
                    break;
                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                if ( std::chrono::steady_clock::now() - started > std::chrono::seconds( 10 ) )
                    break;
            }
        }
        inFlight.fetch_sub( 1, std::memory_order_relaxed );
    };

    const TileRunPartition partition = censusPartition( 8 );
    const ChunkedRunResult result =
      runChunkedOperator( "r15-c-census", Json::Value( Json::objectValue ), ctx, partition,
                          TileRunDeterminism::BitExact, kernel, sink, options );

    CHECK( result.totalTiles == partition.totalTiles() );
    // Every tile sinked exactly once, in index order (scientific equivalence).
    {
        std::lock_guard<std::mutex> lock( orderMutex );
        REQUIRE( order.size() == partition.totalTiles() );
        for ( std::size_t i = 0; i < order.size(); ++i )
            CHECK( order[i] == static_cast<std::uint64_t>( i ) );
    }
    // THE ORACLE: the run executed at the REDUCED capacity (≤3 live tiles).
    // On master the plan was admitted but the pipeline ran at the requested
    // capacity 2 (4 live tiles) — above the run's own budget.
    CHECK( maxInFlight.load( std::memory_order_relaxed ) <= 3 );
}

TEST_CASE( "R15-C C11: the VRAM ledger refuses an absurd estimate instead of wrapping",
           "[models][device][r15-c]" )
{
    using sicnu::operators::runtime::VramLedger;
    VramLedger ledger;
    ledger.setCapacity( 0, 100 );
    std::string why;
    REQUIRE( ledger.tryReserve( 0, 60, "holder-a", &why ) );

    // 60 + INT_MAX wraps negative in int arithmetic → the old check passed
    // and the ledger admitted unbounded further reservations.
    CHECK_FALSE( ledger.tryReserve( 0, 2147483647, "holder-b", &why ) );
    CHECK( ledger.reservedMb( 0 ) == 60 );
    CHECK( ledger.freeMb( 0 ) == 40 );
    // The admission authority still works after the refused attempt.
    CHECK( ledger.tryReserve( 0, 40, "holder-c", &why ) );
    CHECK( ledger.freeMb( 0 ) == 0 );
    CHECK_FALSE( ledger.tryReserve( 0, 1, "holder-d", &why ) );
    CHECK( ledger.reservedMb( 0 ) == 100 );
}

TEST_CASE( "R15-C C11: a refused first-time reservation leaves no phantom holder",
           "[models][device][r15-c]" )
{
    using sicnu::operators::runtime::VramLedger;
    VramLedger ledger;
    ledger.setCapacity( 0, 100 );
    std::string why;
    CHECK_FALSE( ledger.tryReserve( 0, 200, "ghost", &why ) );

    const auto snapshot = ledger.snapshot();
    REQUIRE( snapshot.size() == 1 );
    CHECK( snapshot[0].holders == 0 ); // a failed attempt is not a live session
    CHECK( snapshot[0].reservedMb == 0 );
}

TEST_CASE( "R15-C C11: release honors the amount handed in",
           "[models][device][r15-c]" )
{
    using sicnu::operators::runtime::VramLedger;
    VramLedger ledger;
    ledger.setCapacity( 0, 100 );
    std::string why;
    REQUIRE( ledger.tryReserve( 0, 100, "holder", &why ) );

    ledger.release( 0, 40, "holder" );
    CHECK( ledger.reservedMb( 0 ) == 60 );
    CHECK( ledger.freeMb( 0 ) == 40 );

    // The remainder is still held; an over-release drops the holder and
    // clamps (never negative).
    ledger.release( 0, 1000, "holder" );
    CHECK( ledger.reservedMb( 0 ) == 0 );
    CHECK( ledger.snapshot()[0].holders == 0 );
}
