// test_r27_t02_worker_lease.cpp — R27-T2 / #1764 (RIO-2): the worker lease
// table tracks LIVE workers, not recycled generations.
//
//   * tracker level: N spawn/reset generations keep trackedCount() bounded;
//     without reset() the count grows monotonically (the bug, kept as the
//     documented counter-example).
//   * pool level: real worker processes with maxJobsPerWorker=1 recycle a
//     fresh generation per job; after K jobs the tracked population stays
//     within the configured bound while totalRecycles proves the
//     generations actually churned.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/local_worker_pool.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "runtime/worker/worker_lease.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include <array>
#include <string>
#include <vector>

#ifndef SICNU_WORKER_EXE
#define SICNU_WORKER_EXE "sicnu_worker"
#endif

using sicnu::runtime::worker::WorkerLeaseTracker;
using sicnu::runtime::worker::WorkerHealth;
using sicnu::processing::LocalWorkerPool;
using sicnu::processing::LocalWorkerPoolConfig;

TEST_CASE( "#1764 lease table stays bounded across recycled generations",
           "[worker][r27t02][lease]" )
{
    WorkerLeaseTracker tracker;
    const int generations = 500;

    // The host's recycle protocol WITH the fix: every torn-down worker
    // releases its entry; a successor spawns under a fresh id.
    for ( int i = 0; i < generations; ++i )
    {
        const std::string id = "worker-" + std::to_string( i );
        tracker.onLiveness( id );
        tracker.reset( id );
    }
    REQUIRE( tracker.trackedCount() == 0 );

    // Live population bound: one worker alive at a time, cycled forever —
    // the table must never accumulate generations.
    for ( int i = 0; i < generations; ++i )
    {
        const std::string id = "gen-" + std::to_string( i );
        tracker.onLiveness( id );
        REQUIRE( tracker.trackedCount() <= 1 );
        tracker.reset( id );
    }

    // Counter-example (the RIO-2 bug shape, no reset on teardown): entries
    // accumulate one per generation.
    WorkerLeaseTracker leaking;
    for ( int i = 0; i < generations; ++i )
        leaking.onLiveness( "worker-" + std::to_string( i ) );
    REQUIRE( leaking.trackedCount() == generations );
}

TEST_CASE( "#1764 reset clears quarantine state with the dead generation",
           "[worker][r27t02][lease]" )
{
    WorkerLeaseTracker tracker;
    const std::string id = "w-dead";
    tracker.onLiveness( id );
    REQUIRE( tracker.onJobOutcome( id, false ) == WorkerHealth::Healthy );
    REQUIRE( tracker.onJobOutcome( id, false ) == WorkerHealth::Healthy );
    // Third consecutive failure reaches the default threshold (3).
    tracker.onJobOutcome( id, false );
    REQUIRE( tracker.verdict( id ) == WorkerHealth::Quarantined );
    REQUIRE( tracker.trackedCount() == 1 );
    tracker.reset( id );
    REQUIRE( tracker.trackedCount() == 0 );
    // The successor under a NEW id starts clean (existing contract; pinned
    // here because teardown-time reset is what makes the id lifecycle sound).
    REQUIRE( tracker.onLiveness( "w-next" ) == WorkerHealth::Healthy );
}

namespace
{
void writeLabelRaster( const QString &path )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    GDALDatasetH ds = createOutputTiff( path, 64, 64, 1, GDT_UInt16, gt,
                                                           QString() );
    REQUIRE( ds != nullptr );
    std::vector<std::uint16_t> buf( 64ull * 64 );
    for ( size_t i = 0; i < buf.size(); ++i )
        buf[i] = static_cast<std::uint16_t>( 1 + i % 5 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 64, 64, buf.data(), 64,
                           64, GDT_UInt16, 0, 0 )
             == CE_None );
    GDALClose( ds );
}
} // namespace

TEST_CASE( "#1764 real pool: recycled worker generations keep the lease table bounded",
           "[worker][r27t02][pool]" )
{
    int argc = 1;
    static char arg0[] = "test_r27_t02_worker_lease";
    char *argv[] = { arg0, nullptr };
    if ( !QCoreApplication::instance() )
        new QCoreApplication( argc, argv );

    LocalWorkerPoolConfig config;
    config.workerProgram = QStringLiteral( SICNU_WORKER_EXE );
    config.maxWorkers = 1;
    config.minWarmWorkers = 1;
    config.maxJobsPerWorker = 1; // every job exhausts its worker → recycle
    LocalWorkerPool pool;
    QString error;
    REQUIRE( pool.start( config, &error ) );

    static QTemporaryDir stableDir;
    const QString input = stableDir.filePath( "r27t02-labels.tif" );
    writeLabelRaster( input );

    const int jobs = 4;
    for ( int i = 0; i < jobs; ++i )
    {
        Json::Value params;
        params["input"] = input.toStdString();
        params["output"] = stableDir.filePath( "r27t02-out-" + QString::number( i )
                                                 + ".tif" )
                             .toStdString();
        params["recode_map"] = "{\"1\":5,\"2\":4,\"3\":3,\"4\":2,\"5\":1}";
        const auto result = pool.run( "rs:recode", params );
        REQUIRE( result.isObject() );
    }

    const auto health = pool.health();
    REQUIRE( health.totalRuns == jobs );
    // Generations actually churned (recycles happened) — the premise of the
    // unbounded-growth bug.
    REQUIRE( health.totalRecycles >= jobs - 1 );
    // THE ORACLE (#1764): despite jobs distinct generations, the tracked
    // population reflects LIVE workers only.
    REQUIRE( health.trackedLeases <= config.maxWorkers );
    pool.shutdown();
    REQUIRE( health.trackedLeases <= config.maxWorkers );
}
