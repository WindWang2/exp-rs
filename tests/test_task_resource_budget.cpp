// test_task_resource_budget.cpp — resource-aware scheduler tests (Phase C).
//
// Two layers of coverage:
//   1. Pure unit tests of TaskResourceBudget accounting (deterministic, fast).
//   2. Integration tests through TaskCenter verifying the gate actually delays
//      launch of heavy tasks when the budget is tight, with the never-starve
//      rule and missing-estimate fallback.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/task_resource_budget.h"
#include "processing/framework/task_center.h"
#include "processing/framework/algorithm_engine.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"

#include <QObject>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
void waitForTerminalStatus( sicnu::TaskCenter &center, long taskId,
                            int attempts = 400, int sleepMs = 5 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( sicnu::isTerminalStatus( center.getTaskInfo( taskId ).status ) )
            return;
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
}

sicnu::TaskResourceEstimate makeEstimate( unsigned int mb, sicnu::TaskMemoryClass cls )
{
    sicnu::TaskResourceEstimate e;
    e.ramMb = mb;
    e.memoryClass = cls;
    return e;
}
} // namespace

// ---------------------------------------------------------------------------
// Pure unit tests — TaskResourceBudget accounting (no TaskCenter, no threads).
// ---------------------------------------------------------------------------

TEST_CASE( "TaskResourceBudget: budget 0 disables the gate", "[resource_budget]" )
{
    sicnu::TaskResourceBudget budget;
    budget.setBudgetMb( 0 );
    // Any candidate, any running total → allowed.
    CHECK( budget.canLaunch( 0, 99999, 5 ) );
    CHECK( budget.canLaunch( 50000, 50000, 5 ) );
}

TEST_CASE( "TaskResourceBudget: never-starve — empty profile always launches",
           "[resource_budget]" )
{
    sicnu::TaskResourceBudget budget;
    budget.setBudgetMb( 100 ); // tight budget
    // runningCountInProfile == 0 → always allow, even if candidate alone > cap.
    CHECK( budget.canLaunch( 0, 99999, 0 ) );
    CHECK( budget.canLaunch( 50000, 99999, 0 ) );
}

TEST_CASE( "TaskResourceBudget: projected RAM within cap launches; over holds",
           "[resource_budget]" )
{
    sicnu::TaskResourceBudget budget;
    budget.setBudgetMb( 1000 );
    // running 400 + candidate 500 = 900 ≤ 1000 → launch.
    CHECK( budget.canLaunch( 400, 500, 1 ) );
    // running 600 + candidate 500 = 1100 > 1000 → hold.
    CHECK_FALSE( budget.canLaunch( 600, 500, 1 ) );
    // Exactly at the cap → launch (≤).
    CHECK( budget.canLaunch( 500, 500, 1 ) );
}

TEST_CASE( "TaskResourceBudget: conservative per-class fallback when estimate missing",
           "[resource_budget]" )
{
    using CLS = sicnu::TaskMemoryClass;
    CHECK( sicnu::defaultEstimateMbForClass( CLS::Streaming ) <
           sicnu::defaultEstimateMbForClass( CLS::FullRaster ) );
    CHECK( sicnu::defaultEstimateMbForClass( CLS::MultiPassStreaming ) <
           sicnu::defaultEstimateMbForClass( CLS::FullRaster ) );
    CHECK( sicnu::defaultEstimateMbForClass( CLS::ExternalProcess ) <
           sicnu::defaultEstimateMbForClass( CLS::FullRaster ) );
    // Unknown is treated as the heaviest (safe).
    CHECK( sicnu::defaultEstimateMbForClass( CLS::Unknown ) >=
           sicnu::defaultEstimateMbForClass( CLS::FullRaster ) );

    sicnu::TaskResourceBudget budget;
    // Resolver returns ramMb=0 → resolve applies the class fallback.
    budget.setEstimateResolver(
        []( const std::string & ) { return makeEstimate( 0, CLS::Streaming ); } );
    CHECK( budget.resolve( "any" ).ramMb == sicnu::defaultEstimateMbForClass( CLS::Streaming ) );

    // Unknown class with no estimate → FullRaster-sized fallback.
    budget.setEstimateResolver(
        []( const std::string & ) { return makeEstimate( 0, CLS::Unknown ); } );
    CHECK( budget.resolve( "any" ).ramMb == sicnu::defaultEstimateMbForClass( CLS::Unknown ) );
}

TEST_CASE( "TaskResourceBudget: declared estimate wins over fallback",
           "[resource_budget]" )
{
    sicnu::TaskResourceBudget budget;
    budget.setEstimateResolver(
        []( const std::string &id ) -> sicnu::TaskResourceEstimate {
            if ( id == "rs:pca" )
                return makeEstimate( 4096, sicnu::TaskMemoryClass::FullRaster );
            return {};
        } );
    CHECK( budget.resolve( "rs:pca" ).ramMb == 4096 );
    // Unregistered id → fallback (Unknown = FullRaster-sized default).
    CHECK( budget.resolve( "rs:other" ).ramMb ==
           sicnu::defaultEstimateMbForClass( sicnu::TaskMemoryClass::Unknown ) );
}

// ---------------------------------------------------------------------------
// Integration tests — the gate actually delays launch through TaskCenter.
// ---------------------------------------------------------------------------

TEST_CASE( "TaskCenter resource budget holds a second heavy task until the first finishes",
           "[processing][task_center][resource_budget]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    auto &center = sicnu::TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setGlobalConcurrencyLimit( 8 );           // allow concurrency in principle
    center.setResourceProfileLimit(
        sicnu::ProviderResourceProfile::InProcessThread, 8 );
    // Tight RAM budget: each heavy task claims 500 MB; cap 600 → only one at a time.
    center.setResourceBudgetMb( 600 );
    center.setEstimateResolver(
        []( const std::string & ) { return makeEstimate( 500, sicnu::TaskMemoryClass::FullRaster ); } );

    std::atomic<int> inFlight{ 0 };
    std::atomic<int> maxInFlight{ 0 };
    std::atomic<bool> release{ false };

    engine.registerExecutor(
        "rb_inproc:task",
        [&inFlight, &maxInFlight, &release]( const sicnu::jobs::JobRequest &,
                                             sicnu::operators::RSOperatorContext & ) {
            const int cur = ++inFlight;
            int prev = maxInFlight.load();
            while ( cur > prev && !maxInFlight.compare_exchange_weak( prev, cur ) )
            {
            }
            while ( !release.load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            --inFlight;
            Json::Value result( Json::objectValue );
            result["output"] = "/tmp/rb.tif";
            return result;
        } );

    QList<long> ids;
    for ( int i = 0; i < 3; ++i )
        ids.append( center.enqueueTask( QStringLiteral( "rb_inproc:task" ), {}, false,
                                        sicnu::TaskPriority::Normal, {}, true ) );

    // Give the scheduler a moment to dispatch.
    for ( int attempt = 0; attempt < 200; ++attempt )
    {
        if ( maxInFlight.load() >= 1 )
            break;
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }

    // Snapshot concurrency before release: the budget must hold it to 1.
    int running = 0, queued = 0;
    for ( long id : ids )
    {
        const auto st = center.getTaskInfo( id ).status;
        if ( st == sicnu::TaskStatus::Running )
            ++running;
        else if ( st == sicnu::TaskStatus::Queued )
            ++queued;
    }
    CHECK( running == 1 );
    CHECK( queued == 2 );
    CHECK( maxInFlight.load() == 1 );

    // Release: all three must complete (the held ones launch as each finishes).
    release.store( true );
    engine.waitUntilIdleForTests();
    for ( long id : ids )
        waitForTerminalStatus( center, id );
    for ( long id : ids )
        REQUIRE( center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed );

    // Restore defaults so subsequent tests are unaffected.
    center.setEstimateResolver( {} );
    center.setResourceBudgetMb( center.memoryLimitMb() );
    engine.clearExecutors();
}

TEST_CASE( "TaskCenter resource budget never-starve: a single task launches even if estimate exceeds cap",
           "[processing][task_center][resource_budget]" )
{
    auto &engine = sicnu::jobs::JobEngine::instance();
    engine.shutdownForTests();
    engine.clearExecutors();

    auto &center = sicnu::TaskCenter::instance();
    center.resetResourceProfileLimits();
    center.setGlobalConcurrencyLimit( 8 );
    center.setResourceProfileLimit(
        sicnu::ProviderResourceProfile::InProcessThread, 8 );
    // Absurdly tight budget + huge estimate — but never-starve must still let it run.
    center.setResourceBudgetMb( 1 );
    center.setEstimateResolver(
        []( const std::string & ) { return makeEstimate( 100000, sicnu::TaskMemoryClass::FullRaster ); } );

    std::atomic<bool> release{ false };
    engine.registerExecutor(
        "rb_starve:task",
        [&release]( const sicnu::jobs::JobRequest &,
                    sicnu::operators::RSOperatorContext & ) {
            while ( !release.load() )
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            Json::Value result( Json::objectValue );
            result["output"] = "/tmp/rbs.tif";
            return result;
        } );

    long id = center.enqueueTask( QStringLiteral( "rb_starve:task" ), {}, false,
                                  sicnu::TaskPriority::Normal, {}, true );

    // The task must reach Running despite the estimate far exceeding the cap.
    bool reachedRunning = false;
    for ( int attempt = 0; attempt < 200; ++attempt )
    {
        if ( center.getTaskInfo( id ).status == sicnu::TaskStatus::Running )
        {
            reachedRunning = true;
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
    REQUIRE( reachedRunning );

    release.store( true );
    engine.waitUntilIdleForTests();
    waitForTerminalStatus( center, id );
    REQUIRE( center.getTaskInfo( id ).status == sicnu::TaskStatus::Completed );

    center.setEstimateResolver( {} );
    center.setResourceBudgetMb( center.memoryLimitMb() );
    engine.clearExecutors();
}
