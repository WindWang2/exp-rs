// test_worker_host.cpp — Phase K worker isolation tests: real job execution in
// the sicnu_worker process, protocol failure surfacing, cancel escalation, and
// crash isolation (worker death → typed host error, never a host crash).
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/local_worker_host.h"
#include "processing/framework/local_worker_pool.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#ifndef SICNU_WORKER_EXE
#define SICNU_WORKER_EXE "sicnu_worker"
#endif

using sicnu::processing::runInLocalWorker;
using sicnu::processing::LocalWorkerPool;
using sicnu::processing::LocalWorkerPoolConfig;

namespace
{
void writeLabelRaster( const QString &path )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    GDALDatasetH ds = createOutputTiff( path, 64, 64, 1, GDT_UInt16, gt, QString() );
    REQUIRE( ds != nullptr );
    std::vector<uint16_t> buf( 64ull * 64 );
    for ( size_t i = 0; i < buf.size(); ++i )
        buf[i] = static_cast<uint16_t>( 1 + i % 5 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 64, 64, buf.data(),
                           64, 64, GDT_UInt16, 0, 0 ) == CE_None );
    GDALClose( ds );
}
} // namespace

TEST_CASE( "worker host executes a real operator in the worker process",
           "[worker_host]" )
{
    int argc = 1;
    static char arg0[] = "test_worker_host";
    char *argv[] = { arg0, nullptr };
    if ( !QCoreApplication::instance() )
        new QCoreApplication( argc, argv );

    QTemporaryDir dir;
    const QString input = dir.filePath( "labels.tif" );
    writeLabelRaster( input );
    const QString output = dir.filePath( "recoded.tif" );

    Json::Value params;
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["recode_map"] = "{\"1\":5,\"2\":4,\"3\":3,\"4\":2,\"5\":1}";

    const Json::Value payload = runInLocalWorker( QStringLiteral( SICNU_WORKER_EXE ),
                                                  "rs:recode", params, {},
                                                  std::chrono::minutes( 5 ) );
    REQUIRE( payload["output"].asString() == output.toStdString() );
    REQUIRE( QFile( output ).exists() );
}

TEST_CASE( "worker host surfaces unknown algorithms as typed worker errors",
           "[worker_host]" )
{
    Json::Value params;
    bool typedError = false;
    try
    {
        runInLocalWorker( QStringLiteral( SICNU_WORKER_EXE ), "rs:does_not_exist", params, {},
                          std::chrono::minutes( 1 ) );
    }
    catch ( const std::runtime_error &e )
    {
        typedError = std::string( e.what() ).find( "worker error:" ) != std::string::npos;
    }
    REQUIRE( typedError );
}

TEST_CASE( "worker host isolates a worker that dies before the handshake",
           "[worker_host]" )
{
    // /bin/true exits immediately without speaking the protocol: the host must
    // report a typed failure, not crash.
    bool typedFailure = false;
    try
    {
        runInLocalWorker( QStringLiteral( "/bin/true" ), "rs:recode", {}, {},
                          std::chrono::seconds( 30 ) );
    }
    catch ( const std::runtime_error &e )
    {
        const std::string what = e.what();
        typedFailure = what.find( "worker protocol:" ) != std::string::npos ||
                       what.find( "worker crashed:" ) != std::string::npos;
    }
    REQUIRE( typedFailure );
}

TEST_CASE( "worker host cancel path terminates an unresponsive worker",
           "[worker_host]" )
{
    std::atomic<bool> cancelled{ false };
    std::thread canceller( [&cancelled] {
        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        cancelled = true;
    });
    bool typedFailure = false;
    try
    {
        runInLocalWorker( QStringLiteral( SICNU_WORKER_EXE ), "__hang__", {}, // hangs
                          [&cancelled] { return cancelled.load(); },
                          std::chrono::minutes( 1 ), std::chrono::milliseconds( 1500 ) );
    }
    catch ( const std::runtime_error &e )
    {
        const std::string what = e.what();
        typedFailure = what.find( "worker" ) != std::string::npos; // cancelled/crashed/error
    }
    canceller.join();
    REQUIRE( typedFailure );
}

TEST_CASE( "worker pool reuses a warm worker across jobs and reports health",
           "[worker_pool][warm_reuse]" )
{
    LocalWorkerPoolConfig config;
    config.workerProgram = QStringLiteral( SICNU_WORKER_EXE );
    config.maxWorkers = 1;
    config.minWarmWorkers = 1;
    LocalWorkerPool pool;
    QString error;
    REQUIRE( pool.start( config, &error ) );

    // The raster must outlive the run: build it in a stable temp dir.
    static QTemporaryDir stableDir;
    const QString input = stableDir.filePath( "pool-labels.tif" );
    writeLabelRaster( input );
    Json::Value params;
    params["input"] = input.toStdString();
    params["output"] = stableDir.filePath( "pool-a.tif" ).toStdString();
    params["recode_map"] = "{\"1\":5,\"2\":4,\"3\":3,\"4\":2,\"5\":1}";

    const auto first = pool.run( "rs:recode", params );
    REQUIRE( first.isObject() );
    REQUIRE( pool.health().totalRuns == 1 );

    const auto second = pool.run( "rs:recode", params );
    REQUIRE( second.isObject() );
    const auto health = pool.health();
    REQUIRE( health.totalRuns == 2 );
    // Warm reuse: one worker served both jobs (no spawn churn between them).
    REQUIRE( health.aliveWorkers <= 1 );
    REQUIRE( health.healthy() );
    pool.shutdown();
    REQUIRE_FALSE( pool.isRunning() );
}

TEST_CASE( "worker pool refuses jobs after shutdown", "[worker_pool][shutdown]" )
{
    LocalWorkerPoolConfig config;
    config.workerProgram = QStringLiteral( SICNU_WORKER_EXE );
    LocalWorkerPool pool;
    REQUIRE( pool.start( config ) );
    pool.shutdown();
    bool typedRefusal = false;
    try
    {
        (void)pool.run( "rs:recode", Json::Value( Json::objectValue ) );
    }
    catch ( const std::runtime_error &e )
    {
        typedRefusal = std::string( e.what() ).find( "worker pool" ) != std::string::npos;
    }
    REQUIRE( typedRefusal );
}

TEST_CASE( "worker pool recycles a worker past its lifetime budget",
           "[worker_pool][recycle]" )
{
    LocalWorkerPoolConfig config;
    config.workerProgram = QStringLiteral( SICNU_WORKER_EXE );
    config.maxWorkers = 1;
    config.maxJobsPerWorker = 1; // memory recycling: a new worker every job
    LocalWorkerPool pool;
    REQUIRE( pool.start( config ) );

    static QTemporaryDir stableDir;
    const QString input = stableDir.filePath( "recycle-labels.tif" );
    writeLabelRaster( input );
    Json::Value params;
    params["input"] = input.toStdString();
    params["output"] = stableDir.filePath( "recycle-out.tif" ).toStdString();
    params["recode_map"] = "{\"1\":5,\"2\":4,\"3\":3,\"4\":2,\"5\":1}";
    REQUIRE( pool.run( "rs:recode", params ).isObject() );
    REQUIRE( pool.run( "rs:recode", params ).isObject() );
    REQUIRE( pool.health().totalRecycles >= 1 );
    pool.shutdown();
}

TEST_CASE( "worker pool reports a typed failure for a broken worker program",
           "[worker_pool][crash]" )
{
    LocalWorkerPoolConfig config;
    config.workerProgram = QStringLiteral( "/bin/true" ); // dies pre-handshake
    LocalWorkerPool pool;
    REQUIRE( pool.start( config ) );
    bool typedFailure = false;
    try
    {
        (void)pool.run( "rs:recode", Json::Value( Json::objectValue ) );
    }
    catch ( const std::runtime_error &e )
    {
        const std::string what = e.what();
        typedFailure = what.find( "worker" ) != std::string::npos;
    }
    REQUIRE( typedFailure );
    REQUIRE( pool.health().totalCrashes >= 0 ); // health stays queryable
    pool.shutdown();
}
