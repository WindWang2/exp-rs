// test_r33_t04_job_deadline.cpp — R33-T04 / WP01 (#2122):
//
//   m_jobDeadlines is keyed by jobId, so the old watchdog pick of begin()
//   treated the alphabetically-first job as the earliest deadline. A
//   lexicographically earlier LONG-budget job starved a later-armed short
//   budget until its own (late) deadline. These cases pin the real
//   scheduling contract on a real JobEngine with multiple workers:
//
//   * reverse-lexicographic ids: the short budget fires, the long-budget
//     job keeps running (no misfire);
//   * identical deadlines both fire;
//   * a later-armed shorter deadline wakes the parked watchdog;
//   * a normally completed budget job leaves no stale deadline behind.
#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

using namespace sicnu::jobs;

namespace
{
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

/// Cooperative operator that unwinds with Cancelled once the cancel flag is
/// armed — the same protocol the #1767 budget tests rely on.
JobEngine::JobExecutor unwindOnCancel( std::atomic<bool> &sawFlag )
{
    return [ & ]( const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value
    {
        while ( !ctx.cancelFlag() || !ctx.cancelFlag()->load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        sawFlag.store( true );
        throw cancelledError( "budget elapsed" );
    };
}

/// Operator that waits for the cancel flag and then RETURNS a result —
/// the successful-return-wins contract (#1803) that the watchdog must not
/// regress: a long-budget job kept alive past another job's deadline still
/// finishes Succeeded via its own path.
JobEngine::JobExecutor returnAfterCancel( std::atomic<bool> &sawFlag )
{
    return [ & ]( const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) -> Json::Value
    {
        while ( !ctx.cancelFlag() || !ctx.cancelFlag()->load() )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        sawFlag.store( true );
        return Json::Value( 7 );
    };
}
} // namespace

TEST_CASE( "#2122 short-budget job is not starved by lexicographically earlier long-budget job",
           "[jobs][r33t04]" )
{
    JobEngine &eng = JobEngine::instance();
    eng.shutdownForTests();
    eng.setMaxWorkers( 4 );

    // "aaa-long" sorts before "zz-short" in the deadline map's key order —
    // the old begin() pick watched aaa-long's far-future deadline and
    // starved zz-short for the full 8 s budget.
    JobRequest longReq;
    longReq.algorithmId = "r33t04:long";
    longReq.title = "long";
    longReq.timeoutMs = 8000;
    std::atomic<bool> longSawFlag{ false };
    eng.submitWithId( longReq, "aaa-long", returnAfterCancel( longSawFlag ) );

    JobRequest shortReq;
    shortReq.algorithmId = "r33t04:short";
    shortReq.title = "short";
    shortReq.timeoutMs = 250;
    std::atomic<bool> shortSawFlag{ false };
    eng.submitWithId( shortReq, "zz-short", unwindOnCancel( shortSawFlag ) );

    // The short budget must fire well inside the long job's 8 s budget.
    const auto startedAt = std::chrono::steady_clock::now();
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( "zz-short" );
                            return snap && snap->state == JobState::Cancelled;
                        },
                        3000 ) );
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startedAt )
                                .count();
    REQUIRE( elapsedMs < 4000 ); // fired by ITS deadline, not the long one

    // No misfire: the long-budget job is still Running at this point.
    REQUIRE( eng.snapshot( "aaa-long" )->state == JobState::Running );

    // Release the long job through its own path: successful return wins.
    REQUIRE( eng.cancel( "aaa-long" ) );
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( "aaa-long" );
                            return snap && snap->state == JobState::Succeeded;
                        },
                        5000 ) );

    eng.shutdownForTests();
}

TEST_CASE( "#2122 identical deadlines on different jobIds both fire", "[jobs][r33t04]" )
{
    JobEngine &eng = JobEngine::instance();
    eng.shutdownForTests();
    eng.setMaxWorkers( 4 );

    std::atomic<bool> firstSaw{ false };
    std::atomic<bool> secondSaw{ false };
    JobRequest req; // shared shape
    req.algorithmId = "r33t04:same-deadline";
    req.title = "same";
    req.timeoutMs = 250;
    eng.submitWithId( req, "aaa-first", unwindOnCancel( firstSaw ) );
    eng.submitWithId( req, "zzz-second", unwindOnCancel( secondSaw ) );

    REQUIRE( pollUntil( [&]
                        {
                            const auto a = eng.snapshot( "aaa-first" );
                            const auto b = eng.snapshot( "zzz-second" );
                            return a && a->state == JobState::Cancelled && b
                                   && b->state == JobState::Cancelled;
                        },
                        3000 ) );
    REQUIRE( firstSaw.load() );
    REQUIRE( secondSaw.load() );

    eng.shutdownForTests();
}

TEST_CASE( "#2122 later-armed shorter deadline wakes the parked watchdog", "[jobs][r33t04]" )
{
    JobEngine &eng = JobEngine::instance();
    eng.shutdownForTests();
    eng.setMaxWorkers( 4 );

    JobRequest longReq;
    longReq.algorithmId = "r33t04:parked-long";
    longReq.title = "parked";
    longReq.timeoutMs = 8000;
    std::atomic<bool> longSawFlag{ false };
    eng.submitWithId( longReq, "aaa-parked", unwindOnCancel( longSawFlag ) );
    REQUIRE( pollUntil( [&]
                        { return eng.snapshot( "aaa-parked" )->state == JobState::Running; },
                        5000 ) );

    // The watchdog is parked on aaa-parked's far-future deadline when this
    // shorter budget is armed; the arming notification must make it re-park
    // at the new minimum.
    JobRequest quickReq;
    quickReq.algorithmId = "r33t04:quick";
    quickReq.title = "quick";
    quickReq.timeoutMs = 250;
    std::atomic<bool> quickSawFlag{ false };
    eng.submitWithId( quickReq, "mmm-quick", unwindOnCancel( quickSawFlag ) );

    const auto startedAt = std::chrono::steady_clock::now();
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( "mmm-quick" );
                            return snap && snap->state == JobState::Cancelled;
                        },
                        3000 ) );
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - startedAt )
                                .count();
    REQUIRE( elapsedMs < 4000 );
    // The parked long job was not disturbed.
    REQUIRE( eng.snapshot( "aaa-parked" )->state == JobState::Running );
    REQUIRE( eng.cancel( "aaa-parked" ) );
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( "aaa-parked" );
                            return snap && snap->state == JobState::Cancelled;
                        },
                        5000 ) );

    eng.shutdownForTests();
}

TEST_CASE( "#2122 completed budget job leaves no stale deadline blocking the next one",
           "[jobs][r33t04]" )
{
    JobEngine &eng = JobEngine::instance();
    eng.shutdownForTests();
    eng.setMaxWorkers( 4 );

    // A budget job that finishes NORMALLY before its deadline (terminal
    // erase) — then a later-keyed short-budget job must still fire on time.
    JobRequest finisher;
    finisher.algorithmId = "r33t04:finisher";
    finisher.title = "finisher";
    finisher.timeoutMs = 5000;
    eng.submitWithId( finisher, "aaa-finisher", []( const JobRequest &,
                                                    sicnu::operators::RSOperatorContext & ) -> Json::Value
    { return Json::Value( 1 ); } );
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( "aaa-finisher" );
                            return snap && snap->state == JobState::Succeeded;
                        },
                        5000 ) );

    JobRequest nextReq;
    nextReq.algorithmId = "r33t04:next";
    nextReq.title = "next";
    nextReq.timeoutMs = 250;
    std::atomic<bool> nextSawFlag{ false };
    eng.submitWithId( nextReq, "zzz-next", unwindOnCancel( nextSawFlag ) );
    REQUIRE( pollUntil( [&]
                        {
                            const auto snap = eng.snapshot( "zzz-next" );
                            return snap && snap->state == JobState::Cancelled;
                        },
                        3000 ) );
    REQUIRE( nextSawFlag.load() );

    eng.shutdownForTests();
}
