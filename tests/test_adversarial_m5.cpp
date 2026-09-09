/***************************************************************************
 * tests/test_adversarial_m5.cpp
 * Adversarial Empirical Stress Suite for Milestone 5
 * Issues: #797, #798, #799, #800
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "app/widgets/histogram_widget.h"
#include "app/widgets/roi_statistics_widget.h"
#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "processing/framework/task_center.h"
#include "data/data_manager.h"

#include <QApplication>
#include <QThreadPool>
#include <QThread>
#include <QVariantMap>

#include "operators/framework/rs_operator_context.h"
#include <json/json.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

using namespace sicnu::jobs;

namespace
{
int fake_argc = 1;
char fake_argv0[] = "test_adversarial_m5";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app && !QCoreApplication::instance() )
        app = new QApplication( fake_argc, fake_argv );
    return app;
}

void waitForTerminalStatus( sicnu::TaskCenter &center, long taskId,
                            int attempts = 200, int sleepMs = 5 )
{
    for ( int i = 0; i < attempts; ++i )
    {
        if ( sicnu::isTerminalStatus( center.getTaskInfo( taskId ).status ) )
            return;
        std::this_thread::sleep_for( std::chrono::milliseconds( sleepMs ) );
    }
}
} // namespace

// ============================================================================
// Case 1: #797 - Dedicated Bounded Analysis Thread Pool & Isolation
// ============================================================================
TEST_CASE( "Adversarial M5 - #797: Dedicated bounded thread pools prevent global starvation",
           "[adversarial][m5][issue-797]" )
{
    ensureApp();

    auto *hPool = HistogramWidget::analysisThreadPool();
    REQUIRE( hPool != nullptr );
    CHECK( hPool->maxThreadCount() == 2 );

    auto *rPool = RoiStatisticsWidget::analysisThreadPool();
    REQUIRE( rPool != nullptr );
    CHECK( rPool->maxThreadCount() == 2 );

    // 1. Concurrency limit stress test: launch 8 tasks onto bounded pool
    std::atomic<int> active{ 0 };
    std::atomic<int> peakActive{ 0 };
    std::atomic<int> totalRun{ 0 };

    for ( int i = 0; i < 8; ++i )
    {
        hPool->start( [&]() {
            int cur = active.fetch_add( 1 ) + 1;
            int prevPeak = peakActive.load();
            while ( cur > prevPeak && !peakActive.compare_exchange_weak( prevPeak, cur ) )
            {
                // loop until updated or higher
            }
            QThread::msleep( 25 );
            active.fetch_sub( 1 );
            totalRun.fetch_add( 1 );
        } );
    }

    hPool->waitForDone();
    CHECK( totalRun.load() == 8 );
    CHECK( peakActive.load() <= 2 );

    // 2. Global thread pool starvation prevention test:
    // Occupy both bounded slots in hPool with longer tasks, then verify
    // QThreadPool::globalInstance() still executes immediately without starvation.
    std::atomic<bool> hPoolSlot1Busy{ true };
    std::atomic<bool> hPoolSlot2Busy{ true };
    hPool->start( [&]() {
        while ( hPoolSlot1Busy.load() )
        {
            QThread::msleep( 5 );
        }
    } );
    hPool->start( [&]() {
        while ( hPoolSlot2Busy.load() )
        {
            QThread::msleep( 5 );
        }
    } );

    // Wait until both slots are active
    QThread::msleep( 20 );

    std::atomic<bool> globalExecuted{ false };
    QThreadPool::globalInstance()->start( [&]() {
        globalExecuted.store( true );
    } );

    // Global pool should execute within 100ms even though hPool is 100% occupied
    bool completedInTime = false;
    for ( int i = 0; i < 20; ++i )
    {
        if ( globalExecuted.load() )
        {
            completedInTime = true;
            break;
        }
        QThread::msleep( 5 );
    }
    CHECK( completedInTime );
    CHECK( globalExecuted.load() );

    // Release hPool slots
    hPoolSlot1Busy.store( false );
    hPoolSlot2Busy.store( false );
    hPool->waitForDone();
}

// ============================================================================
// Case 2: #798 - Worker Thread Deadlock Prevention and waitForJob
// ============================================================================
TEST_CASE( "Adversarial M5 - #798: Worker thread deadlock prevention and waitForJob",
           "[adversarial][m5][issue-798]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();

    // Calling thread is not a worker thread
    CHECK_FALSE( JobEngine::isWorkerThread() );

    std::atomic<bool> workerThreadDetected{ false };
    std::atomic<bool> childWaitRejected{ false };
    std::atomic<bool> idleWaitRejected{ false };

    JobRequest req;
    req.algorithmId = "callable:adversarial_worker";
    req.clientTag = "test:m5_798";
    auto executor = [&]( const JobRequest &, sicnu::operators::RSOperatorContext & ) -> Json::Value {
        workerThreadDetected.store( JobEngine::isWorkerThread() );

        // Synchronous child wait on worker thread MUST be rejected to prevent pool deadlock (#798)
        auto tStart = std::chrono::steady_clock::now();
        bool waitResult = JobEngine::instance().waitForJob( "non_existent_child", 500 );
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tStart ).count();

        // Must reject immediately without waiting the full 500ms timeout
        if ( !waitResult && elapsed < 200 )
            childWaitRejected.store( true );

        // waitUntilIdleForTests on worker thread MUST also return immediately
        auto tStart2 = std::chrono::steady_clock::now();
        JobEngine::instance().waitUntilIdleForTests( 500 );
        auto elapsed2 = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - tStart2 ).count();
        if ( elapsed2 < 200 )
            idleWaitRejected.store( true );

        return Json::Value( Json::objectValue );
    };

    std::string jobId = engine.submit( req, executor );
    REQUIRE_FALSE( jobId.empty() );

    // Calling waitForJob from non-worker (main test thread) is permitted and should succeed
    bool ok = engine.waitForJob( jobId, 5000 );
    CHECK( ok );
    CHECK( workerThreadDetected.load() );
    CHECK( childWaitRejected.load() );
    CHECK( idleWaitRejected.load() );

    // Main thread calling waitForJob for non-existent job times out cleanly
    CHECK_FALSE( engine.waitForJob( "non_existent_job_xyz", 30 ) );
}

// ============================================================================
// Case 3: #799 - TaskCenter Rapid Completion & Race-Free ClientTag Recovery
// ============================================================================
TEST_CASE( "Adversarial M5 - #799: TaskCenter rapid completion and clientTag recovery",
           "[adversarial][m5][issue-799]" )
{
    ensureApp();
    auto &engine = JobEngine::instance();
    auto &center = sicnu::TaskCenter::instance();

    engine.registerExecutor( "test:rapid_algo", []( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
        Json::Value res( Json::objectValue );
        res["status"] = "ok";
        return res;
    } );

    std::vector<long> taskIds;
    const int NUM_TASKS = 15;

    for ( int i = 0; i < NUM_TASKS; ++i )
    {
        JobRequest req;
        req.algorithmId = "test:rapid_algo";
        req.source = "task_panel";
        long id = center.submitJob( req );
        REQUIRE( id > 0 );
        taskIds.push_back( id );
    }

    engine.waitUntilIdleForTests();

    for ( long id : taskIds )
    {
        waitForTerminalStatus( center, id, 300, 5 );
        auto info = center.getTaskInfo( id );
        CHECK( sicnu::isTerminalStatus( info.status ) );
        CHECK( info.status == sicnu::TaskStatus::Completed );
    }

    engine.clearExecutors();
}

// ============================================================================
// Case 4: #800 - DataManager Const Accessor Thread Affinity Assertions
// ============================================================================
TEST_CASE( "Adversarial M5 - #800: DataManager const accessor thread affinity",
           "[adversarial][m5][issue-800]" )
{
    ensureApp();
    sicnu::data::DataManager dm;

    // Invariant: DataManager thread matches current thread
    CHECK( dm.thread() == QThread::currentThread() );

    // Calling all 19 const accessors on owning thread succeeds seamlessly
    CHECK( dm.assets().isEmpty() );
    CHECK( !dm.asset( sicnu::data::AssetId::generate() ).has_value() );
    CHECK( !dm.findByPath( QStringLiteral( "/nonexistent/test/path" ) ).has_value() );
    CHECK( dm.catalogGeneration() >= 1 );
    CHECK_FALSE( dm.provenance( sicnu::data::AssetId::generate() ).has_value() );
    CHECK( dm.derivedFrom( sicnu::data::AssetId::generate() ).isEmpty() );
    CHECK( dm.derivedOutputsOf( sicnu::data::AssetId::generate() ).isEmpty() );
    CHECK( dm.derivedOutputsOfCollection( sicnu::data::CollectionId::generate() ).isEmpty() );
    CHECK( dm.leaseCount( sicnu::data::AssetId::generate() ) == 0 );
    CHECK( dm.leases( sicnu::data::AssetId::generate() ).isEmpty() );
    CHECK_FALSE( dm.hasActiveEditLease( sicnu::data::AssetId::generate() ) );
    auto plan = dm.planUnload( sicnu::data::AssetId::generate() );
    CHECK( plan.strongDependents().isEmpty() );
    CHECK( dm.strongDependenciesOf( sicnu::data::AssetId::generate() ).isEmpty() );
    CHECK( dm.strongDependentsOf( sicnu::data::AssetId::generate() ).isEmpty() );
    CHECK_FALSE( dm.virtualRasterRecipe( sicnu::data::AssetId::generate() ).has_value() );
    CHECK_FALSE( dm.collection( sicnu::data::CollectionId::generate() ).has_value() );
    CHECK( dm.collections().isEmpty() );
    CHECK_FALSE( dm.temporalCollection( sicnu::data::CollectionId::generate() ).has_value() );
    CHECK( dm.temporalCollections().isEmpty() );

    // Worker thread detection
    auto fut = std::async( std::launch::async, [&dm]() {
        return QThread::currentThread() != dm.thread();
    } );
    CHECK( fut.get() );
}
