// test_remote_source_cache.cpp — Phase F remote COG caching layer tests:
// GDAL cache config defaults, the bounded remote dataset pool (coalescing,
// bounds, failure), and the validator-seam staleness bookkeeping.
#include <catch2/catch_test_macros.hpp>

#include "data/artifact_store.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "data/providers/remote_source_cache.h"
#include "support/mini_cog_server.h"

#include <QDir>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <atomic>
#include <utility>
#include <cstdlib>
#include <thread>

using namespace sicnu::data;
using sicnu_test::MiniCogServer;

namespace
{
void writeTiledTiff( const QString &path )
{
    ensureGdalInit();
    const char *options[] = { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64", nullptr };
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 128, 128, 1,
                                      GDT_Float32, const_cast<char **>( options ) );
    REQUIRE( ds != nullptr );
    std::vector<float> buf( 128ull * 128, 1.0f );
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, 0, 128, 128, buf.data(),
                                               128, 128, GDT_Float32, 0, 0, nullptr )
             == CE_None );
    GDALClose( ds );
}
} // namespace

TEST_CASE( "remote caching defaults are applied and bounded", "[remote_cache][config]" )
{
    configureRemoteCachingDefaults();
    REQUIRE( CPLGetConfigOption( "CPL_VSIL_CURL_CACHE_SIZE", nullptr ) != nullptr );
    REQUIRE( CPLGetConfigOption( "VSI_CACHE", nullptr ) != nullptr );
    REQUIRE( CPLGetConfigOption( "GDAL_HTTP_MAX_RETRY", nullptr ) != nullptr );
}

TEST_CASE( "RemoteDatasetPool coalesces sequential acquires onto one open",
           "[remote_cache][pool]" )
{
    qputenv( "SICNU_REMOTE_POOL_HANDLES", "1" );
    QTemporaryDir dir;
    const QString tiff = dir.filePath( "cog.tif" );
    writeTiledTiff( tiff );

    MiniCogServer server( tiff );
    REQUIRE( server.start() );
    const QString url = QString( "http://127.0.0.1:%1/cog.tif" ).arg( server.port() );

    auto &pool = RemoteDatasetPool::instance();
    pool.clear(); // reset counters and handles for a deterministic test
    const qint64 opensBefore = pool.openCount();

    {
        const auto lease1 = pool.acquire( url, GA_ReadOnly );
        REQUIRE( lease1 );
        REQUIRE( GDALGetRasterCount( lease1.get() ) == 1 );
    }
    {
        // Sequential re-acquire reuses the pooled handle: still one open.
        const auto lease2 = pool.acquire( url, GA_ReadOnly );
        REQUIRE( lease2 );
    }
    REQUIRE( pool.openCount() - opensBefore == 1 );

    // GDAL actually fetched the bytes over HTTP. Small rasters may be served
    // as one full GET (no Range header), so assert traffic, not ranges.
    REQUIRE( server.requests() >= 1 );
    pool.clear();
    qunsetenv( "SICNU_REMOTE_POOL_HANDLES" );
}

TEST_CASE( "RemoteDatasetPool bounds concurrent handles per URL",
           "[remote_cache][pool]" )
{
    qputenv( "SICNU_REMOTE_POOL_HANDLES", "1" );
    QTemporaryDir dir;
    const QString tiff = dir.filePath( "cog.tif" );
    writeTiledTiff( tiff );

    MiniCogServer server( tiff );
    REQUIRE( server.start() );
    const QString url = QString( "http://127.0.0.1:%1/cog.tif" ).arg( server.port() );

    auto &pool = RemoteDatasetPool::instance();
    pool.clear();
    const qint64 opensBefore = pool.openCount();

    auto held = pool.acquire( url, GA_ReadOnly );
    REQUIRE( held );

    // A concurrent acquire must block (bound 1) until the lease is returned,
    // NOT open a second handle.
    std::atomic<bool> secondAcquired{ false };
    std::thread second( [&] {
        const auto lease = pool.acquire( url, GA_ReadOnly );
        secondAcquired = lease.get() != nullptr;
    } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 120 ) );
    REQUIRE( secondAcquired.load() == false ); // still blocked, no new handle
    { RemoteDatasetLease releaser = std::move( held ); } // release → waiter proceeds
    second.join();
    REQUIRE( secondAcquired.load() );
    REQUIRE( pool.openCount() - opensBefore == 1 );
    pool.clear();
    qunsetenv( "SICNU_REMOTE_POOL_HANDLES" );
}

TEST_CASE( "RemoteDatasetPool surfaces open failures as empty leases",
           "[remote_cache][pool]" )
{
    auto &pool = RemoteDatasetPool::instance();
    pool.clear();
    const auto lease = pool.acquire( QStringLiteral( "http://127.0.0.1:1/none.tif" ),
                                     GA_ReadOnly );
    REQUIRE_FALSE( lease );
    pool.clear();
}

namespace
{
class FakeValidator final : public RemoteSourceValidator
{
  public:
    explicit FakeValidator( QString token ) : m_token( std::move( token ) ) {}
    QString currentValidatorToken( const QString & ) override { return m_token; }
    void setToken( const QString &token ) { m_token = token; }

  private:
    QString m_token;
};
} // namespace

TEST_CASE( "RemoteSourceCache bookkeeping is conservative", "[remote_cache][stale]" )
{
    QTemporaryDir dir;
    ArtifactStore store;
    QString err;
    REQUIRE( store.open( dir.filePath( "store.sqlite3" ), &err ) );

    RemoteSourceCache cache( store );
    // Without a validator: never stale.
    REQUIRE_FALSE( cache.isStale( QStringLiteral( "http://x/y.tif" ) ) );

    FakeValidator validator( QStringLiteral( "etag-1" ) );
    cache.setValidator( &validator );
    // Never validated: benefit of the doubt.
    REQUIRE_FALSE( cache.isStale( QStringLiteral( "http://x/y.tif" ) ) );

    cache.recordKnownGood( QStringLiteral( "http://x/y.tif" ), QStringLiteral( "etag-1" ) );
    REQUIRE( cache.recordedToken( QStringLiteral( "http://x/y.tif" ) )
             == QStringLiteral( "etag-1" ) );
    // Same token: fresh.
    REQUIRE_FALSE( cache.isStale( QStringLiteral( "http://x/y.tif" ) ) );

    // Remote changed: stale.
    validator.setToken( QStringLiteral( "etag-2" ) );
    REQUIRE( cache.isStale( QStringLiteral( "http://x/y.tif" ) ) );

    // Offline (empty token): offline fallback — assume unchanged.
    validator.setToken( QString() );
    REQUIRE_FALSE( cache.isStale( QStringLiteral( "http://x/y.tif" ) ) );

    // The token CHANGE bumps once (etag-1 → etag-2); repeating the SAME
    // token must not churn further versions.
    validator.setToken( QStringLiteral( "etag-2" ) );
    cache.recordKnownGood( QStringLiteral( "http://x/y.tif" ), QStringLiteral( "etag-2" ) );
    cache.recordKnownGood( QStringLiteral( "http://x/y.tif" ), QStringLiteral( "etag-2" ) );
    const auto record = store.latestByLogicalKey(
        QStringLiteral( "remote-source/http://x/y.tif" ) );
    REQUIRE( record );
    REQUIRE( record->version == 2 );

    cache.forget( QStringLiteral( "http://x/y.tif" ) );
    REQUIRE( cache.recordedToken( QStringLiteral( "http://x/y.tif" ) ).isEmpty() );
}
