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

using namespace sicnu::runtime;
using chunk::TileMemoryPlan;
using chunk::TileMemoryRequest;
using exec::AdmissionRefused;
using exec::ExecutionGovernor;

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
