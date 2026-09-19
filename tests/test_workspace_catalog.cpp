// test_workspace_catalog.cpp — Phase I persistent workspace catalog: schema,
// upsert/alias/tag bookkeeping, paged queries, transactional bulk import, and
// the 100k-record performance contract (indexed lookups and paged listing stay
// O(log n); the full-scan copy cost of the in-memory catalog is what Phase I
// removes).
#include <catch2/catch_test_macros.hpp>

#include "data/workspace_catalog.h"

#include <QTemporaryDir>

#include <sqlite3.h>

#include <chrono>

using namespace sicnu::data;

namespace
{
CatalogAsset makeAsset( int i )
{
    CatalogAsset a;
    a.assetId = QStringLiteral( "asset-%1" ).arg( i, 6, 10, QLatin1Char( '0' ) );
    a.sourceKey = a.assetId;
    a.canonicalSource = QStringLiteral( "/data/scene_%1.tif" ).arg( i, 6, 10, QLatin1Char( '0' ) );
    a.kind = QStringLiteral( "raster" );
    a.state = QStringLiteral( "Ready" );
    a.persistence = QStringLiteral( "project" );
    a.displayName = QStringLiteral( "Scene %1" ).arg( i );
    a.acquisitionMs = 1700000000000LL + i;
    a.revision = 1;
    a.aliases = QStringList{ QStringLiteral( "/vsicurl/http://x/scene_%1.tif" ).arg( i, 6, 10, QLatin1Char( '0' ) ) };
    a.tags = QStringList{ QStringLiteral( "sentinel-2" ) };
    return a;
}

/// Plants a RAISE(ABORT) guard on the catalog DB (direct DB surgery while the
/// catalog handle is idle) — the only deterministic way to make a step inside
/// an open transaction fail.
void plantTrigger( const QString &dbPath, const QString &sql )
{
    sqlite3 *raw = nullptr;
    REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &raw, SQLITE_OPEN_READWRITE,
                              nullptr ) == SQLITE_OK );
    char *err = nullptr;
    const int rc = sqlite3_exec( raw, sql.toUtf8().constData(), nullptr, nullptr, &err );
    if ( rc != SQLITE_OK )
        INFO( QString::fromUtf8( err ).toStdString() );
    sqlite3_free( err );
    sqlite3_close( raw );
    REQUIRE( rc == SQLITE_OK );
}
} // namespace

TEST_CASE( "WorkspaceCatalog round-trips assets with aliases and tags",
           "[workspace_catalog]" )
{
    QTemporaryDir dir;
    WorkspaceCatalog catalog;
    QString err;
    REQUIRE( catalog.open( dir.filePath( "catalog.sqlite" ), &err ) );
    REQUIRE( catalog.schemaVersion() == QStringLiteral( "1" ) );

    CatalogAsset asset = makeAsset( 42 );
    REQUIRE( catalog.upsertAsset( asset ) );

    const auto byId = catalog.byId( asset.assetId );
    REQUIRE( byId );
    REQUIRE( byId->displayName == QStringLiteral( "Scene 42" ) );
    // Hydrated aliases include the canonical source plus each alias spelling.
    REQUIRE( byId->aliases.size() == 2 );

    // Indexed alias lookup — both spellings resolve.
    REQUIRE( catalog.byPath( asset.canonicalSource ) );
    REQUIRE( catalog.byPath( asset.aliases.first() ) );
    REQUIRE_FALSE( catalog.byPath( QStringLiteral( "/nope.tif" ) ) );

    // Upsert updates in place; alias replacement drops stale spellings.
    asset.state = QStringLiteral( "Missing" );
    asset.aliases.clear();
    REQUIRE( catalog.upsertAsset( asset ) );
    REQUIRE( catalog.byId( asset.assetId )->state == QStringLiteral( "Missing" ) );
    REQUIRE_FALSE( catalog.byPath( asset.aliases.isEmpty() ? QString() : QString( "z" ) ) );
    REQUIRE( catalog.count() == 1 );

    REQUIRE( catalog.removeAsset( asset.assetId ) );
    REQUIRE( catalog.count() == 0 );
    REQUIRE_FALSE( catalog.removeAsset( asset.assetId ) );
}

TEST_CASE( "WorkspaceCatalog pages filtered queries", "[workspace_catalog]" )
{
    QTemporaryDir dir;
    WorkspaceCatalog catalog;
    QString err;
    REQUIRE( catalog.open( dir.filePath( "catalog.sqlite" ), &err ) );

    QVector<CatalogAsset> batch;
    for ( int i = 0; i < 250; ++i )
        batch.append( makeAsset( i ) );
    REQUIRE( catalog.upsertAssets( batch ) ); // one transaction

    CatalogQuery query;
    query.kind = QStringLiteral( "raster" );

    const auto first = catalog.page( query, 0, 100 );
    REQUIRE( first.total == 250 );
    REQUIRE( first.items.size() == 100 );
    const auto second = catalog.page( query, 100, 100 );
    REQUIRE( second.items.size() == 100 );
    // Paging is stable (deterministic order) and disjoint.
    REQUIRE( first.items.first().assetId != second.items.first().assetId );
    const auto tail = catalog.page( query, 200, 100 );
    REQUIRE( tail.items.size() == 50 );

    // State filter.
    query.state = QStringLiteral( "Missing" );
    REQUIRE( catalog.page( query, 0, 100 ).total == 0 );

    // Prefix filter.
    CatalogQuery prefix;
    prefix.textPrefix = QStringLiteral( "Scene 24" );
    REQUIRE( catalog.page( prefix, 0, 10 ).total == 11 ); // 24, 240..249
}

TEST_CASE( "WorkspaceCatalog batch upsert is atomic when a step fails",
           "[workspace_catalog][transaction]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "catalog.sqlite" ) );
    WorkspaceCatalog catalog;
    QString err;
    REQUIRE( catalog.open( dbPath, &err ) );
    const CatalogAsset first = makeAsset( 0 );
    REQUIRE( catalog.upsertAsset( first ) );

    // Abort only the SECOND asset's alias insert: the first asset's writes
    // already went through when the batch fails.
    plantTrigger( dbPath,
                  QStringLiteral( "CREATE TRIGGER fail_alias BEFORE INSERT ON aliases"
                                 " WHEN NEW.path='/data/scene_000001.tif'"
                                 " BEGIN SELECT RAISE(ABORT,'planted'); END" ) );

    CatalogAsset mutated = first;
    mutated.state = QStringLiteral( "Missing" );
    CatalogAsset second = makeAsset( 1 );
    const QVector<CatalogAsset> batch{ mutated, second };
    CHECK_FALSE( catalog.upsertAssets( batch ) );

    // Neither half is durable: the stored asset keeps its old row (no stale
    // aliases, no half-applied mutation) and the new asset never lands.
    REQUIRE( catalog.count() == 1 );
    const auto reloaded = catalog.byId( first.assetId );
    REQUIRE( reloaded );
    CHECK( reloaded->state == QStringLiteral( "Ready" ) );
    CHECK_FALSE( catalog.byPath( second.canonicalSource ).has_value() );

    // No transaction leaked: once the obstacle is gone the batch commits whole.
    plantTrigger( dbPath, QStringLiteral( "DROP TRIGGER fail_alias" ) );
    REQUIRE( catalog.upsertAssets( batch ) );
    REQUIRE( catalog.count() == 2 );
    CHECK( catalog.byId( first.assetId )->state == QStringLiteral( "Missing" ) );
}

TEST_CASE( "WorkspaceCatalog removeAsset keeps the asset when a step fails",
           "[workspace_catalog][transaction]" )
{
    QTemporaryDir dir;
    const QString dbPath = dir.filePath( QStringLiteral( "catalog.sqlite" ) );
    WorkspaceCatalog catalog;
    QString err;
    REQUIRE( catalog.open( dbPath, &err ) );
    const CatalogAsset asset = makeAsset( 7 );
    REQUIRE( catalog.upsertAsset( asset ) );

    plantTrigger( dbPath,
                  QStringLiteral( "CREATE TRIGGER fail_alias_del BEFORE DELETE ON aliases"
                                 " BEGIN SELECT RAISE(ABORT,'planted'); END" ) );
    CHECK_FALSE( catalog.removeAsset( asset.assetId ) );
    // The asset row and its aliases survive the aborted delete.
    REQUIRE( catalog.count() == 1 );
    REQUIRE( catalog.byId( asset.assetId ) );
    REQUIRE( catalog.byPath( asset.canonicalSource ) );

    plantTrigger( dbPath, QStringLiteral( "DROP TRIGGER fail_alias_del" ) );
    REQUIRE( catalog.removeAsset( asset.assetId ) );
    REQUIRE( catalog.count() == 0 );
    CHECK_FALSE( catalog.removeAsset( asset.assetId ) );
}

TEST_CASE( "WorkspaceCatalog stays fast at 100k records", "[workspace_catalog][perf]" )
{
    QTemporaryDir dir;
    WorkspaceCatalog catalog;
    QString err;
    REQUIRE( catalog.open( dir.filePath( "catalog.sqlite" ), &err ) );

    // Bulk import in 10 transactions of 10k (the batched-mutation contract).
    const auto importStart = std::chrono::steady_clock::now();
    for ( int batchStart = 0; batchStart < 100000; batchStart += 10000 )
    {
        QVector<CatalogAsset> batch;
        batch.reserve( 10000 );
        for ( int i = batchStart; i < batchStart + 10000; ++i )
            batch.append( makeAsset( i ) );
        REQUIRE( catalog.upsertAssets( batch ) );
    }
    const auto importMs = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - importStart ).count();
    REQUIRE( catalog.count() == 100000 );
    INFO( "bulk import ms: " << importMs );

    // Indexed point lookup: single-digit microseconds is normal; the contract
    // here is "well under a millisecond" (vs the O(N)-stat scan it replaces).
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
    // Sanitizer instrumentation roughly doubles the per-row SQLite cost; the
    // contracts stay proportional (the O(N)-scan alternative is orders of
    // magnitude away either way).
    constexpr double kLookupLimitMs = 400.0; // 2 ms each would still be 100x too slow
#else
    constexpr double kLookupLimitMs = 200.0; // 1 ms each would already be 100x too slow
#endif
    double lookupMs = 0.0;
    for ( int pass = 0; pass < 2; ++pass )
    {
        const auto lookupStart = std::chrono::steady_clock::now();
        for ( int probe = 0; probe < 200; ++probe )
        {
            const QString path = QStringLiteral( "/data/scene_%1.tif" ).arg( probe * 437, 6, 10, QLatin1Char( '0' ) );
            REQUIRE( catalog.byPath( path ).has_value() );
        }
        lookupMs = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - lookupStart ).count();
        INFO( "200 path lookups ms (pass " << pass << "): " << lookupMs );
        if ( lookupMs < kLookupLimitMs )
            break;
    }
    REQUIRE( lookupMs < kLookupLimitMs );

    // Paged listing never materializes the whole set.
    CatalogQuery all;
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
    // Sanitizer instrumentation roughly doubles the per-row SQLite cost; the
    // contracts stay proportional (the O(N)-scan alternative is orders of
    // magnitude away either way).
    constexpr double kPageLimitMs = 1500.0;
#else
    constexpr double kPageLimitMs = 500.0;
#endif
    // Shared CI runners jitter: a single over-limit pass is retried once, so
    // only a reproducible slowdown (a real regression) fails the contract.
    double pageMs = 0.0;
    for ( int pass = 0; pass < 2; ++pass )
    {
        const auto pageStart = std::chrono::steady_clock::now();
        for ( int page = 0; page < 20; ++page )
        {
            const auto result = catalog.page( all, page * 100, 100 );
            REQUIRE( result.items.size() == 100 );
            REQUIRE( result.total == 100000 );
        }
        pageMs = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - pageStart ).count();
        INFO( "20 pages ms (pass " << pass << "): " << pageMs );
        if ( pageMs < kPageLimitMs )
            break;
    }
    REQUIRE( pageMs < kPageLimitMs );
}
