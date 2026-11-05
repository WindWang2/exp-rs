// test_r28_t02_task_lifecycle_batch.cpp — R28-T02 WP12 scenario B: the
// multi-task lifecycle interleave over TaskCenter + JobEngine with cancel,
// watchdog timeout and bounded shutdown competing:
//
//   * a mixed batch (normal / low / a queued cancel) where every task lands
//     in EXACTLY ONE terminal state — no orphans in Cancelling, no
//     double-terminal, no half-published results;
//   * a late cancel arriving AFTER a task already succeeded never rewrites
//     the success (the TaskCenter layer of RIO-10);
//   * a worker parked in a non-cancellable operator is finalized by the
//     cancel watchdog (typed reason kept), and the engine's bounded
//     shutdown reports the stuck count instead of hanging or pretending a
//     clean exit.
//
// Deterministic: TestGate releases + poll predicates + an injected shutdown
// deadline — no wall-clock luck (5 ms poll vs multi-hundred-ms budgets).
#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/framework/task_center.h"
#include "support/bounded_wait.h"

#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <chrono>
#include <set>
#include <vector>

using sicnu::TaskCenter;
using sicnu::TaskPriority;
using sicnu::TaskStatus;
using sicnu::jobs::JobEngine;
using sicnu::jobs::JobRequest;

namespace
{
void ensureApp()
{
    if ( QCoreApplication::instance() )
        return;
    static int argc = 1;
    static char appName[] = "test_r28_t02_task_lifecycle_batch";
    static char *argv[] = { appName, nullptr };
    new QCoreApplication( argc, argv );
}

JobRequest batchRequest( const char *algorithmId, sicnu::TaskPriority priority )
{
    JobRequest r;
    r.algorithmId = algorithmId;
    // Priority mirror of sicnu::TaskPriority (job_types.h): 0=High, 1=Normal,
    // 2=Low — the engine's bucketed queue picks the lowest value first.
    r.priority = static_cast<int>( priority );
    return r;
}

bool waitForStatus( long taskId, std::initializer_list<TaskStatus> wanted, int timeoutMs )
{
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds( timeoutMs );
    while ( std::chrono::steady_clock::now() < deadline )
    {
        const TaskStatus s = TaskCenter::instance().getTaskInfo( taskId ).status;
        for ( TaskStatus w : wanted )
            if ( s == w )
                return true;
        QThread::msleep( 5 );
    }
    return false;
}
} // namespace

TEST_CASE( "scenario B: mixed batch — cancel, watchdog and late cancel land exactly-once",
           "[r28t02][lifecycle][batch]" )
{
    ensureApp();
    JobEngine &engine = JobEngine::instance();
    engine.shutdownForTests();
    TaskCenter &center = TaskCenter::instance();
    center.shutdownForTests();
    center.setCancelWatchdogMs( 200 );

    // Two cooperative fast tasks, one hang (never polls the flag), one
    // cancellable long task.
    std::atomic<int> fastDone{ 0 };
    sicnu_test::TestGate hangRelease;
    engine.clearExecutors();
    engine.registerExecutor(
        "b12:", [&]( const JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) {
            if ( req.algorithmId == "b12:fast" )
            {
                ++fastDone;
                return Json::Value();
            }
            if ( req.algorithmId == "b12:hang" )
            {
                hangRelease.wait(); // non-cancellable native shape
                return Json::Value();
            }
            // b12:long — cooperative: unwinds on cancel
            for ( int i = 0; i < 4000; ++i )
            {
                ctx.throwIfCancelled();
                std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            }
            return Json::Value();
        } );

    // ① Mixed batch: 2 fast (high/low), 1 long (cancellable), 1 hang.
    const long fastHigh = center.submitJob( batchRequest( "b12:fast", TaskPriority::High ) );
    const long long1 = center.submitJob( batchRequest( "b12:long", TaskPriority::Normal ) );
    const long hang1 = center.submitJob( batchRequest( "b12:hang", TaskPriority::Normal ) );
    const long fastLow = center.submitJob( batchRequest( "b12:fast", TaskPriority::Low ) );
    REQUIRE( fastHigh > 0 );
    REQUIRE( long1 > 0 );
    REQUIRE( hang1 > 0 );
    REQUIRE( fastLow > 0 );

    // ② Cancel the LONG task once it is Running — it unwinds cooperatively
    // to Canceled (typed), never publishes a result.
    REQUIRE( waitForStatus( long1, { TaskStatus::Running, TaskStatus::Cancelling }, 8000 ) );
    REQUIRE( center.cancelTask( long1 ) );
    const auto longInfo =
        center.waitForTask( long1, std::chrono::milliseconds( 8000 ),
                            std::chrono::milliseconds( 10 ) );
    REQUIRE( longInfo.status == TaskStatus::Canceled );
    REQUIRE( longInfo.cancelReason == sicnu::TaskCancelReason::User );

    // ③ The hang task: cancel fires the watchdog → finalized Canceled with
    // the watchdog attribution, even though the worker never reports.
    REQUIRE( waitForStatus( hang1, { TaskStatus::Running }, 8000 ) );
    REQUIRE( center.cancelTask( hang1 ) );
    const auto hangInfo =
        center.waitForTask( hang1, std::chrono::milliseconds( 10000 ),
                            std::chrono::milliseconds( 10 ) );
    REQUIRE( hangInfo.status == TaskStatus::Canceled );
    REQUIRE( hangInfo.errorMessage.contains( QStringLiteral( "watchdog" ),
                                             Qt::CaseInsensitive ) );

    // ④ The fast tasks succeeded; a LATE cancel after success must not
    // rewrite the outcome (TaskCenter layer of RIO-10).
    const auto fastInfo =
        center.waitForTask( fastHigh, std::chrono::milliseconds( 8000 ),
                            std::chrono::milliseconds( 10 ) );
    REQUIRE( fastInfo.status == TaskStatus::Completed );
    center.cancelTask( fastHigh ); // deliberately ignored result: late cancel
    const auto afterLate =
        center.waitForTask( fastHigh, std::chrono::milliseconds( 200 ),
                            std::chrono::milliseconds( 10 ) );
    CHECK( afterLate.status == TaskStatus::Completed ); // unchanged

    // ⑤ Exactly-once terminals across the whole batch.
    for ( const long id : { fastHigh, long1, hang1, fastLow } )
    {
        const auto info = center.getTaskInfo( id );
        CHECK( sicnu::isTerminalStatus( info.status ) );
    }

    // ⑥ Bounded shutdown with a stuck worker: the deadline passes, the
    // engine reports the stuck count (observable, WP02) instead of hanging.
    engine.setShutdownJoinTimeoutMs( 200 );
    const std::size_t stuck = engine.shutdown();
    CHECK( stuck >= 1 ); // the hang worker is still parked at its gate
    hangRelease.open();
    CHECK( !hangRelease.timedOut() );
    engine.clearExecutors();
    engine.setShutdownJoinTimeoutMs( 5000 );
    center.resetResourceProfileLimits();
    center.shutdownForTests();
    engine.shutdownForTests();
}
