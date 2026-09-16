// test_worker_lease_11.cpp — WP-E: lease expiry, poison quarantine, bounded
// takeover. Oracle: an INJECTED clock (the tracker never sees wall time), so
// every transition is exact arithmetic, not timing.
#include <catch2/catch_test_macros.hpp>

#include "runtime/worker/worker_lease.h"

#include <chrono>
#include <cstdint>

using namespace sicnu::runtime::worker;

namespace
{
struct FakeClock
{
    std::int64_t nowMs = 1'000'000;
    std::int64_t operator()() const { return nowMs; }
};
} // namespace

TEST_CASE( "Lease expires only while a job is held and silent", "[execution][worker]" )
{
    WorkerLeaseConfig cfg;
    cfg.leaseTtl = std::chrono::milliseconds( 5000 );
    cfg.poisonFailureThreshold = 3;
    FakeClock clock;
    WorkerLeaseTracker tracker( cfg );
    tracker.setClock( [&clock] { return clock.nowMs; } );

    REQUIRE( tracker.verdict( "w1" ) == WorkerHealth::Healthy );

    tracker.onJobStart( "w1" );
    clock.nowMs += 4'999;
    REQUIRE( tracker.verdict( "w1" ) == WorkerHealth::Healthy ); // still inside TTL

    clock.nowMs += 2; // past TTL, no frames
    REQUIRE( tracker.verdict( "w1" ) == WorkerHealth::Expired );
    REQUIRE( tracker.stats().expiries == 1 );

    // Repeated verdicts on the SAME silence episode count once.
    REQUIRE( tracker.verdict( "w1" ) == WorkerHealth::Expired );
    REQUIRE( tracker.stats().expiries == 1 );

    // Any frame renews the lease and closes the episode.
    tracker.onLiveness( "w1" );
    REQUIRE( tracker.verdict( "w1" ) == WorkerHealth::Healthy );
    clock.nowMs += 6'000;
    REQUIRE( tracker.verdict( "w1" ) == WorkerHealth::Expired );
    REQUIRE( tracker.stats().expiries == 2 ); // new episode counted
}

TEST_CASE( "Idle workers never expire (job-held silence only)", "[execution][worker]" )
{
    WorkerLeaseConfig cfg;
    cfg.leaseTtl = std::chrono::milliseconds( 100 );
    FakeClock clock;
    WorkerLeaseTracker tracker( cfg );
    tracker.setClock( [&clock] { return clock.nowMs; } );

    tracker.onLiveness( "w-idle" );
    clock.nowMs += 1'000'000; // a very long time, but no job held
    REQUIRE( tracker.verdict( "w-idle" ) == WorkerHealth::Healthy );
}

TEST_CASE( "Poison: consecutive failures quarantine; success resets the streak",
           "[execution][worker]" )
{
    WorkerLeaseConfig cfg;
    cfg.poisonFailureThreshold = 3;
    FakeClock clock;
    WorkerLeaseTracker tracker( cfg );
    tracker.setClock( [&clock] { return clock.nowMs; } );

    tracker.onJobStart( "w2" );
    REQUIRE( tracker.onJobOutcome( "w2", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w2", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.verdict( "w2" ) == WorkerHealth::Healthy );

    // A success clears the streak: 2 fails + 1 success + 2 fails = healthy.
    REQUIRE( tracker.onJobOutcome( "w2", true ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w2", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w2", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.verdict( "w2" ) == WorkerHealth::Healthy );

    // Three consecutive failures quarantine (sticky). Reset first so the
    // streak starts clean — the previous asserts left it at 2.
    REQUIRE( tracker.onJobOutcome( "w2", true ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w2", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w2", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w2", false ) == WorkerHealth::Quarantined );
    REQUIRE( tracker.stats().quarantines == 1 );
    REQUIRE( tracker.verdict( "w2" ) == WorkerHealth::Quarantined );

    // Even a later success does not un-quarantine — only reset() (a real
    // worker restart) does.
    tracker.onJobOutcome( "w2", true );
    REQUIRE( tracker.verdict( "w2" ) == WorkerHealth::Quarantined );
    tracker.reset( "w2" );
    REQUIRE( tracker.verdict( "w2" ) == WorkerHealth::Healthy );
}

TEST_CASE( "Bounded takeover ladder cannot loop", "[execution][worker]" )
{
    WorkerLeaseConfig cfg;
    cfg.maxTakeoverRetries = 2;
    FakeClock clock;
    WorkerLeaseTracker tracker( cfg );
    tracker.setClock( [&clock] { return clock.nowMs; } );

    REQUIRE( tracker.mayTakeover( "w3", 0 ) );
    REQUIRE( tracker.mayTakeover( "w3", 1 ) );
    REQUIRE( tracker.mayTakeover( "w3", 2 ) );
    REQUIRE_FALSE( tracker.mayTakeover( "w3", 3 ) );
    REQUIRE_FALSE( tracker.mayTakeover( "w3", 100 ) );
}

TEST_CASE( "Tracker has no scheduling authority: unknown workers are Healthy",
           "[execution][worker]" )
{
    WorkerLeaseConfig cfg;
    FakeClock clock;
    WorkerLeaseTracker tracker( cfg );
    tracker.setClock( [&clock] { return clock.nowMs; } );
    REQUIRE( tracker.verdict( "never-seen" ) == WorkerHealth::Healthy );
    REQUIRE( tracker.stats().expiries == 0 );
    REQUIRE( tracker.stats().quarantines == 0 );
}
