// tests/test_r32_t04_worker.cpp — WP05: typed frame admission + exception-
// safe slot recovery in LocalWorkerPool / local worker host (#2125 layer 2).
//
// Uses a real protocol-speaking helper (tests/data/r32_t04_bad_worker.py)
// for the violation frames and the real sicnu_worker binary for the healthy
// recovery leg. RUN_SERIAL like the other real-process worker lanes.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <stdexcept>
#include <string>

#include "processing/framework/local_worker_pool.h"

using sicnu::processing::LocalWorkerPool;
using sicnu::processing::LocalWorkerPoolConfig;

namespace
{
// The pool spawns workerProgram as ONE executable (no argument passing), so
// each mode is staged as its own copy of the helper with the mode pinned in
// a preamble line.
QString stageBadWorker( const QString &mode )
{
    static QTemporaryDir dir;
    const QString script = dir.filePath( QStringLiteral( "bad-%1.py" ).arg( mode ) );
    QFile out( script );
    if ( out.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        const QString source = QStringLiteral( CMAKE_SOURCE_DIR )
                               + QStringLiteral( "/tests/data/r32_t04_bad_worker.py" );
        QFile in( source );
        if ( in.open( QIODevice::ReadOnly ) )
        {
            out.write( QByteArray( "import sys\nsys.argv = [\"bad\", \"" )
                + mode.toUtf8() + QByteArray( "\"]\n" ) );
            out.write( in.readAll() );
        }
    }
    QFile::setPermissions( script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                       | QFile::ReadGroup | QFile::ExeGroup );
    return script;
}
} // namespace

TEST_CASE( "WP05 typed violations retire the worker and recover the slot",
           "[r32t04][wp05][pool]" )
{
    int argc = 1;
    static char arg0[] = "test_r32_t04_worker";
    char *argv[] = { arg0, nullptr };
    if ( !QCoreApplication::instance() )
        new QCoreApplication( argc, argv );

    for ( const char *mode : { "bad_progress", "bad_op", "bad_result_id" } )
    {
        // The worker program is "<script> <mode>"; QProcess starts the
        // script directly (shebang) with the mode as its argv[1].
        const QString program = stageBadWorker( QStringLiteral( mode ) );
        LocalWorkerPoolConfig config;
        config.workerProgram = program;
        config.maxWorkers = 1;
        config.jobTimeout = std::chrono::seconds( 20 );
        LocalWorkerPool pool;
        QString error;
        REQUIRE( pool.start( config, &error ) );

        bool typedRefusal = false;
        try
        {
            Json::Value params( Json::objectValue );
            pool.run( "rs:never_reaches_an_operator", params );
        }
        catch ( const std::runtime_error &e )
        {
            const std::string what = e.what();
            typedRefusal = what.find( "worker protocol:" ) != std::string::npos;
            // The violation must be a TYPED protocol refusal, not a generic
            // crash/timeout, and must name the offending member.
            CHECK( ( what.find( "'op'" ) != std::string::npos
                     || what.find( "'value'" ) != std::string::npos
                     || what.find( "'jobId'" ) != std::string::npos ) );
        }
        REQUIRE( typedRefusal );

        // Slot recovery: the worker was retired and the pool's capacity is
        // back (no permanent leak — the #2125 hazard).
        const auto health = pool.health();
        REQUIRE( health.aliveWorkers == 0 );

        // Capacity is truly usable again: a second run spawns a fresh worker
        // and gets the same typed refusal (not "no worker became available").
        bool secondRefusalTyped = false;
        try
        {
            Json::Value params( Json::objectValue );
            pool.run( "rs:again", params );
        }
        catch ( const std::runtime_error &e )
        {
            secondRefusalTyped =
                std::string( e.what() ).find( "worker protocol:" ) != std::string::npos;
        }
        REQUIRE( secondRefusalTyped );
        pool.shutdown();
    }
}

TEST_CASE( "WP05 a throwing onProgress callback cannot leak the worker slot",
           "[r32t04][wp05][pool][callback]" )
{
    int argc = 1;
    static char arg0[] = "test_r32_t04_worker";
    char *argv[] = { arg0, nullptr };
    if ( !QCoreApplication::instance() )
        new QCoreApplication( argc, argv );

    LocalWorkerPoolConfig config;
    config.workerProgram = stageBadWorker( QStringLiteral( "throwing_progress" ) );
    config.maxWorkers = 1;
    config.jobTimeout = std::chrono::seconds( 20 );
    LocalWorkerPool pool;
    REQUIRE( pool.start( config ) );

    bool contained = false;
    try
    {
        Json::Value params( Json::objectValue );
        pool.run( "rs:callback_throws", params,
                  {},
                  []( double, const std::string & ) {
                      throw std::runtime_error( "user callback exploded" );
                  } );
    }
    catch ( const std::runtime_error &e )
    {
        contained = std::string( e.what() ).find( "worker host exception" )
                    != std::string::npos;
    }
    REQUIRE( contained );
    // Exactly one release: the slot returned to the pool.
    REQUIRE( pool.health().aliveWorkers == 0 );
    pool.shutdown();
}

TEST_CASE( "WP05 pool still serves a real healthy job after a violation cycle",
           "[r32t04][wp05][pool][healthy]" )
{
    int argc = 1;
    static char arg0[] = "test_r32_t04_worker";
    char *argv[] = { arg0, nullptr };
    if ( !QCoreApplication::instance() )
        new QCoreApplication( argc, argv );

    // Violation cycle first: capacity-1 pool survives a typed retirement...
    {
        LocalWorkerPoolConfig config;
        config.workerProgram = stageBadWorker( QStringLiteral( "bad_progress" ) );
        config.maxWorkers = 1;
        config.jobTimeout = std::chrono::seconds( 20 );
        LocalWorkerPool pool;
        REQUIRE( pool.start( config ) );
        bool refused = false;
        try
        {
            Json::Value params( Json::objectValue );
            pool.run( "rs:x", params );
        }
        catch ( const std::runtime_error & )
        {
            refused = true;
        }
        REQUIRE( refused );
        REQUIRE( pool.health().aliveWorkers == 0 );
        pool.shutdown();
    }
    // ...and a fresh pool over the REAL worker completes a healthy job.
    {
        LocalWorkerPoolConfig config;
        config.workerProgram = QStringLiteral( SICNU_WORKER_EXE );
        config.maxWorkers = 1;
        config.jobTimeout = std::chrono::seconds( 30 );
        LocalWorkerPool pool;
        REQUIRE( pool.start( config ) );
        bool healthy = false;
        try
        {
            Json::Value params( Json::objectValue );
            // Unknown algorithm: a TYPED operator error ("worker error:")
            // proves the full handshake + dispatch + result loop works.
            pool.run( "rs:r32t04_nonexistent", params );
        }
        catch ( const std::runtime_error &e )
        {
            healthy = std::string( e.what() ).find( "worker error:" )
                      != std::string::npos;
        }
        REQUIRE( healthy );
        REQUIRE( pool.health().healthy() );
        pool.shutdown();
    }
}
