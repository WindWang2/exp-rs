// test_scheduler3.cpp — Phase G multi-dimension scheduler contracts:
// per-dimension caps, interactive reserve, aging (no starvation), and the
// never-starve rule.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/task_resource_budget2.h"

#include <chrono>

using namespace sicnu;

namespace
{
ResourceRequest makeRequest( LatencyClass cls, unsigned int readW, unsigned int writeW,
                             int priority = 2 )
{
    ResourceRequest r;
    r.latencyClass = cls;
    r.diskReadWeight = readW;
    r.diskWriteWeight = writeW;
    r.priority = priority;
    r.submitStamp = std::chrono::steady_clock::now();
    return r;
}
} // namespace

TEST_CASE( "Scheduler3 gates on each dimension with caps", "[scheduler3]" )
{
    SchedulerLimits limits;
    limits.ramMb = 8192;
    limits.vramMb = 4096;
    limits.cpuThreads = 8;
    TaskResourceBudget2 budget( limits );

    ResourceUsage running;
    running.ramMb = 7000;
    running.vramMb = 0;
    running.cpuThreads = 4;

    ResourceRequest heavy;
    heavy.ramMb = 2000; // 7000 + 2000 > 8192 → hold
    heavy.vramMb = 1024;
    heavy.cpuThreads = 2;
    heavy.latencyClass = LatencyClass::Interactive;
    REQUIRE_FALSE( budget.canLaunch( running, heavy, std::chrono::steady_clock::now() ) );

    ResourceRequest small;
    small.ramMb = 1000;
    small.vramMb = 0;
    small.cpuThreads = 4;
    REQUIRE( budget.canLaunch( running, small, std::chrono::steady_clock::now() ) );

    // VRAM dimension gates independently of everything else.
    ResourceRequest gpu;
    gpu.ramMb = 0;
    gpu.cpuThreads = 0;
    gpu.vramMb = 2048;
    REQUIRE( budget.canLaunch( ResourceUsage{}, gpu, std::chrono::steady_clock::now() ) );
    running.vramMb = 2048;
    // 2048 used + 4096 wanted > 4096 cap → held (exact fit would admit).
    gpu.vramMb = 4096;
    REQUIRE_FALSE( budget.canLaunch( running, gpu, std::chrono::steady_clock::now() ) );
}

TEST_CASE( "Scheduler3 interactive reserve protects the UI lane", "[scheduler3]" )
{
    SchedulerLimits limits;
    limits.diskWriteWeight = 100;
    limits.interactiveReservePercent = 25; // batch cannot exceed 75 projected
    TaskResourceBudget2 budget( limits );

    ResourceUsage running;
    running.diskWriteWeight = 60;

    // Batch 20: 60+20=80 > 75 → held despite fitting the raw cap.
    ResourceRequest batch = makeRequest( LatencyClass::Batch, 0, 20 );
    REQUIRE_FALSE( budget.canLaunch( running, batch, std::chrono::steady_clock::now() ) );

    // Interactive 20: reserve-protected → admits against the full cap.
    ResourceRequest interactive = makeRequest( LatencyClass::Interactive, 0, 20 );
    REQUIRE( budget.canLaunch( running, interactive, std::chrono::steady_clock::now() ) );
}

TEST_CASE( "Scheduler3 aging prevents starvation", "[scheduler3]" )
{
    using namespace std::chrono;
    SchedulerLimits limits;
    limits.diskWriteWeight = 100;
    limits.interactiveReservePercent = 25;
    TaskResourceBudget2 budget( limits );

    ResourceUsage running;
    running.diskWriteWeight = 60;

    auto then = std::chrono::steady_clock::now() - milliseconds( 11000 );
    ResourceRequest oldBatch = makeRequest( LatencyClass::Batch, 0, 20 );
    oldBatch.submitStamp = then;

    // Un-aged batch is held by the reserve…
    ResourceRequest freshBatch = makeRequest( LatencyClass::Batch, 0, 20 );
    REQUIRE_FALSE( budget.canLaunch( running, freshBatch, std::chrono::steady_clock::now() ) );
    // …but two aging promotions (11s / 5s) let it through the reserve lane.
    REQUIRE( budget.canLaunch( running, oldBatch, std::chrono::steady_clock::now() ) );
    REQUIRE( agedPriority( oldBatch, std::chrono::steady_clock::now() ) == 0 );
}

TEST_CASE( "Scheduler3 weight cap 0 disables the dimension", "[scheduler3]" )
{
    SchedulerLimits limits; // defaults: weights 100; ram/vram/cpu 0 = off
    TaskResourceBudget2 budget( limits );
    ResourceRequest r = makeRequest( LatencyClass::Batch, 40, 40 );
    REQUIRE( budget.canLaunch( ResourceUsage{}, r, std::chrono::steady_clock::now() ) );
    ResourceUsage full;
    full.diskReadWeight = 100;
    full.diskWriteWeight = 100;
    REQUIRE_FALSE( budget.canLaunch( full, r, std::chrono::steady_clock::now() ) );
}
