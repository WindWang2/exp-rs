// test_r27_t02_job_engine.cpp — R27-T2 / #1767 (RIO-8) + #1803 (RIO-9/10):
//
//   * shutdown() is deadline-bounded: a worker executing an operator that
//     NEVER polls its cancel flag cannot hang shutdown; the stuck worker is
//     abandoned (detached, immortal engine) and shutdown returns the stuck
//     count. shutdownForTests() then recovers the engine for reuse.
//   * optional per-job wall-clock budget (JobRequest::timeoutMs): the
//     watchdog fires the cancellation path at the deadline; a cooperative
//     operator unwinds to Cancelled.
//   * RIO-10: an executor that RETURNS A RESULT after the cancel flag was
//     armed finishes Succeeded (late cancel recorded), never Cancelled.
#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <functional>
#include <string>
#include <thread>

using namespace sicnu::jobs;

namespace
{
/// Polls until @p pred() is true or @p timeoutMs elapses (convenience for
/// engine-internal transitions the test cannot block on directly).
bool pollUntil( const std::function<bool()> &pred, int timeoutMs )
{
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds( timeoutMs );
    while ( std::chrono::steady_clock::now() < deadline )
    {
        if ( pred() )
            return true;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    return pred();
}

sicnu::operators::RSOperatorError cancelledError( const std::string &msg )
{
    return sicnu::operators::RSOperatorError(
        sicnu::operators::ErrorCode::Cancelled, msg );
}
} // namespace

TEST_CASE( "#1767 wall-clock budget fires the cancel path and a cooperative operator unwinds",
           "[job][r27t02]" )
{
    JobEngine &eng = JobEngine::instance();
    eng.shutdownForTests(); // clean slate for this binary's engine

    JobRequest req;
    req.algorithmId = "r27t02:budget";
    req.title = "budget";
    req.timeoutMs = 150; // watchdog deadline

    std::atomic<bool> sawFlag{ false };
    const std::string id = eng.submit(
        req,
        [&]( const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value
        {
            // Cooperative operator: poll the cancel flag, then unwind with
            // the Cancelled code (the protocol the budget relies on).
            while ( !ctx.cancelFlag() || !ctx.cancelFlag()->load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            sawFlag.store( true );
            throw cancelledError( "budget elapsed" );
        } );
    REQUIRE( !id.empty() );

    REQUIRE( pollUntil( [&]
                        { return sawFlag.load(); }, 5000 ) );
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( id );
                            return snap && snap->state == JobState::Cancelled;
                        },
                        5000 ) );

    eng.shutdownForTests();
}

TEST_CASE( "#1803 RIO-10 late cancel after a returned result stays Succeeded",
           "[job][r27t02]" )
{
    JobEngine &eng = JobEngine::instance();
    eng.shutdownForTests();

    JobRequest req;
    req.algorithmId = "r27t02:late-cancel";
    req.title = "late";

    std::atomic<bool> flagSeen{ false };
    const std::string id = eng.submit(
        req,
        [&]( const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value
        {
            // Wait for a user cancel, then COMPLETE anyway with a result —
            // the finished-work-late-cancel shape RIO-10 fixes.
            while ( !ctx.cancelFlag() || !ctx.cancelFlag()->load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            flagSeen.store( true );
            Json::Value result;
            result["value"] = 42;
            return result;
        } );
    REQUIRE( !id.empty() );

    REQUIRE( pollUntil( [&]
                        { return eng.snapshot( id )->state == JobState::Running; },
                        5000 ) );
    REQUIRE( eng.cancel( id ) );
    REQUIRE( pollUntil( [&]
                        { return flagSeen.load(); }, 5000 ) );
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( id );
                            return snap && snap->state == JobState::Succeeded;
                        },
                        5000 ) );

    const auto snap = eng.snapshot( id );
    REQUIRE( snap->state == JobState::Succeeded );
    REQUIRE( snap->result.isObject() );
    REQUIRE( snap->result["value"].asInt() == 42 );
    // The late cancel is recorded, not silent.
    bool lateNoted = false;
    for ( const auto &line : snap->logLines )
        if ( line.text.find( "cancel arrived after completion" ) != std::string::npos )
            lateNoted = true;
    REQUIRE( lateNoted );

    eng.shutdownForTests();
}

TEST_CASE( "#1767 shutdown is bounded when a worker ignores cancellation; engine recovers",
           "[job][r27t02][shutdown]" )
{
    JobEngine &eng = JobEngine::instance();
    eng.shutdownForTests();
    eng.setShutdownJoinTimeoutMs( 250 );

    // Stuck operator: blocks forever, never polls its cancel flag — the
    // exact RIO-8 shape that used to hang shutdown() for good.
    std::promise<void> neverReleased;
    const std::shared_future<void> gate = neverReleased.get_future().share();
    std::atomic<bool> executorEntered{ false };
    JobRequest req;
    req.algorithmId = "r27t02:stuck";
    req.title = "stuck";
    const std::string id = eng.submit(
        req,
        [&]( const JobRequest &, sicnu::operators::RSOperatorContext & ) -> Json::Value
        {
            executorEntered.store( true );
            gate.wait(); // forever, until the test releases
            return Json::Value();
        } );
    REQUIRE( !id.empty() );
    REQUIRE( pollUntil( [&]
                        { return executorEntered.load(); }, 5000 ) );

    const auto startedAt = std::chrono::steady_clock::now();
    const std::size_t stuck = eng.shutdown();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startedAt )
                                .count();

    // Bounded: the 250 ms deadline (plus arming slack) — never an unbounded
    // join. At least the stuck worker is reported.
    REQUIRE( stuck >= 1 );
    REQUIRE( elapsedMs < 5000 );
    REQUIRE( eng.isTerminated() );

    // Release the abandoned worker so it can unwind against the immortal
    // engine, then recover the engine for reuse (test contract).
    neverReleased.set_value();
    eng.shutdownForTests();
    eng.setShutdownJoinTimeoutMs( 5000 ); // hygiene: later cases keep the default
    REQUIRE_FALSE( eng.isTerminated() );

    // Reuse proof: a fresh job runs to completion after the recovery.
    JobRequest quick;
    quick.algorithmId = "r27t02:after-recovery";
    quick.title = "quick";
    const std::string id2 = eng.submit(
        quick, []( const JobRequest &, sicnu::operators::RSOperatorContext & ) -> Json::Value
        { return Json::Value( 1 ); } );
    REQUIRE( !id2.empty() );
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( id2 );
                            return snap && snap->state == JobState::Succeeded;
                        },
                        5000 ) );

    eng.shutdownForTests();
}
