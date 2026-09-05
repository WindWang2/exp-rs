// test_worker_host.cpp — Phase K worker isolation tests: real job execution in
// the sicnu_worker process, protocol failure surfacing, cancel escalation, and
// crash isolation (worker death → typed host error, never a host crash).
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/local_worker_host.h"
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
