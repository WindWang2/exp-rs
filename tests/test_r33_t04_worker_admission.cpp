// test_r33_t04_worker_admission.cpp — R33-T04 / WP04 + WP05 (#2125):
//
//   The worker frame gate only proved "an op member exists" — a mistyped
//   frame (string progress value, numeric jobId, non-finite value) reached
//   the consumers' bare asString()/asDouble() calls and escaped as
//   Json::LogicError, bypassing every release/retire path and permanently
//   leaking the worker's slot.
//
//   * frameMatchesOpShape: per-op typed admission (unit-level closed set);
//   * real helper process (tests/fixtures/r33_t04_bad_frame_worker.cpp):
//     a mistyped frame is a typed rejection, the worker is RETIRED (not
//     counted as a crash), and the capacity-1 pool still serves the next
//     healthy job;
//   * an onProgress callback that throws retires the worker exactly once
//     and the pool stays serviceable afterwards.
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/local_worker_pool.h"
#include "runtime/worker/worker_protocol.h"

#include <QCoreApplication>
#include <QString>

#include <json/json.h>

#include <chrono>
#include <stdexcept>
#include <string>

using namespace sicnu::processing;
using sicnu::runtime::worker::frameMatchesOpShape;

namespace
{

Json::Value parse( const std::string &json )
{
    Json::Value root;
    Json::Reader reader;
    reader.parse( json, root, false );
    return root;
}

bool shapeOk( const std::string &json, std::string &error )
{
    return frameMatchesOpShape( parse( json ), error );
}

void ensureQCoreApplication()
{
    if ( !QCoreApplication::instance() )
    {
        static int argc = 1;
        static char appName[] = "test_r33_t04_worker_admission";
        static char *argv[] = { appName, nullptr };
        new QCoreApplication( argc, argv );
    }
}

LocalWorkerPoolConfig badFramePoolConfig()
{
    LocalWorkerPoolConfig config;
    config.workerProgram = QString::fromUtf8( R33_T04_BAD_FRAME_WORKER );
    config.maxWorkers = 1;
    config.minWarmWorkers = 0;
    config.jobTimeout = std::chrono::seconds( 20 );
    return config;
}
} // namespace

TEST_CASE( "frameMatchesOpShape admits legal frames and rejects mistyped members",
           "[worker_pool][r33t04]" )
{
    std::string error;

    SECTION( "legal frames of every known op" )
    {
        CHECK( shapeOk( R"({"v":1,"op":"ready","caps":["progress"]})", error ) );
        CHECK( shapeOk( R"({"v":1,"op":"heartbeat"})", error ) );
        CHECK( shapeOk( R"({"v":1,"op":"progress","jobId":"j","value":0.4,"message":"m"})", error ) );
        CHECK( shapeOk( R"({"v":1,"op":"progress","jobId":"j"})", error ) ); // liveness-only
        CHECK( shapeOk( R"({"v":1,"op":"ack","jobId":"j","kind":"cancel"})", error ) );
        CHECK( shapeOk( R"({"v":1,"op":"error","jobId":"j","message":"m","code":"cancelled"})", error ) );
        CHECK( shapeOk( R"({"v":1,"op":"result","jobId":"j","payload":{"a":1},"outputs":[]})", error ) );
        // Unknown extension op: bounded-wait rule, only the shared op-string
        // requirement applies.
        CHECK( shapeOk( R"({"v":1,"op":"future-op","anything":[1,2]})", error ) );
    }

    SECTION( "non-string op is rejected" )
    {
        CHECK_FALSE( shapeOk( R"({"v":1,"op":5})", error ) );
        CHECK( error.find( "op" ) != std::string::npos );
    }

    SECTION( "mistyped progress members are rejected" )
    {
        CHECK_FALSE( shapeOk( R"({"v":1,"op":"progress","jobId":"j","value":"0.4"})", error ) );
        CHECK_FALSE( shapeOk( R"({"v":1,"op":"progress","jobId":"j","message":7})", error ) );
        // 1e999 parses to a non-finite double — rejected at admission.
        CHECK_FALSE( shapeOk( R"({"v":1,"op":"progress","jobId":"j","value":1e999})", error ) );
    }

    SECTION( "mistyped error/result/ack members are rejected" )
    {
        CHECK_FALSE( shapeOk( R"({"v":1,"op":"error","jobId":5,"message":"m"})", error ) );
        CHECK_FALSE( shapeOk( R"({"v":1,"op":"error","jobId":"j","message":true})", error ) );
        CHECK_FALSE( shapeOk( R"({"v":1,"op":"result","jobId":{},"payload":{}})", error ) );
        CHECK_FALSE( shapeOk( R"({"v":1,"op":"ack","jobId":"j","kind":9})", error ) );
    }
}

TEST_CASE( "a mistyped frame from a real helper process is a typed rejection, not a leaked slot",
           "[worker_pool][r33t04][helper]" )
{
    ensureQCoreApplication();
    LocalWorkerPool pool;
    QString error;
    REQUIRE( pool.start( badFramePoolConfig(), &error ) );

    // The string "value" progress frame: previously a Json::LogicError
    // escaped the drive loop and the worker's slot was never recovered.
    bool typedFailure = false;
    try
    {
        pool.run( "r33t04:bad_progress", Json::Value( Json::objectValue ) );
    }
    catch ( const std::runtime_error &e )
    {
        const std::string what = e.what();
        typedFailure = what.find( "frame rejected" ) != std::string::npos
                       && what.find( "value" ) != std::string::npos;
    }
    REQUIRE( typedFailure );

    {
        const auto health = pool.health();
        CHECK( health.totalFrameRejects == 1 );
        CHECK( health.totalCrashes == 0 ); // retired, not conflated with a crash
        CHECK( health.healthy() );
    }

    // The numeric "jobId" result frame: same typed rejection class.
    typedFailure = false;
    try
    {
        pool.run( "r33t04:bad_result", Json::Value( Json::objectValue ) );
    }
    catch ( const std::runtime_error &e )
    {
        typedFailure = std::string( e.what() ).find( "frame rejected" ) != std::string::npos;
    }
    REQUIRE( typedFailure );
    {
        const auto health = pool.health();
        CHECK( health.totalFrameRejects == 2 );
        CHECK( health.totalCrashes == 0 );
    }

    // Capacity was never leaked: the capacity-1 pool still serves a healthy
    // job on a FRESH worker.
    const auto ok = pool.run( "r33t04:ok", Json::Value( Json::objectValue ) );
    REQUIRE( ok.isObject() );
    CHECK( ok["status"].asString() == "ok" );
    {
        const auto health = pool.health();
        CHECK( health.totalRuns == 3 );
        CHECK( health.totalFrameRejects == 2 );
        CHECK( health.aliveWorkers <= 1 );
        CHECK( health.healthy() );
    }

    pool.shutdown();
}

TEST_CASE( "a throwing onProgress callback retires the worker and keeps the pool serviceable",
           "[worker_pool][r33t04][helper]" )
{
    ensureQCoreApplication();
    LocalWorkerPool pool;
    QString error;
    REQUIRE( pool.start( badFramePoolConfig(), &error ) );

    bool sawOriginalThrow = false;
    try
    {
        pool.run( "r33t04:ok", Json::Value( Json::objectValue ),
                  /*isCancelled=*/{},
                  /*onProgress=*/[]( double, const std::string & ) {
                      throw std::runtime_error( "user callback exploded" );
                  } );
    }
    catch ( const std::runtime_error &e )
    {
        sawOriginalThrow = std::string( e.what() ).find( "user callback exploded" )
                           != std::string::npos;
    }
    REQUIRE( sawOriginalThrow ); // the ORIGINAL exception propagates

    // Exactly-once recovery: the next healthy job runs on a fresh worker.
    const auto after = pool.run( "r33t04:ok", Json::Value( Json::objectValue ) );
    REQUIRE( after.isObject() );
    CHECK( after["status"].asString() == "ok" );
    {
        const auto health = pool.health();
        CHECK( health.totalRuns == 2 );
        CHECK( health.aliveWorkers <= 1 );
        CHECK( health.healthy() );
    }

    pool.shutdown();
}
