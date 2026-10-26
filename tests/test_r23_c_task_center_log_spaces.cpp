// tests/test_r23_c_task_center_log_spaces.cpp — r23-C (C05) regression.
//
// Pins the TaskCenter log-forwarding consumer's index-space contract from
// JobRecord::logLinesOffset (jobs/job_types.h): consumers must compare and
// store ABSOLUTE appended-line indices (droppedLogLines + local index), never
// the retained window's size. R22-agent REVIEW_R2 R2-F3/R2-F6 found that the
// cumulative branch of TaskCenter::processJobRecord compares
// record.logLines.size() — the RETAINED window after #1705 block eviction —
// against m_forwardedLogCounts (absolute), so once a chatty job crossed the
// 10k-line cap:
//   * an engine-internal appendLog + full-record notify (shutdown/queued
//     cancel) can no longer deliver its line (guard size > forwarded is
//     false because retained < absolute forwarded) — the line is LOST;
//   * a stale progress-cumulative delivered after newer deltas (an operator
//     logging from >= 2 threads, the pattern #1693 acknowledges) mis-slices
//     the window and then SKIPS or DUPLICATES the late deltas.
//
// The engine listener is a single replacing slot that delivers synchronously
// in call order, so the out-of-order interleaving cannot be produced through
// the public surface deterministically; TaskCenter::deliverJobRecordForTests
// replays engine-shaped records into the real consumer with a controlled
// order. Every record shape below is field-for-field what a real producer
// emits (progress-callback cumulative copy: logLinesOffset == 0 with the
// retained window + droppedLogLines bookkeeping; log-callback delta:
// logLinesOffset > 0; engine-internal append: full record with offset 0).
// Assertions count whole logBuffer lines, so they only reference lines the
// per-task ring (#1454, newest 2000) still retains.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/task_center.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <future>
#include <string>
#include <thread>

using namespace sicnu::jobs;

namespace
{

void ensureApp()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char appName[] = "test_r23_c_task_center_log_spaces";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
}

struct SingletonReset
{
    SingletonReset()
    {
        ensureApp();
        sicnu::TaskCenter::instance().shutdownForTests();
        JobEngine::instance().shutdownForTests();
        JobEngine::instance().clearExecutors();
        JobEngine::instance().setMaxWorkers( 1 );
    }
    ~SingletonReset()
    {
        JobEngine::instance().setListener( nullptr );
        JobEngine::instance().clearExecutors();
        JobEngine::instance().setFallbackExecutor( {} );
        sicnu::TaskCenter::instance().shutdownForTests();
    }
};

// Executor that parks until the test releases it, so the mapped task stays
// Running (non-terminal records keep flowing through processJobRecord) while
// records are replayed deterministically from the test thread. The promise
// fires from the destructor too, so a failed assertion cannot strand the
// worker.
struct ParkedRelease
{
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    ~ParkedRelease()
    {
        // Fires the park release even when an assertion aborts the case; a
        // double fire (happy path released it already) is swallowed — a
        // throwing destructor would terminate the worker join instead.
        try { release.set_value(); } catch ( ... ) {}
    }
};

int countLine( const sicnu::TaskCenter &center, long taskId, const std::string &needle )
{
    const auto info = center.getTaskInfo( taskId );
    const QString wanted = QString::fromStdString( needle );
    return static_cast<int>( std::count( info.logBuffer.cbegin(), info.logBuffer.cend(), wanted ) );
}

// Poll until the task exposes the engine jobId (set at launch time) — the
// replayed records must carry the real id so onJobRecord maps them.
std::string waitJobId( sicnu::TaskCenter &center, long taskId )
{
    for ( int i = 0; i < 2000; ++i )
    {
        const std::string jobId = center.getTaskInfo( taskId ).jobId;
        if ( !jobId.empty() )
            return jobId;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }
    return center.getTaskInfo( taskId ).jobId;
}

void waitStatus( sicnu::TaskCenter &center, long taskId, sicnu::TaskStatus status )
{
    for ( int i = 0; i < 3000; ++i )
    {
        if ( center.getTaskInfo( taskId ).status == status )
            return;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }
}

} // namespace

TEST_CASE( "engine-internal cumulative appends still deliver after eviction (#1705 absolute-space, R2-F6)",
           "[processing][task_center][r23c]" )
{
    SingletonReset reset;
    auto &engine = JobEngine::instance();
    auto &center = sicnu::TaskCenter::instance();

    ParkedRelease parked;
    constexpr int kChattyLines = 12'000; // crosses the 10k cap once: dropped > 0
    // The worker captures the shared future BY VALUE (r22 precedent in
    // test_r22_agent_job_logs.cpp): the lambda outlives the test scope, and
    // a failed assertion must not destroy the future under a parked worker.
    engine.registerExecutor( "r23c:parked-chatty",
                             [released = parked.released, kChattyLines](
                               const JobRequest &, sicnu::operators::RSOperatorContext &ctx ) {
        for ( int i = 0; i < kChattyLines; ++i )
            ctx.logInfo( "abs " + std::to_string( i ) );
        released.wait();
        Json::Value r( Json::objectValue );
        r["output"] = "parked-done";
        return r;
    } );

    JobRequest request;
    request.algorithmId = "r23c:parked-chatty";
    request.source = "task_panel";
    const long taskId = center.submitJob( request );
    REQUIRE( taskId > 0 );

    waitStatus( center, taskId, sicnu::TaskStatus::Running );
    const std::string jobId = waitJobId( center, taskId );
    REQUIRE( !jobId.empty() );

    // Wait until every delta has been forwarded (the last chatty line lands
    // in the task log while the task is still Running — same determinism
    // pattern as the #638 delta test in test_task_center.cpp).
    for ( int i = 0; i < 5000; ++i )
    {
        if ( countLine( center, taskId, "abs 11999" ) == 1 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    }
    REQUIRE( countLine( center, taskId, "abs 11999" ) == 1 );
    // Sanity: eviction really happened — the authoritative record keeps the
    // retained window plus drop accounting, and is shipped cumulative.
    const auto snap = engine.snapshot( jobId );
    REQUIRE( snap.has_value() );
    REQUIRE( snap->droppedLogLines > 0 );
    REQUIRE( snap->logLinesOffset == 0 );

    // Engine-internal append shape (the shutdown / cancelled-while-queued
    // paths appendLog into the record and notify the FULL record with
    // logLinesOffset == 0): replay a real snapshot copy carrying one more
    // appended line. m_forwardedLogCounts already holds the ABSOLUTE total
    // (1 Started + 12000 lines), which is larger than the retained window —
    // exactly the R2-F6 shape.
    JobRecord internal = *snap;
    internal.state = JobState::Running; // keep the task non-terminal
    JobLogLine tail;
    tail.text = "engine internal tail";
    internal.logLines.push_back( tail );
    center.deliverJobRecordForTests( internal );

    // The appended line must land exactly once. RED behavior: the cumulative
    // guard compares the RETAINED size (7001) against the ABSOLUTE forwarded
    // count (12001), the guard is false, and the line is never delivered.
    REQUIRE( countLine( center, taskId, "engine internal tail" ) == 1 );
    // Exactly-once: the most recent chatty line was not re-delivered.
    REQUIRE( countLine( center, taskId, "abs 11999" ) == 1 );

    parked.release.set_value();
    engine.waitUntilIdleForTests( 15000 );
    waitStatus( center, taskId, sicnu::TaskStatus::Completed );
}

TEST_CASE( "stale cumulative plus late deltas stay exactly-once under eviction (R2-F3)",
           "[processing][task_center][r23c]" )
{
    SingletonReset reset;
    auto &engine = JobEngine::instance();
    auto &center = sicnu::TaskCenter::instance();

    // Silent operator: the only real line is the engine's own Started line,
    // so the synthetic absolute index space starts at a known base. Volumes
    // stay far below the task-log ring's trim threshold (#1454) so whole-line
    // counting is exact.
    ParkedRelease parked;
    engine.registerExecutor( "r23c:parked-silent",
                             [released = parked.released](
                               const JobRequest &, sicnu::operators::RSOperatorContext & ) {
        released.wait();
        Json::Value r( Json::objectValue );
        r["output"] = "silent-done";
        return r;
    } );

    JobRequest request;
    request.algorithmId = "r23c:parked-silent";
    request.source = "task_panel";
    const long taskId = center.submitJob( request );
    REQUIRE( taskId > 0 );

    waitStatus( center, taskId, sicnu::TaskStatus::Running );
    const std::string jobId = waitJobId( center, taskId );
    REQUIRE( !jobId.empty() );
    // Started line (absolute index 0) forwarded by the Running record.
    for ( int i = 0; i < 2000 && center.getTaskInfo( taskId ).logBuffer.isEmpty(); ++i )
        std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
    const std::size_t base = static_cast<std::size_t>( center.getTaskInfo( taskId ).logBuffer.size() );
    REQUIRE( base >= 1 );

    const auto makeLine = []( std::size_t absoluteIndex ) {
        JobLogLine line;
        line.text = "B " + std::to_string( absoluteIndex );
        return line;
    };
    const auto bLine = []( std::size_t absoluteIndex ) {
        return "B " + std::to_string( absoluteIndex );
    };

    // 1) Delta record as the log callback ships it: lines for absolute
    //    [base, base+600) delivered in order. Forwarded count = base+600.
    constexpr std::size_t kDelta1 = 600;
    constexpr std::size_t kRetained = 700;
    JobRecord d1;
    d1.id = jobId;
    d1.state = JobState::Running;
    d1.logLinesOffset = base + 1;
    for ( std::size_t i = 0; i < kDelta1; ++i )
        d1.logLines.push_back( makeLine( base + i ) );
    center.deliverJobRecordForTests( d1 );
    REQUIRE( countLine( center, taskId, bLine( base + kDelta1 - 1 ) ) == 1 );

    const std::size_t forwarded = base + kDelta1;        // absolute count seen
    const std::size_t dropped = base + 502;              // eviction bookkeeping
    const std::size_t absTotal = dropped + kRetained;    // window: [dropped, absTotal)

    // 2) STALE cumulative record (progress-callback shape) delivered LATE by
    //    a second producer thread: retained window = absolute
    //    [dropped, absTotal). Its absolute total is beyond `forwarded`, so
    //    the not-yet-forwarded absolute tail [forwarded, absTotal) must land
    //    — nothing skipped, nothing extra. RED behavior: the guard slices
    //    logLines[forwarded..] of the RETAINED window, forwarding the wrong
    //    (later) slice and losing absolute [forwarded, dropped+forwarded).
    JobRecord staleCumulative;
    staleCumulative.id = jobId;
    staleCumulative.state = JobState::Running;
    staleCumulative.droppedLogLines = dropped;
    for ( std::size_t i = 0; i < kRetained; ++i )
        staleCumulative.logLines.push_back( makeLine( dropped + i ) );
    center.deliverJobRecordForTests( staleCumulative );

    REQUIRE( countLine( center, taskId, bLine( forwarded ) ) == 1 );   // slice starts here
    REQUIRE( countLine( center, taskId, bLine( absTotal - 1 ) ) == 1 ); // slice ends here
    REQUIRE( static_cast<std::size_t>( center.getTaskInfo( taskId ).logBuffer.size() ) == absTotal );

    // 3) The deltas that were already emitted but only NOW delivered (the
    //    reordering the single synchronous listener cannot show, but a
    //    multi-threaded operator produces): absolute [dropped, absTotal)
    //    again. Every already-seen absolute index must be skipped.
    JobRecord lateDelta;
    lateDelta.id = jobId;
    lateDelta.state = JobState::Running;
    lateDelta.logLinesOffset = dropped + 1;
    for ( std::size_t i = 0; i < kRetained; ++i )
        lateDelta.logLines.push_back( makeLine( dropped + i ) );
    center.deliverJobRecordForTests( lateDelta );

    // Exactly-once: the slice boundary lines are single, and a mid-window
    // line is not duplicated. RED behavior: the corrupted (retained-size)
    // forwarded count lets the late delta re-deliver the upper half — the
    // overlap region is absolute [dropped + forwarded, absTotal).
    REQUIRE( countLine( center, taskId, bLine( forwarded ) ) == 1 );
    REQUIRE( countLine( center, taskId, bLine( dropped + forwarded ) ) == 1 );
    REQUIRE( countLine( center, taskId, bLine( absTotal - 1 ) ) == 1 );
    REQUIRE( static_cast<std::size_t>( center.getTaskInfo( taskId ).logBuffer.size() ) == absTotal );

    // 4) Replaying the same cumulative record changes nothing (idempotent
    //    against duplicate delivery).
    center.deliverJobRecordForTests( staleCumulative );
    REQUIRE( static_cast<std::size_t>( center.getTaskInfo( taskId ).logBuffer.size() ) == absTotal );
    REQUIRE( countLine( center, taskId, bLine( dropped + forwarded ) ) == 1 );

    parked.release.set_value();
    engine.waitUntilIdleForTests( 15000 );
    waitStatus( center, taskId, sicnu::TaskStatus::Completed );
}
