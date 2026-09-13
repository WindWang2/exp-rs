// test_large_scale_execution_10.cpp — LSEE 10.0 scale & failure-matrix
// evidence (ADR 0148 §7): the assertions prove COMPLEXITY and BOUNDED
// STATE, never wall-clock:
//   - wide fan-out joins drain with peak in-flight tiles bounded by the
//     planner's formula (stages+1)*queueCap*inputs (+1 consumer tile);
//   - concurrent scratch acquisition never exceeds the budget and ends
//     fully unaccounted;
//   - a mid-stream tile checkpoint restarts a simulated crash, and refuses
//     (fail-closed) on identity/parameter drift;
//   - the execution cache stays correct at thousands of entries (hit,
//     miss, self-heal);
//   - a poison task (worker crashes on every attempt) converges to a
//     terminal Failed after exactly the bounded retry budget — the pool is
//     never respawned into an unbounded loop.
// Heavy variants run under SICNU_LSEE10_STRESS=1 (10^6 logical tiles).
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>

#include "data/execution_fingerprint.h"
#include "jobs/job_engine.h"
#include "processing/framework/task_center.h"
#include "runtime/chunk/chunk_graph.h"
#include "runtime/chunk/memory_planner.h"
#include "runtime/chunk/scratch_registry.h"
#include "runtime/chunk/tile_checkpoint.h"

using namespace sicnu::runtime::chunk;

namespace
{

bool stressEnabled()
{
    static const bool enabled = [] {
        const char *env = std::getenv( "SICNU_LSEE10_STRESS" );
        return env && env[0] && env != std::string( "0" );
    }();
    return enabled;
}

/// RAII test isolation for the JobEngine/TaskCenter singletons (mirrors the
/// ep9 harness conventions).
struct EngineGuard10
{
    EngineGuard10()
    {
        if ( !QCoreApplication::instance() )
        {
            static int argc = 1;
            static char appName[] = "test_large_scale_execution_10";
            static char *argv[] = { appName, nullptr };
            new QCoreApplication( argc, argv );
        }
        auto &engine = sicnu::jobs::JobEngine::instance();
        engine.shutdownForTests();
        engine.clearExecutors();
        engine.setMaxWorkers( 2 );
    }
    ~EngineGuard10() { sicnu::jobs::JobEngine::instance().shutdownForTests(); }
};

TilePayload indexedPayload( int index, int total )
{
    TileSpec spec;
    spec.index = index;
    spec.totalTiles = total;
    spec.width = 4;
    spec.height = 4;
    spec.bufferWidth = 4;
    spec.bufferHeight = 4;
    spec.bands = 1;
    spec.rasterWidth = 4 * total;
    spec.rasterHeight = 4;
    auto buffer = std::make_shared<std::vector<float>>( spec.bufferElementCount(),
                                                       static_cast<float>( index ) );
    return TilePayload{ spec, std::move( buffer ) };
}

} // namespace

TEST_CASE( "Wide fan-out join drains 10^6 logical tiles with bounded in-flight memory",
           "[lsee10][scale][graph]" )
{
    const int totalTiles = stressEnabled() ? 1000000 : 20000;
    const int sourceCount = 4;
    constexpr std::size_t queueCap = 2;
    constexpr int stages = 1; // the join counts as the one stage

    ChunkGraph::Config config;
    config.queueCapacity = queueCap;
    ChunkGraph graph( config );

    // Live-tile accounting: incremented when a source hands a tile to its
    // queue, decremented when the sink consumes it. The bounded-queue
    // contract says this can never exceed (stages+1)*cap*inputs + 1.
    std::atomic<int> live{ 0 };
    std::atomic<int> peak{ 0 };
    auto trackSource = [&]( int /*id*/ ) {
        auto next = std::make_shared<std::atomic<int>>( 0 );
        return [&, next]( TilePayload &out ) {
            const int index = next->fetch_add( 1 );
            if ( index >= totalTiles )
                return false;
            out = indexedPayload( index, totalTiles );
            const int now = live.fetch_add( 1 ) + 1;
            int expected = peak.load();
            while ( now > expected && !peak.compare_exchange_weak( expected, now ) )
            {
            }
            return true;
        };
    };

    std::vector<ChunkGraph::NodeId> sources;
    for ( int i = 0; i < sourceCount; ++i )
        sources.push_back( graph.addSource( trackSource( i ) ) );
    auto join = graph.addJoin( sources, [&]( std::vector<TilePayload> &&tiles ) {
        // Deterministic tuple: all four inputs carry the same index.
        TileSpec spec = tiles.front().spec;
        const float index = tiles[0].pixels->at( 0 );
        for ( const auto &tile : tiles )
            if ( tile.pixels->at( 0 ) != index )
                throw std::runtime_error( "join tuple misaligned" );
        // The dropped inputs' payloads die HERE: the live counter follows
        // payload lifetime, not production count.
        live.fetch_sub( sourceCount - 1 );
        return TilePayload{ spec, tiles[0].pixels };
    } );
    std::atomic<int> consumed{ 0 };
    graph.addSink( join, [&]( TilePayload && ) {
        --live;
        ++consumed;
        return true; // run to normal EOF; cancellation is a separate test
    } );

    graph.run();
    REQUIRE( consumed == totalTiles );
    REQUIRE( graph.completedTiles() == static_cast<std::size_t>( totalTiles ) );

    // Precise bound for THIS counter's accounting: a tile is live from the
    // producer's hands (before push) until the sink consumes it, so
    //   producers' hands + input queues + join tuple + output queue + sink.
    // The planner's stream bound (stages+1)*cap*inputs + 1 = 17 covers the
    // queue/node-held subset; the +sourceCount is the pre-push hands.
    const int bound = sourceCount                                   /*producer hands*/
                      + sourceCount * static_cast<int>( queueCap )  /*input queues*/
                      + sourceCount                                 /*join tuple*/
                      + static_cast<int>( queueCap )                /*join output queue*/
                      + 1;                                          /*sink tile*/
    REQUIRE( peak.load() <= bound );
}

TEST_CASE( "Concurrent scratch acquisition never exceeds the budget and ends unaccounted",
           "[lsee10][scale][scratch]" )
{
    ScratchRegistry registry( { ( std::filesystem::temp_directory_path() / "lsee10-storm" ).string(),
                                 /*budgetBytes=*/4096 } );
    // Deterministically saturate the budget with 8 leases of 512 B: every
    // storm acquire below must refuse (8 × 512 == budget, nothing left).
    std::vector<ScratchLease> saturation;
    for ( int i = 0; i < 8; ++i )
        saturation.push_back( registry.acquire( "storm-run", "sat", 512 ) );

    constexpr int threads = 8;
    constexpr int rounds = 200;
    std::atomic<int> refused{ 0 };
    std::atomic<std::uint64_t> observedPeak{ 0 };
    std::vector<std::thread> workers;
    for ( int t = 0; t < threads; ++t )
    {
        workers.emplace_back( [&, t] {
            for ( int round = 0; round < rounds; ++round )
            {
                try
                {
                    auto lease = registry.acquire( "storm-run", "tile", 512 );
                    const std::uint64_t now = registry.outstandingBytes();
                    std::uint64_t expected = observedPeak.load();
                    while ( now > expected && !observedPeak.compare_exchange_weak( expected, now ) )
                    {
                    }
                    std::this_thread::yield();
                }
                catch ( const ScratchBudgetExceeded & )
                {
                    ++refused;
                }
            }
        } );
    }
    for ( auto &worker : workers )
        worker.join();
    REQUIRE( refused == threads * rounds );
    saturation.clear(); // release the saturation leases: accounting drains

    // A second storm where leases DO fit proves RAII drains the accounting
    // to exactly zero when every scope ends.
    for ( int round = 0; round < rounds; ++round )
    {
        auto lease = registry.acquire( "storm-run", "tile", 512 );
        REQUIRE( registry.outstandingBytes() <= 4096 );
    }
    REQUIRE( registry.outstandingBytes() == 0 );
}

TEST_CASE( "Mid-stream tile checkpoint restarts a simulated crash and refuses drift",
           "[lsee10][checkpoint]" )
{
    const auto root = std::filesystem::temp_directory_path() / "lsee10-ckpt";
    std::filesystem::create_directories( root );
    const std::string path = ( root / "task.tileckpt" ).string();

    const std::uint64_t operatorId = 777;
    std::uint64_t inputId = 4242;

    // Attempt 1: crash after 500 tiles (checkpoint left on disk).
    TileCheckpoint crashCheckpoint;
    crashCheckpoint.formatVersion = kTileCheckpointFormatVersion;
    crashCheckpoint.operatorIdentity = operatorId;
    crashCheckpoint.inputIdentity = inputId;
    crashCheckpoint.completedTiles = 500;
    crashCheckpoint.scratchRunId = "run-crash";
    REQUIRE( TileCheckpointWriter::save( path, crashCheckpoint ) );

    // Restart with the SAME identity: resume from the recorded position.
    const auto resumed = TileCheckpointWriter::load( path, operatorId, inputId );
    REQUIRE( resumed.has_value() );
    REQUIRE( resumed->completedTiles == 500 );

    // Restart after a parameter/input change: fail-closed, start from zero.
    inputId = 4243;
    REQUIRE_FALSE( TileCheckpointWriter::load( path, operatorId, inputId ).has_value() );

    // User cancellation deletes the checkpoint: nothing to resume.
    TileCheckpointWriter::remove( path );
    REQUIRE_FALSE( TileCheckpointWriter::load( path, operatorId, inputId ).has_value() );
}

TEST_CASE( "Execution cache hit/miss/self-heal stays correct at thousands of entries",
           "[lsee10][scale][cache]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    auto &cache = sicnu::data::ExecutionResultCache::instance();
    const bool wasEnabled = cache.isEnabled();
    const std::size_t oldMax = cache.maxEntries();
    cache.setEnabled( true );
    cache.setMaxEntries( 8192 );

    constexpr int kEntries = 3000;
    std::vector<std::string> outputs;
    outputs.reserve( kEntries );
    for ( int i = 0; i < kEntries; ++i )
    {
        const std::string path = ( dir.filePath( QString::number( i ) ) ).toStdString();
        { std::ofstream out( path, std::ios::binary ); out << "payload-" << i; }
        outputs.push_back( path );
        sicnu::data::ExecutionFingerprint fp;
        fp.digest = QByteArray::number( i );
        sicnu::data::ExecutionResultCache::CachedExecution execution;
        execution.declaredOutputPath = QString::fromStdString( path );
        execution.producedArtifacts.append( QString::fromStdString( path ) );
        QFileInfo info( QString::fromStdString( path ) );
        execution.artifactSizes.insert( QString::fromStdString( path ), info.size() );
        execution.artifactMsecs.insert( QString::fromStdString( path ),
                                        info.lastModified().toMSecsSinceEpoch() );
        cache.storeExecution( fp, execution );
    }

    // Every fingerprint hits and serves its own path.
    for ( int i = 0; i < kEntries; ++i )
    {
        sicnu::data::ExecutionFingerprint fp;
        fp.digest = QByteArray::number( i );
        const auto hit = cache.lookupExecution( fp );
        REQUIRE( hit.has_value() );
        REQUIRE( hit->declaredOutputPath.toStdString() == outputs[static_cast<std::size_t>( i )] );
    }

    // Out-of-band corruption self-heals: the entry is erased (a miss), the
    // real execution would re-run — never serve foreign bytes.
    std::filesystem::resize_file( outputs[0], 3 );
    sicnu::data::ExecutionFingerprint fp0;
    fp0.digest = QByteArray::number( 0 );
    REQUIRE_FALSE( cache.lookupExecution( fp0 ).has_value() );

    cache.clear();
    cache.setMaxEntries( oldMax );
    cache.setEnabled( wasEnabled );
}

TEST_CASE( "Poison task converges to terminal Failed after exactly the bounded retries",
           "[lsee10][poison]" )
{
    EngineGuard10 guard;
    auto &center = sicnu::TaskCenter::instance();
    center.shutdownForTests();
    center.setMaxAutoRetries( 2 );

    std::atomic<int> executions{ 0 };
    auto poisonExecutor = [ &executions ]( const sicnu::jobs::JobRequest &,
                                           sicnu::operators::RSOperatorContext & ) -> Json::Value {
        ++executions;
        // The worker-infrastructure crash class (isTransientExecutionError).
        throw std::runtime_error( "worker crashed: poison-task-storm" );
    };

    sicnu::jobs::JobRequest request;
    request.algorithmId = "poison:test";
    request.source = "test";
    const long taskId = center.submitJob( request, poisonExecutor );
    REQUIRE( taskId > 0 );

    const int attempts = 20000;
    int waited = 0;
    while ( !sicnu::isTerminalStatus( center.getTaskInfo( taskId ).status ) && waited < attempts )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        ++waited;
    }
    REQUIRE( sicnu::isTerminalStatus( center.getTaskInfo( taskId ).status ) );
    REQUIRE( center.getTaskInfo( taskId ).status == sicnu::TaskStatus::Failed );
    // 1 first execution + exactly maxAutoRetries re-executions — never an
    // unbounded respawn loop.
    REQUIRE( center.getTaskInfo( taskId ).autoRetryAttempts == 2 );
    REQUIRE( executions.load() == 3 );
    center.clearCompletedTasks();
}
