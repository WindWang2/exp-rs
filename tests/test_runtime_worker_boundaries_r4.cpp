// test_runtime_worker_boundaries_r4.cpp — Track 15 WP-B, worker subdomain.
// Boundary oracles for the host-side lease policy (WorkerLeaseTracker, exact
// arithmetic on an injected clock) and the worker wire protocol (parseFrame /
// error-frame contract against hostile peer bytes). ADR 0130 execution-plane
// semantics; every expectation is derived from the worker_lease.h contract
// text and the protocol header's own rules, never from the implementation's
// internal state.
#include <catch2/catch_test_macros.hpp>

#include "runtime/worker/worker_lease.h"
#include "runtime/worker/worker_protocol.h"

#include <chrono>
#include <cstdint>
#include <string>

using namespace sicnu::runtime::worker;

namespace
{
struct FakeClock
{
    std::int64_t nowMs = 1'000'000;
    std::int64_t operator()() const { return nowMs; }
};

WorkerLeaseConfig makeConfig( std::chrono::milliseconds ttl, int poisonThreshold = 3,
                              std::uint32_t maxTakeover = 2 )
{
    WorkerLeaseConfig cfg;
    cfg.leaseTtl = ttl;
    cfg.poisonFailureThreshold = poisonThreshold;
    cfg.maxTakeoverRetries = maxTakeover;
    return cfg;
}
} // namespace

TEST_CASE( "Lease TTL boundary is strict: silence == TTL stays Healthy, +1ms expires",
           "[runtime][worker][r4]" )
{
    FakeClock clock;
    WorkerLeaseTracker tracker( makeConfig( std::chrono::milliseconds( 5'000 ), 3, 2 ) );
    tracker.setClock( [ &clock ] { return clock.nowMs; } );

    tracker.onJobStart( "w-ttl" );
    clock.nowMs += 5'000; // exactly the TTL: not yet expired
    REQUIRE( tracker.verdict( "w-ttl" ) == WorkerHealth::Healthy );
    REQUIRE( tracker.stats().expiries == 0 );

    clock.nowMs += 1; // one millisecond past the TTL
    REQUIRE( tracker.verdict( "w-ttl" ) == WorkerHealth::Expired );
    REQUIRE( tracker.stats().expiries == 1 );
}

TEST_CASE( "Quarantine takes precedence over expiry and does not bump expiry stats",
           "[runtime][worker][r4]" )
{
    FakeClock clock;
    WorkerLeaseTracker tracker( makeConfig( std::chrono::milliseconds( 1'000 ), 3, 2 ) );
    tracker.setClock( [ &clock ] { return clock.nowMs; } );

    // Quarantine first (3 consecutive failures).
    tracker.onJobStart( "w-both" );
    REQUIRE( tracker.onJobOutcome( "w-both", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w-both", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w-both", false ) == WorkerHealth::Quarantined );

    // Now the worker holds a new job (host bug or race) and goes silent past
    // the TTL. The sticky quarantine verdict wins; the expiry machinery must
    // not fire a second, contradictory health class.
    tracker.onJobStart( "w-both" );
    clock.nowMs += 60'000;
    REQUIRE( tracker.verdict( "w-both" ) == WorkerHealth::Quarantined );
    REQUIRE( tracker.stats().expiries == 0 );
    REQUIRE( tracker.stats().quarantines == 1 );
}

TEST_CASE( "Liveness never lifts quarantine (sticky until reset)",
           "[runtime][worker][r4]" )
{
    FakeClock clock;
    WorkerLeaseTracker tracker( makeConfig( std::chrono::milliseconds( 1'000 ), 3, 2 ) );
    tracker.setClock( [ &clock ] { return clock.nowMs; } );

    tracker.onJobStart( "w-sticky" );
    tracker.onJobOutcome( "w-sticky", false );
    tracker.onJobOutcome( "w-sticky", false );
    REQUIRE( tracker.onJobOutcome( "w-sticky", false ) == WorkerHealth::Quarantined );

    // Heartbeats keep flowing (worker alive but poisoned): every frame must
    // report the quarantine, and no frame may clear it.
    for ( int i = 0; i < 5; ++i )
    {
        clock.nowMs += 100;
        REQUIRE( tracker.onLiveness( "w-sticky" ) == WorkerHealth::Quarantined );
        REQUIRE( tracker.verdict( "w-sticky" ) == WorkerHealth::Quarantined );
    }
    REQUIRE( tracker.stats().quarantines == 1 );

    tracker.reset( "w-sticky" );
    REQUIRE( tracker.verdict( "w-sticky" ) == WorkerHealth::Healthy );
}

TEST_CASE( "reset clears the failure streak: re-quarantine needs the full threshold again",
           "[runtime][worker][r4]" )
{
    FakeClock clock;
    WorkerLeaseTracker tracker( makeConfig( std::chrono::milliseconds( 1'000 ), 3, 2 ) );
    tracker.setClock( [ &clock ] { return clock.nowMs; } );

    tracker.onJobStart( "w-reset" );
    tracker.onJobOutcome( "w-reset", false );
    tracker.onJobOutcome( "w-reset", false );
    REQUIRE( tracker.onJobOutcome( "w-reset", false ) == WorkerHealth::Quarantined );
    tracker.reset( "w-reset" );

    // Streak restarts from zero: two failures stay healthy.
    tracker.onJobStart( "w-reset" );
    REQUIRE( tracker.onJobOutcome( "w-reset", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w-reset", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.verdict( "w-reset" ) == WorkerHealth::Healthy );

    // Third consecutive failure quarantines again — and the stats count both
    // quarantine episodes.
    REQUIRE( tracker.onJobOutcome( "w-reset", false ) == WorkerHealth::Quarantined );
    REQUIRE( tracker.stats().quarantines == 2 );
}

TEST_CASE( "Degenerate TTL=0 expires on any silence while holding a job",
           "[runtime][worker][r4]" )
{
    FakeClock clock;
    WorkerLeaseTracker tracker( makeConfig( std::chrono::milliseconds( 0 ), 3, 2 ) );
    tracker.setClock( [ &clock ] { return clock.nowMs; } );

    tracker.onJobStart( "w-zero" );
    REQUIRE( tracker.verdict( "w-zero" ) == WorkerHealth::Healthy ); // no silence yet
    clock.nowMs += 1;
    REQUIRE( tracker.verdict( "w-zero" ) == WorkerHealth::Expired );
}

TEST_CASE( "maxTakeoverRetries=0 still admits the episode's first attempt and nothing beyond",
           "[runtime][worker][r4]" )
{
    // Documented semantics (worker_lease.h): mayTakeover is TRUE while the
    // 0-based attempt is WITHIN the configured budget (<=). A zero budget
    // therefore admits exactly the first takeover attempt of an episode and
    // closes the ladder there — pinned so the bound can never silently flip
    // to an off-by-one in either direction.
    FakeClock clock;
    WorkerLeaseTracker tracker( makeConfig( std::chrono::milliseconds( 1'000 ), 3, 0 ) );
    tracker.setClock( [ &clock ] { return clock.nowMs; } );
    REQUIRE( tracker.mayTakeover( "w-takeover0", 0 ) );
    REQUIRE_FALSE( tracker.mayTakeover( "w-takeover0", 1 ) );
}

TEST_CASE( "onJobOutcome on a never-seen worker tracks the poison streak from scratch",
           "[runtime][worker][r4]" )
{
    FakeClock clock;
    WorkerLeaseTracker tracker( makeConfig( std::chrono::milliseconds( 1'000 ), 3, 2 ) );
    tracker.setClock( [ &clock ] { return clock.nowMs; } );

    // Host reports an outcome without a matching onJobStart: the tracker must
    // still account the failure (defensive state creation), never crash and
    // never silently drop the poison signal.
    REQUIRE( tracker.onJobOutcome( "w-ghost", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w-ghost", false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( "w-ghost", false ) == WorkerHealth::Quarantined );
    REQUIRE( tracker.stats().quarantines == 1 );
}

TEST_CASE( "A second onJobStart re-arms the silence window",
           "[runtime][worker][r4]" )
{
    FakeClock clock;
    WorkerLeaseTracker tracker( makeConfig( std::chrono::milliseconds( 1'000 ), 3, 2 ) );
    tracker.setClock( [ &clock ] { return clock.nowMs; } );

    tracker.onJobStart( "w-rearm" );
    clock.nowMs += 900;
    // The host re-dispatches / re-marks the job: the countdown restarts.
    tracker.onJobStart( "w-rearm" );
    clock.nowMs += 900;
    REQUIRE( tracker.verdict( "w-rearm" ) == WorkerHealth::Healthy );
    clock.nowMs += 101;
    REQUIRE( tracker.verdict( "w-rearm" ) == WorkerHealth::Expired );
}

TEST_CASE( "parseFrame refuses deeply nested frames instead of overflowing the stack",
           "[runtime][worker][protocol][r4]" )
{
    // Hostile peer: small line, deep recursion. The protocol gate must answer
    // a typed false — the reader thread must survive.
    std::string deep;
    const int kDepth = 4'096;
    for ( int i = 0; i < kDepth; ++i )
        deep.push_back( '[' );
    for ( int i = 0; i < kDepth; ++i )
        deep.push_back( ']' );

    Json::Value frame;
    REQUIRE_FALSE( parseFrame( deep, frame ) );
}

TEST_CASE( "parseFrame refuses non-object roots and malformed version tags, admits unknown ops",
           "[runtime][worker][protocol][r4]" )
{
    Json::Value frame;
    // Non-object roots must not crash the gate (jsoncpp throws LogicError on
    // isMember() for non-object roots — parseFrame owns the guard).
    REQUIRE_FALSE( parseFrame( "[1,2,3]", frame ) );
    REQUIRE_FALSE( parseFrame( "\"a string\"", frame ) );
    REQUIRE_FALSE( parseFrame( "42", frame ) );
    REQUIRE_FALSE( parseFrame( "null", frame ) );
    // Malformed "v" tags: object/missing/string/double.
    REQUIRE_FALSE( parseFrame( "{\"op\":\"run\"}", frame ) );
    REQUIRE_FALSE( parseFrame( "{\"v\":{},\"op\":\"run\"}", frame ) );
    REQUIRE_FALSE( parseFrame( "{\"v\":\"1\",\"op\":\"run\"}", frame ) );
    REQUIRE_FALSE( parseFrame( "{\"v\":1.5,\"op\":\"run\"}", frame ) );
    // Extension rule: a well-formed v1 frame with an unknown op still parses
    // (the caller decides whether it knows the op).
    REQUIRE( parseFrame( "{\"v\":1,\"op\":\"something-new\"}", frame ) );
    REQUIRE( frame["op"].asString() == "something-new" );
}

TEST_CASE( "frameErrorMeansCancelled is neutral on malformed frames and honours code and legacy text",
           "[runtime][worker][protocol][r4]" )
{
    Json::Value frame;
    // Non-object roots and weird payloads must return false, never throw.
    {
        Json::Value arrayRoot( Json::arrayValue );
        REQUIRE_FALSE( frameErrorMeansCancelled( arrayRoot ) );
    }
    {
        Json::Value objectRoot( Json::objectValue );
        objectRoot["message"] = 17; // wrong type on purpose
        REQUIRE_FALSE( frameErrorMeansCancelled( objectRoot ) );
    }
    // Structured code (7.0 workers). An empty frame has no code and no
    // message: not a cancellation.
    Json::Value empty( Json::objectValue );
    REQUIRE_FALSE( frameErrorMeansCancelled( empty ) );

    Json::Value coded( Json::objectValue );
    coded["code"] = "cancelled";
    REQUIRE( frameErrorMeansCancelled( coded ) );
    Json::Value legacy( Json::objectValue );
    legacy["message"] = "cancelled";
    REQUIRE( frameErrorMeansCancelled( legacy ) );
    Json::Value other( Json::objectValue );
    other["code"] = "outOfMemory";
    other["message"] = "device OOM";
    REQUIRE_FALSE( frameErrorMeansCancelled( other ) );
    // A "cancelled" code beats a non-cancelled message.
    Json::Value mixed( Json::objectValue );
    mixed["code"] = "cancelled";
    mixed["message"] = "failed";
    REQUIRE( frameErrorMeansCancelled( mixed ) );
}

TEST_CASE( "makeErrorFrame stays legacy byte-compatible without code and carries code when set",
           "[runtime][worker][protocol][r4]" )
{
    const std::string legacy = makeErrorFrame( "job-1", "boom" );
    REQUIRE( legacy.find( '\n' ) == std::string::npos ); // single-line transport
    Json::Value parsed;
    REQUIRE( parseFrame( legacy, parsed ) );
    REQUIRE_FALSE( parsed.isMember( "code" ) ); // legacy workers never see the key
    REQUIRE( parsed["message"].asString() == "boom" );

    const std::string coded = makeErrorFrame( "job-1", "boom", "cancelled" );
    Json::Value parsedCoded;
    REQUIRE( parseFrame( coded, parsedCoded ) );
    REQUIRE( frameErrorCode( parsedCoded ) == "cancelled" );
    REQUIRE( frameErrorMeansCancelled( parsedCoded ) );
}
