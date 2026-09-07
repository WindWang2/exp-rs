// tests/test_plugin_execution_barrier.cpp — owner-scoped unload barrier (#747)
#include <catch2/catch_test_macros.hpp>

#include "plugins/framework/plugin_execution_barrier.h"

#include <atomic>
#include <chrono>
#include <thread>

using sicnu::plugins::ExecutionLease;
using sicnu::plugins::PluginExecutionBarrier;

// Barrier ids are namespaced per test to avoid cross-test interference with
// the process-wide singleton.
namespace {
const char *kIdA = "org.barrier.test-a";
const char *kIdB = "org.barrier.test-b";
} // namespace

TEST_CASE( "barrier leases track in-flight executions", "[plugin][barrier]" )
{
    auto &barrier = PluginExecutionBarrier::instance();
    REQUIRE( barrier.activeCount( kIdA ) == 0 );
    REQUIRE_FALSE( barrier.isRefusing( kIdA ) );

    {
        auto lease = barrier.acquire( kIdA );
        REQUIRE( lease );
        REQUIRE( barrier.activeCount( kIdA ) == 1 );
        REQUIRE( lease->pluginId() == kIdA );
    }
    REQUIRE( barrier.activeCount( kIdA ) == 0 );

    // Empty ids never produce leases (non-plugin adapters).
    REQUIRE_FALSE( barrier.acquire( "" ) );
}

TEST_CASE( "drain refuses new executions; cancel reopens", "[plugin][barrier]" )
{
    auto &barrier = PluginExecutionBarrier::instance();
    barrier.beginDrain( kIdA );
    REQUIRE( barrier.isRefusing( kIdA ) );
    REQUIRE_FALSE( barrier.acquire( kIdA ) );

    barrier.cancelDrain( kIdA );
    REQUIRE_FALSE( barrier.isRefusing( kIdA ) );
    {
        auto lease = barrier.acquire( kIdA );
        REQUIRE( lease );
    }
}

TEST_CASE( "close is permanent: cancel does not reopen", "[plugin][barrier]" )
{
    auto &barrier = PluginExecutionBarrier::instance();
    barrier.close( kIdB );
    REQUIRE_FALSE( barrier.acquire( kIdB ) );
    barrier.cancelDrain( kIdB );
    REQUIRE_FALSE( barrier.acquire( kIdB ) );
    REQUIRE( barrier.isRefusing( kIdB ) );
}

TEST_CASE( "waitIdle times out while a lease is held elsewhere", "[plugin][barrier]" )
{
    auto &barrier = PluginExecutionBarrier::instance();
    // The execution is already in flight when the unload arms the drain.
    auto lease = barrier.acquire( kIdA );
    REQUIRE( lease );
    barrier.beginDrain( kIdA );

    const bool drained = barrier.waitIdle( kIdA, 50 );
    REQUIRE_FALSE( drained );
    REQUIRE( barrier.activeCount( kIdA ) == 1 );

    // Simulate the execution finishing: unload proceeds.
    lease.reset();
    REQUIRE( barrier.waitIdle( kIdA, 1000 ) );
    barrier.cancelDrain( kIdA );
}

TEST_CASE( "lease release from another thread wakes the waiter", "[plugin][barrier]" )
{
    auto &barrier = PluginExecutionBarrier::instance();
    auto lease = barrier.acquire( kIdA );
    REQUIRE( lease );
    barrier.beginDrain( kIdA );

    // The execution "finishes" on a worker thread: releasing there must
    // wake the draining unload.
    std::thread releaser( [lease = std::move( lease )]() mutable {
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        lease.reset();
    } );
    REQUIRE( barrier.waitIdle( kIdA, 2000 ) );
    releaser.join();
    barrier.cancelDrain( kIdA );
}

TEST_CASE( "move semantics transfer and never double-release", "[plugin][barrier]" )
{
    auto &barrier = PluginExecutionBarrier::instance();
    {
        auto first = barrier.acquire( kIdA );
        REQUIRE( barrier.activeCount( kIdA ) == 1 );
        auto second = std::move( first );
        REQUIRE( barrier.activeCount( kIdA ) == 1 ); // no release on move-from
    }
    // Both gone: exactly one release happened.
    REQUIRE( barrier.activeCount( kIdA ) == 0 );
    barrier.cancelDrain( kIdA );
}
