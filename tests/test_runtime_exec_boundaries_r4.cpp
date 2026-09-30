// test_runtime_exec_boundaries_r4.cpp — Track 15 WP-B, exec subdomain.
// Boundary oracles for ExecutionGovernor: the exact ±1 admission ladder
// arithmetic, the typed refusal payload, the documented budget-override
// contract, write-gate backpressure (real thread), and leak-report lifecycle
// isolation between governors.
#include <catch2/catch_test_macros.hpp>
#include "support/bounded_wait.h"

#include "runtime/exec/execution_governor.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <future>
#include <string>
#include <system_error>
#include <thread>

using namespace sicnu::runtime::exec;
using namespace sicnu::runtime::chunk;

namespace
{
// Reference shape: 256x256 tile, 1 band, float32 → perTile = 256 KiB.
// Linear chain (stageCount 0, inputCount 1), queue capacity 2:
//   queued = 1*2*1 = 2, inHand = 2, drain = 1 → 5 tiles = 1_310_720 B.
// At capacity 1: 4 tiles = 1_048_576 B.
constexpr std::uint64_t kPerTile = 256ull * 256ull * 4ull;
constexpr std::uint64_t kPeakCap2 = 5ull * kPerTile;      // 1_310_720
constexpr std::uint64_t kPeakCap1 = 4ull * kPerTile;      // 1_048_576

TileMemoryRequest referenceRequest()
{
    TileMemoryRequest request;
    request.tileWidth = 256;
    request.tileHeight = 256;
    request.haloPixels = 0;
    request.bands = 1;
    request.bytesPerSample = 4;
    request.inputCount = 1;
    request.stageCount = 0;
    request.requestedQueueCapacity = 2;
    return request;
}
} // namespace

TEST_CASE( "Admission ladder exact boundaries: ==budget admits, +1 byte reduces, below the minimum refuses",
           "[runtime][exec][r4]" )
{
    // An unbounded governor (all budgets 0) forwards the caller's budget.
    ExecutionGovernor governor( ExecutionGovernor::Config{} );

    TileMemoryRequest exact = referenceRequest();
    exact.budgetBytes = kPeakCap2;
    {
        const TileMemoryPlan plan = governor.admitOrRefuse( exact );
        REQUIRE( plan.action == TileMemoryPlan::Action::Admit );
        REQUIRE( plan.estimatedPeakBytes == kPeakCap2 );
    }

    TileMemoryRequest oneOver = exact;
    oneOver.budgetBytes = kPeakCap2 - 1;
    {
        const TileMemoryPlan plan = governor.admitOrRefuse( oneOver );
        REQUIRE( plan.action == TileMemoryPlan::Action::ReduceConcurrency );
        REQUIRE( plan.estimatedPeakBytes == kPeakCap1 );
        REQUIRE( plan.recommendedQueueCapacity == 1 );
        REQUIRE( plan.fits() );
    }

    // Below the RAM-minimal shape, without spill: typed refusal — the caller
    // must never discover the working set via std::bad_alloc.
    TileMemoryRequest hopeless = oneOver;
    hopeless.budgetBytes = kPeakCap1 - 1;
    hopeless.allowSpill = false;
    REQUIRE_THROWS_AS( governor.admitOrRefuse( hopeless ), AdmissionRefused );
}

TEST_CASE( "AdmissionRefused carries the planner's structured need/budget reason",
           "[runtime][exec][r4]" )
{
    ExecutionGovernor governor( ExecutionGovernor::Config{} );
    TileMemoryRequest request = referenceRequest();
    request.budgetBytes = kPeakCap1 - 1;

    bool caught = false;
    try
    {
        (void)governor.admitOrRefuse( request );
    }
    catch ( const AdmissionRefused &refused )
    {
        caught = true;
        REQUIRE_FALSE( refused.reason.empty() );
        // The reason names the minimum shape and the budget (actionable, not
        // a bare "refused").
        REQUIRE( refused.reason.find( "cannot fit" ) != std::string::npos );
        REQUIRE( refused.reason.find( std::to_string( request.budgetBytes ) ) != std::string::npos );
        REQUIRE( refused.reason.find( std::to_string( kPeakCap1 ) ) != std::string::npos );
    }
    REQUIRE( caught );
}

TEST_CASE( "The spill rung needs declared scratch and reports the spill bytes",
           "[runtime][exec][r4]" )
{
    ExecutionGovernor governor( ExecutionGovernor::Config{} );
    TileMemoryRequest request = referenceRequest();
    request.budgetBytes = kPeakCap1 - 1; // RAM cannot admit any shape
    request.allowSpill = true;
    request.expectedTileCount = 4;
    request.scratchBudgetBytes = 4 * kPerTile; // scratch covers all intermediates

    const TileMemoryPlan plan = governor.admitOrRefuse( request );
    REQUIRE( plan.action == TileMemoryPlan::Action::Spill );
    REQUIRE( plan.spillBytes == 4 * kPerTile );
    // fits() is the planner's IN-RAM predicate — Spill deliberately sits
    // outside it (the shape only executes WITH external memory).
    REQUIRE_FALSE( plan.fits() );
    REQUIRE( plan.reason.find( "spill" ) != std::string::npos );

    // One byte short on scratch: the same shape refuses.
    request.scratchBudgetBytes = 4 * kPerTile - 1;
    REQUIRE_THROWS_AS( governor.admitOrRefuse( request ), AdmissionRefused );
}

TEST_CASE( "Governor-configured budgets override a looser caller budget (documented override)",
           "[runtime][exec][r4]" )
{
    // The governor owns its host's budget triple: a caller asking for more
    // RAM than the host allows is capped at the configured value (this pin
    // documents the intentional override at execution_governor.cpp —
    // configured budget wins, never the union).
    ExecutionGovernor::Config config;
    config.ramBytes = kPeakCap1; // host only ever allows the minimal shape
    ExecutionGovernor governor( config );

    TileMemoryRequest request = referenceRequest();
    request.budgetBytes = 1ull << 40; // caller delusion: 1 TiB
    const TileMemoryPlan plan = governor.admitOrRefuse( request );
    REQUIRE( plan.action == TileMemoryPlan::Action::ReduceConcurrency );
    REQUIRE( plan.estimatedPeakBytes == kPeakCap1 );

    // Advisory mode is untouched by the override when ramBytes==0.
    ExecutionGovernor unbounded( ExecutionGovernor::Config{} );
    TileMemoryRequest advisory = referenceRequest(); // budgetBytes == 0
    const TileMemoryPlan estimate = unbounded.admitOrRefuse( advisory );
    REQUIRE( estimate.action == TileMemoryPlan::Action::Advisory );
    REQUIRE( estimate.estimatedPeakBytes == kPeakCap2 );
}

TEST_CASE( "A fresh governor starts with a clean leak-report slate",
           "[runtime][exec][r4]" )
{
    // Governor A leaks a scratch lease on destruction: its report is the
    // visible "last leak report".
    std::string leakedRoot;
    std::optional<sicnu::runtime::chunk::ScratchLease> leakedLease;
    {
        ExecutionGovernor::Config config;
        config.scratchRoot = ( std::filesystem::temp_directory_path()
                               / "r4-gov-leak-a" )
                                  .string();
        config.scratchBytes = 1024; // bounded: budgeted leases are accounted
        ExecutionGovernor leaking( config );
        auto acquired = leaking.scratch().acquire( "run-leak", "tile", 128 );
        REQUIRE( leaking.hasOutstandingResources() );
        leakedRoot = leaking.scratch().root();
        // THE LEAK: the live lease MOVES OUT and survives the governor's
        // destruction — the destructor's leak check then sees outstanding
        // scratch bytes and emits the diagnostic report.
        leakedLease = std::move( acquired );
    } // destructor emits the report
    REQUIRE_FALSE( ExecutionGovernor::lastLeakReportJson().empty() );

    // Governor B is constructed and destroyed clean: the stale report from A
    // must not survive B's construction — a fresh observation window.
    {
        ExecutionGovernor::Config config;
        config.scratchRoot = ( std::filesystem::temp_directory_path()
                               / "r4-gov-clean-b" )
                                  .string();
        config.scratchBytes = 1024; // bounded: budgeted leases are accounted
        ExecutionGovernor clean( config );
        REQUIRE_FALSE( clean.hasOutstandingResources() );
    }
    REQUIRE( ExecutionGovernor::lastLeakReportJson().empty() );

    // The leak test intentionally leaves a scratch file behind (the leak IS
    // the scenario); clean the fixture roots so the host stays unpolluted.
    std::error_code cleanupEc;
    std::filesystem::remove_all(
        std::filesystem::temp_directory_path() / "r4-gov-leak-a", cleanupEc );
    std::filesystem::remove_all(
        std::filesystem::temp_directory_path() / "r4-gov-clean-b", cleanupEc );
}

TEST_CASE( "Write gate: exact-cap admits, oversized writes escape only when idle, and a waiter unblocks on release",
           "[runtime][exec][r4]" )
{
    BoundedWriteGate gate( 100 );

    // Oversized request on an idle gate: never-starve escape (documented).
    {
        BoundedWriteGate::Reservation big( gate, 1000 );
        REQUIRE( gate.outstandingBytes() == 1000 );
    }
    REQUIRE( gate.outstandingBytes() == 0 );

    // Exact fit under load + a real second thread unblocked by release.
    // Ordering is promise-synchronized: the waiter only STARTS acquiring
    // while the gate is full, so "blocked while full, proceeds after
    // release" is observed deterministically.
    std::promise<void> startGate;
    std::promise<void> doneGate;
    std::future<void> started = startGate.get_future();
    std::future<void> done = doneGate.get_future();
    auto waiter = std::async( std::launch::async, [&gate, &startGate, &doneGate] {
        startGate.set_value();
        BoundedWriteGate::Reservation reservation( gate, 50 );
        const std::uint64_t observed = gate.outstandingBytes();
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        doneGate.set_value();
        return observed;
    } );
    {
        BoundedWriteGate::Reservation holder( gate, 100 );
        REQUIRE( gate.outstandingBytes() == 100 );
        // Bounded (#1392): the waiter sets started as its first statement.
        REQUIRE( started.wait_for( std::chrono::seconds( 5 ) ) == std::future_status::ready );
        // The waiter cannot fit while 100/100 is outstanding: bounded
        // negative-observation window (fail fast if it completes early).
        CHECK_FALSE( sicnu_test::waitUntil(
            [&] { return done.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready; },
            100,
            [] { return "waiter completed although 100/100 bytes were outstanding"; } ) );
    } // holder releases → waiter proceeds
    REQUIRE( waiter.get() == 50 );
    REQUIRE( gate.outstandingBytes() == 0 );
}
