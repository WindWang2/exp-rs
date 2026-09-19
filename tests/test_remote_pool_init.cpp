// test_remote_pool_init.cpp — RemoteDatasetPool first-use initialization
// concurrency regressions (issue #1047).
//
// Unlike test_remote_source_cache (loopback HTTP server, POSIX-only), these
// cases drive the pool's process-wide state machine directly: the single-Impl
// guarantee under a barrier-started acquire storm, the per-URL bound, and the
// acquire/clear contract. A local GeoTIFF keeps the suite network-free on
// every platform. Each Catch2 case is discovered as its own CTest test, so
// the runner gives every case a fresh process and therefore a genuine
// first use — the state the race in #1047 corrupted.
#include <catch2/catch_test_macros.hpp>

#include "data/providers/gdal_runtime.h"
#include "data/providers/remote_source_cache.h"

#include <QDir>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

using namespace sicnu::data;

namespace
{
void writeTiledTiff( const QString &path )
{
    providers::ensureGdalRuntime();
    const char *options[] = { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64", nullptr };
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 128, 128, 1,
                                      GDT_Float32, const_cast<char **>( options ) );
    REQUIRE( ds != nullptr );
    std::vector<float> buf( 128ull * 128, 1.0f );
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 128, 128, buf.data(),
                                               128, 128, GDT_Float32, 0, 0, nullptr )
             == CE_None );
    GDALClose( ds );
}

struct StormResult
{
    int acquired = 0;
    int peakConcurrent = 0;
    std::set<GDALDatasetH> distinctHandles;
};

/// Barrier-started acquire storm: every worker calls acquire at the same
/// instant, records the handle it received and holds the lease briefly so
/// overlapping checkouts are observable from the collected result.
StormResult runAcquireStorm( RemoteDatasetPool &pool, const QString &url, int threads )
{
    std::barrier gate( threads );
    std::atomic<int> acquired{ 0 };
    std::atomic<int> concurrent{ 0 };
    std::atomic<int> peak{ 0 };
    std::mutex resultMutex;
    std::set<GDALDatasetH> distinct;
    std::vector<std::thread> workers;
    workers.reserve( static_cast<size_t>( threads ) );

    for ( int i = 0; i < threads; ++i )
    {
        workers.emplace_back( [&] {
            gate.arrive_and_wait();
            auto lease = pool.acquire( url, GA_ReadOnly );
            if ( !lease )
                return;
            const int now = concurrent.fetch_add( 1 ) + 1;
            int observed = peak.load();
            while ( observed < now && !peak.compare_exchange_weak( observed, now ) )
            {
            }
            {
                const std::lock_guard<std::mutex> lock( resultMutex );
                distinct.insert( lease.get() );
            }
            acquired.fetch_add( 1 );
            std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
            concurrent.fetch_sub( 1 );
        } );
    }
    for ( auto &worker : workers )
        worker.join();

    StormResult result;
    result.acquired = acquired.load();
    result.peakConcurrent = peak.load();
    result.distinctHandles = std::move( distinct );
    return result;
}

QString makeTiledCog( const QTemporaryDir &dir, const char *name )
{
    const QString path = dir.filePath( QString::fromLatin1( name ) );
    writeTiledTiff( path );
    return QDir::toNativeSeparators( path );
}
} // namespace

TEST_CASE( "RemoteDatasetPool first use is race-free: one impl, one open, one handle",
           "[remote_cache][pool][concurrency]" )
{
    // Bound 1 makes the single-Impl guarantee observable: the pre-#1047 race
    // could hand two threads two different Impl instances, each opening (and
    // holding) its own handle for the same URL — two opens, two mutex
    // domains, one leaked Impl.
    qputenv( "SICNU_REMOTE_POOL_HANDLES", "1" );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString url = makeTiledCog( dir, "first-use.tif" );

    // DELIBERATELY no clear() here: under CTest this case runs in its own
    // process, so the storm below IS the process-wide first use the race in
    // #1047 corrupted. The exact assertions therefore depend on this being
    // the first pool touch in the process (declaration order / per-case
    // discovery); run the suite with ctest, not a shuffled direct binary run.
    auto &pool = RemoteDatasetPool::instance();
    const qint64 opensBefore = pool.openCount();

    constexpr int kThreads = 32;
    const StormResult result = runAcquireStorm( pool, url, kThreads );

    CHECK( result.acquired == kThreads );
    CHECK( result.distinctHandles.size() == 1 );
    CHECK( result.peakConcurrent == 1 );
    CHECK( pool.openCount() - opensBefore == 1 );

    pool.clear();
    qunsetenv( "SICNU_REMOTE_POOL_HANDLES" );
}

TEST_CASE( "RemoteDatasetPool never exceeds the per-URL bound under a concurrent storm",
           "[remote_cache][pool][concurrency]" )
{
    // Fresh process under CTest: the bound is applied at first use. If an
    // earlier case already initialized the process (direct binary run), the
    // assertions stay valid because they are upper bounds.
    qputenv( "SICNU_REMOTE_POOL_HANDLES", "2" );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString url = makeTiledCog( dir, "bound.tif" );

    auto &pool = RemoteDatasetPool::instance();
    pool.clear();
    const qint64 opensBefore = pool.openCount();

    constexpr int kThreads = 12;
    const StormResult result = runAcquireStorm( pool, url, kThreads );

    CHECK( result.acquired == kThreads );
    CHECK( result.peakConcurrent <= 2 );
    CHECK( result.distinctHandles.size() <= 2 );
    CHECK( pool.openCount() - opensBefore <= 2 );

    pool.clear();
    qunsetenv( "SICNU_REMOTE_POOL_HANDLES" );
}

TEST_CASE( "RemoteDatasetPool clear waits for leases and drops cached handles",
           "[remote_cache][pool][concurrency]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString url = makeTiledCog( dir, "clear.tif" );

    auto &pool = RemoteDatasetPool::instance();
    pool.clear();
    const qint64 opensBefore = pool.openCount();

    auto held = pool.acquire( url, GA_ReadOnly );
    REQUIRE( held );

    std::atomic<bool> entered{ false };
    std::atomic<bool> cleared{ false };
    std::thread clearer( [&] {
        entered = true;
        pool.clear();
        cleared = true;
    } );

    while ( !entered.load() )
        std::this_thread::yield();
    std::this_thread::sleep_for( std::chrono::milliseconds( 150 ) );
    CHECK_FALSE( cleared.load() ); // outstanding lease still blocks clear()

    held = RemoteDatasetLease{};
    clearer.join();
    CHECK( cleared.load() );

    // The sweep dropped the old handle: the next acquire opens freshly.
    {
        const auto again = pool.acquire( url, GA_ReadOnly );
        REQUIRE( again );
        CHECK( pool.openCount() - opensBefore == 2 );
    } // lease returned before the final clear (clear() waits for leases)

    pool.clear();
}

TEST_CASE( "RemoteDatasetPool clear is safe against a concurrent acquire storm",
           "[remote_cache][pool][concurrency]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString url = makeTiledCog( dir, "storm.tif" );

    auto &pool = RemoteDatasetPool::instance();
    pool.clear();

    std::atomic<int> completed{ 0 };
    constexpr int kWorkers = 4;
    constexpr int kIterations = 25;
    std::vector<std::thread> workers;
    workers.reserve( kWorkers );
    for ( int i = 0; i < kWorkers; ++i )
    {
        workers.emplace_back( [&] {
            for ( int n = 0; n < kIterations; ++n )
            {
                const auto lease = pool.acquire( url, GA_ReadOnly );
                if ( lease )
                    completed.fetch_add( 1 );
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
        } );
    }

    // clear() runs while the storm is in flight; it must neither crash nor
    // corrupt pool state (waiters wake on notify_all and re-open).
    std::atomic<bool> cleared{ false };
    std::thread clearer( [&] {
        pool.clear();
        cleared = true;
    } );

    for ( auto &worker : workers )
        worker.join();
    clearer.join();

    CHECK( cleared.load() );
    CHECK( completed.load() > 0 );

    // The pool is still usable after the interleaving.
    {
        const auto lease = pool.acquire( url, GA_ReadOnly );
        REQUIRE( lease );
    } // lease returned before the final clear

    pool.clear();
}
