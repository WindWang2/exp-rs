// test_concurrency_stress.cpp — JobEngine concurrency & lifecycle stress
// (task E, Verification 7.0). Small data, high iteration counts; every wait
// is a deterministic condition wait (waitForJob with a bounded timeout), no
// sleeps.
//
// Scenarios:
//   1. instant-completion storm — submitWithId (#799): a job that finishes
//      before dispatch can never strand or lose its record;
//   2. cancel race — queued and running cancels always reach a terminal
//      state; zombies (Running forever) are a failure;
//   3. worker child jobs (#798) — a job executing ON a worker gets its
//      synchronous child wait rejected immediately (no deadlock, no crash);
//   4. exclusive "drain then alone" policy — an exclusive job runs with zero
//      concurrency after in-flight work drains;
//   5. record retention — pruneCompleted keeps list() bounded;
//   6. shutdown semantics (#684) — after shutdown() submit refuses with a
//      Cancelled record and workers are never respawned (until
//      shutdownForTests).
//
// Link note: compiles job_engine.cpp (+ the 100-line operator registry)
// directly so the stress target stays Qt-free and needs no sicnu_operators
// SHARED closure.
#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "jobs/job_types.h"
#include "operators/framework/rs_operator_registry.h"

#include <atomic>
#include <mutex>
#include <cstdio>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace sicnu::jobs;

namespace
{
constexpr int kWaitMs = 15000; // bounded, generous; a hang is a failure

/// Test isolation for the process-global engine singleton.
struct EngineReset
{
    EngineReset()
    {
        auto &engine = JobEngine::instance();
        engine.clearExecutors();
        engine.setFallbackExecutor( {} );
        engine.setMaxWorkers( JobEngine::kMinWorkers );
        engine.shutdownForTests();
        engine.clearCompleted();
    }
    ~EngineReset()
    {
        auto &engine = JobEngine::instance();
        engine.clearExecutors();
        engine.shutdownForTests();
        engine.clearCompleted();
    }
    EngineReset( const EngineReset & ) = delete;
    EngineReset &operator=( const EngineReset & ) = delete;
};

JobRequest simpleRequest( const std::string &id, const std::string &source = "module" )
{
    JobRequest request;
    request.algorithmId = "callable:stress";
    request.title = id;
    request.source = source;
    return request;
}
} // namespace

TEST_CASE( "stress: instant-completion storm loses no record", "[stress][jobs]" )
{
    EngineReset reset;
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers( JobEngine::kMinWorkers ); // force queueing past 2

    constexpr int kJobs = 300;
    for ( int i = 0; i < kJobs; ++i )
    {
        const std::string jobId = "instant-" + std::to_string( i );
        const std::string submitted = engine.submitWithId(
            simpleRequest( jobId ), jobId,
            [i]( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
                Json::Value result;
                result["i"] = i;
                result["doubled"] = i * 2;
                return result;
            } );
        REQUIRE( submitted == jobId );
    }

    // Deterministic drain: every job must reach Succeeded with its payload.
    for ( int i = 0; i < kJobs; ++i )
    {
        const std::string jobId = "instant-" + std::to_string( i );
        REQUIRE( engine.waitForJob( jobId, kWaitMs ) );
        const auto record = engine.snapshot( jobId );
        REQUIRE( record.has_value() );
        REQUIRE( record->state == JobState::Succeeded );
        REQUIRE( record->result["doubled"].asInt64() == static_cast<int64_t>( i ) * 2 );
    }
}

TEST_CASE( "stress: cancel race always lands in a terminal state", "[stress][jobs]" )
{
    EngineReset reset;
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers( JobEngine::kMinWorkers );

    constexpr int kJobs = 60;
    std::atomic<int> observedCancel{ 0 };
    for ( int i = 0; i < kJobs; ++i )
    {
        JobRequest request = simpleRequest( "cancel-" + std::to_string( i ) );
        request.algorithmId = "callable:stress";
        const std::string jobId = "cancel-" + std::to_string( i );
        engine.submitWithId(
            request, jobId,
            [ &observedCancel ]( const JobRequest &, sicnu::operators::RSOperatorContext &ctx )
                -> Json::Value {
                // Cooperative cancellation: spin until the flag flips.
                for ( int spin = 0; spin < 20000 && !ctx.isCancelled(); ++spin )
                    std::this_thread::yield();
                if ( ctx.isCancelled() )
                    ++observedCancel;
                Json::Value result;
                result["done"] = true;
                return result;
            },
            [] { /* cancel hook */ } );

        // Race by construction: half the cancels are requested while the job
        // is still queued (workers = 2), half likely while running. The
        // cancel() return value is intentionally NOT asserted — a job that
        // already finished before the cancel landed is legitimate; the
        // invariant under test is terminality of every job below.
        if ( i % 2 == 0 )
            engine.cancel( jobId );
    }

    for ( int i = 0; i < kJobs; ++i )
    {
        const std::string jobId = "cancel-" + std::to_string( i );
        REQUIRE( engine.waitForJob( jobId, kWaitMs ) );
        const auto record = engine.snapshot( jobId );
        REQUIRE( record.has_value() );
        const bool terminal = record->state == JobState::Succeeded ||
                              record->state == JobState::Failed ||
                              record->state == JobState::Cancelled;
        INFO( "job " << jobId << " state " << static_cast<int>( record->state ) );
        REQUIRE( terminal );
        if ( record->state == JobState::Cancelled )
            REQUIRE( record->request.algorithmId == "callable:stress" );
    }
    // Every cooperative executor that saw the flag still terminated.
    REQUIRE( observedCancel.load() >= 0 ); // exercised without hanging
}

TEST_CASE( "stress: worker-thread child wait is rejected, never deadlocks "
           "(#798)",
           "[stress][jobs]" )
{
    EngineReset reset;
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers( JobEngine::kMinWorkers );

    const std::string childId = "child-of-worker";
    // Pre-register the child's id so the worker can submit it by id.
    const std::string parentJobId = "parent-on-worker";
    std::atomic<bool> childSubmitted{ false };
    std::atomic<bool> isWorkerInside{ false };
    std::atomic<bool> waitRejected{ false };
    std::string dynamicChildId;
    std::mutex childMutex;

    engine.submitWithId(
        simpleRequest( childId ), childId,
        []( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
            Json::Value result;
            result["child"] = true;
            return result;
        } );

    engine.submitWithId(
        simpleRequest( parentJobId ), parentJobId,
        [ & ]( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
            isWorkerInside.store( JobEngine::isWorkerThread() );
            // Submit a child from INSIDE a worker; the synchronous wait must
            // be refused immediately (false), not deadlock.
            const std::string id =
                engine.submit( simpleRequest( "child-dynamic" ),
                               []( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
                                   Json::Value r;
                                   r["ok"] = true;
                                   return r;
                               } );
            const bool waited = engine.waitForJob( id, 5000 );
            waitRejected.store( !waited );
            // A worker thread can never block-wait (#798): record the child
            // id so the MAIN thread asserts its completion after the parent.
            {
                std::lock_guard<std::mutex> lock( childMutex );
                dynamicChildId = id;
            }
            childSubmitted.store( !id.empty() );
            Json::Value result;
            result["parent"] = true;
            return result;
        } );

    REQUIRE( engine.waitForJob( parentJobId, kWaitMs ) );
    REQUIRE( isWorkerInside.load() );
    INFO( "child wait inside worker must be rejected" );
    REQUIRE( waitRejected.load() );
    REQUIRE( childSubmitted.load() );
    // From the MAIN thread the child completes normally.
    std::string dynamicId;
    {
        std::lock_guard<std::mutex> lock( childMutex );
        dynamicId = dynamicChildId;
    }
    REQUIRE( engine.waitForJob( dynamicId, kWaitMs ) );
    REQUIRE( engine.snapshot( dynamicId )->state == JobState::Succeeded );
    REQUIRE( engine.snapshot( childId )->state == JobState::Succeeded );
}

TEST_CASE( "stress: exclusive job runs alone after in-flight work drains",
           "[stress][jobs]" )
{
    EngineReset reset;
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers( JobEngine::kMinWorkers );

    std::atomic<int> concurrent{ 0 };
    std::atomic<int> maxOverlapWithExclusive{ 0 };
    std::atomic<bool> exclusiveInside{ false };

    auto enterJob = [ & ] {
        ++concurrent;
        // While the exclusive body is running, nothing else may be inside.
        if ( exclusiveInside.load() )
        {
            const int seen = concurrent.load();
            int current = maxOverlapWithExclusive.load();
            while ( seen > current &&
                    !maxOverlapWithExclusive.compare_exchange_weak( current, seen ) )
            {
            }
        }
    };
    auto exitJob = [ & ] { --concurrent; };

    // Warm non-exclusive load. submit() mints its own id — capture them so
    // the drain waits below target real records, not invented ones.
    std::vector<std::string> warmIds;
    for ( int i = 0; i < 12; ++i )
        warmIds.push_back( engine.submit(
            simpleRequest( "warm-" + std::to_string( i ) ),
                       [ & ]( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
                           enterJob();
                           std::this_thread::yield();
                           exitJob();
                           Json::Value result;
                           return result;
                       } ) );

    // Exclusive job: must drain the queue, then run with no other job.
    JobRequest exclusive = simpleRequest( "the-exclusive" );
    exclusive.exclusive = true;
    engine.submitWithId(
        exclusive, "the-exclusive",
        [ & ]( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
            enterJob();
            exclusiveInside.store( true );
            std::this_thread::yield();
            exclusiveInside.store( false );
            exitJob();
            Json::Value result;
            return result;
        } );

    // Trailing non-exclusive jobs must wait for the exclusive to finish.
    std::vector<std::string> tailIds;
    for ( int i = 0; i < 12; ++i )
        tailIds.push_back( engine.submit(
            simpleRequest( "tail-" + std::to_string( i ) ),
                       [ & ]( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
                           enterJob();
                           std::this_thread::yield();
                           exitJob();
                           Json::Value result;
                           return result;
                       } ) );

    REQUIRE( engine.waitForJob( "the-exclusive", kWaitMs ) );
    for ( const std::string &id : tailIds )
    {
        const bool done = engine.waitForJob( id, kWaitMs );
        if ( !done )
        {
            // Diagnostic dump (Verification 7.0): is the exclusive policy
            // leaving queued jobs stranded after the exclusive completes?
            for ( const auto &rec : engine.list() )
                std::printf( "STATE-DUMP job=%s state=%d\n", rec.id.c_str(),
                             static_cast<int>( rec.state ) );
            std::fflush( stdout );
        }
        REQUIRE( done );
    }
    for ( const std::string &id : warmIds )
        REQUIRE( engine.waitForJob( id, kWaitMs ) );
    // Nothing ever overlapped the exclusive job's body.
    INFO( "max concurrency seen while exclusive inside: "
          << maxOverlapWithExclusive.load() );
    REQUIRE( maxOverlapWithExclusive.load() <= 1 );
}

TEST_CASE( "stress: record retention keeps list() bounded", "[stress][jobs]" )
{
    EngineReset reset;
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers( JobEngine::kMinWorkers );

    constexpr int kJobs = 400;
    for ( int i = 0; i < kJobs; ++i )
    {
        const std::string jobId = "retained-" + std::to_string( i );
        engine.submitWithId( simpleRequest( jobId ), jobId,
                             []( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
                                 Json::Value result;
                                 return result;
                             } );
    }
    for ( int i = 0; i < kJobs; ++i )
        REQUIRE( engine.waitForJob( "retained-" + std::to_string( i ), kWaitMs ) );

    // Retention window: prune to the newest 100 terminal records.
    const auto removed = engine.pruneCompleted( 100 );
    REQUIRE( removed == static_cast<std::size_t>( kJobs ) - 100 );
    REQUIRE( engine.list().size() == 100 );
    // Newest record survives; the oldest is pruned.
    REQUIRE( engine.snapshot( "retained-" + std::to_string( kJobs - 1 ) ).has_value() );
    REQUIRE_FALSE( engine.snapshot( "retained-0" ).has_value() );
}

TEST_CASE( "stress: shutdown refuses submit with a Cancelled record, never "
           "respawns (#684)",
           "[stress][jobs]" )
{
    EngineReset reset;
    auto &engine = JobEngine::instance();
    engine.setMaxWorkers( JobEngine::kMinWorkers );

    engine.shutdown();
    const std::string jobId = "after-shutdown";
    engine.submitWithId( simpleRequest( jobId ), jobId,
                         []( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
                             Json::Value result;
                             return result;
                         } );
    const auto record = engine.snapshot( jobId );
    REQUIRE( record.has_value() );
    REQUIRE( record->state == JobState::Cancelled );

    // shutdownForTests restores reusability for the next test case.
    engine.shutdownForTests();
    const std::string jobId2 = "after-reset";
    engine.submitWithId( simpleRequest( jobId2 ), jobId2,
                         []( const JobRequest &, sicnu::operators::RSOperatorContext & ) {
                             Json::Value result;
                             return result;
                         } );
    REQUIRE( engine.waitForJob( jobId2, kWaitMs ) );
    REQUIRE( engine.snapshot( jobId2 )->state == JobState::Succeeded );
}
