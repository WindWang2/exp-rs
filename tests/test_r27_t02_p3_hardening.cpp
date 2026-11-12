// test_r27_t02_p3_hardening.cpp — R27-T2 / #1803 P3 hardening oracles:
//
//   * RIO-7  fieldStatistics WHERE guard — statement-breaking tokens are a
//            typed refusal, never spliced into the OGR SQL.
//   * RIO-11 governance audit — single audit authority (gov_audit duplicate
//            retired) + bounded retention on open.
//   * RIO-12 CRS user input — network/file spellings fail typed instead of
//            being fetched/opened.
//   * RIO-13 mirror manifest — stripping "index" does not downgrade a v2
//            manifest to the pre-checksum contract.
#include <catch2/catch_test_macros.hpp>

#include "data/governance/governance_store.h"
#include "geospatial/crs/crs_policy.h"
#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/fabric/catalog_service.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"

#include <QTemporaryDir>

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

namespace
{
/// A file PATH under the (already existing) parent — the caller's writers
/// create the file; pre-creating it as a directory would make every
/// GDAL/writer open fail.
std::string scratch( const QString &parent, const char *name )
{
    return ( std::filesystem::path( parent.toStdString() ) / name ).string();
}
} // namespace

// ---------------------------------------------------------------------------
// RIO-7
// ---------------------------------------------------------------------------
TEST_CASE( "#1803 RIO-7 fieldStatistics refuses statement-breaking WHERE clauses",
           "[io][vector][r27t02]" )
{
    QTemporaryDir base;
    const std::string target = scratch( base.path(), "sites.gpkg" );
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
        target, "sites", "Point", { { "score", "Real" } },
        sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
    for ( int i = 0; i < 4; ++i )
    {
        Json::Value attrs( Json::objectValue );
        attrs["score"] = static_cast<double>( i );
        writer.writeFeature( attrs, "POINT (1 2)" );
    }
    writer.finalize();

    sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
    // The legitimate WHERE still evaluates.
    REQUIRE( reader.fieldStatistics( "score", "score > 1" ).nonNullCount == 2 );
    // Statement-breaking tokens are typed refusals BEFORE any SQL runs.
    REQUIRE_THROWS_AS( reader.fieldStatistics( "score", "1 = 1; DROP TABLE x" ),
                       sicnu::geo::GeoError );
    REQUIRE_THROWS_AS( reader.fieldStatistics( "score", "1 = 1 -- comment" ),
                       sicnu::geo::GeoError );
    REQUIRE_THROWS_AS( reader.fieldStatistics( "score", "1 = 1 /* injected" ),
                       sicnu::geo::GeoError );
}

// ---------------------------------------------------------------------------
// RIO-11
// ---------------------------------------------------------------------------
TEST_CASE( "#1803 RIO-11 governance audit: single authority, bounded retention",
           "[governance][r27t02]" )
{
    QTemporaryDir base;
    const QString dbPath = base.filePath( "gov.sqlite" );

    // First open creates the store; second open migrates/dedups.
    {
        sicnu::workspace::GovernanceStore store;
        QString err;
        REQUIRE( store.open( dbPath, &err ) );
        REQUIRE( static_cast<bool>( store.appendAudit( "test", "open", "asset", "a1" ) ) );
    }
    {
        sicnu::workspace::GovernanceStore store;
        QString err;
        REQUIRE( store.open( dbPath, &err ) );
    }

    sqlite3 *db = nullptr;
    REQUIRE( sqlite3_open_v2( dbPath.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr )
             == SQLITE_OK );
    char *errMsg = nullptr;
    // The trigger-fed duplicate table must be gone.
    auto countRows = []( sqlite3 *handle, const char *sql ) -> long long
    {
        sqlite3_stmt *stmt = nullptr;
        long long n = -1;
        if ( sqlite3_prepare_v2( handle, sql, -1, &stmt, nullptr ) == SQLITE_OK
             && sqlite3_step( stmt ) == SQLITE_ROW )
            n = sqlite3_column_int64( stmt, 0 );
            sqlite3_finalize( stmt );
        return n;
    };
    (void)errMsg;
    REQUIRE( countRows( db, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND "
                            "name='gov_audit'" )
             == 0 );
    REQUIRE( countRows( db, "SELECT COUNT(*) FROM sqlite_master WHERE type='trigger' AND "
                            "name='trg_audit_log_gov_audit'" )
             == 0 );
    REQUIRE( countRows( db, "SELECT COUNT(*) FROM audit_log" ) == 1 );
    sqlite3_close( db );
}

// ---------------------------------------------------------------------------
// RIO-12
// ---------------------------------------------------------------------------
TEST_CASE( "#1803 RIO-12 CRS user input never reaches network or filesystem",
           "[crs][r27t02]" )
{
    // The oracle asserts the GDAL refusal MESSAGE, not just a GeoError:
    // without the ALLOW flags an unresolvable host / unparsable file also
    // throws GeoError — only the flag-named refusal proves the input was
    // stopped BY THE POLICY before any I/O (verified against GDAL 3.9+:
    // "Cannot import ... due to ALLOW_NETWORK_ACCESS=NO").
    bool sawNetworkFlag = false;
    bool sawFileFlag = false;
    try
    {
        ( void )sicnu::geo::Crs::fromUserInput( "https://169.254.169.254/wkt" );
        FAIL( "network CRS input must refuse" );
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        const std::string what = e.what();
        sawNetworkFlag = what.find( "ALLOW_NETWORK_ACCESS" ) != std::string::npos
                         || what.find( "network" ) != std::string::npos;
    }
    try
    {
        ( void )sicnu::geo::Crs::fromUserInput( "/etc/passwd" );
        FAIL( "file CRS input must refuse" );
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        const std::string what = e.what();
        sawFileFlag = what.find( "ALLOW_FILE_ACCESS" ) != std::string::npos
                      || what.find( "file" ) != std::string::npos;
    }
    // On GDAL < 3.9 the pre-scan path refuses with the policy wording in
    // our own message; on >= 3.9 GDAL's flag-named error rides along in
    // details — either way the refusal must NAME the blocked vector.
    REQUIRE( sawNetworkFlag );
    REQUIRE( sawFileFlag );
    // Legitimate spellings keep resolving.
    REQUIRE( sicnu::geo::Crs::fromUserInput( "EPSG:4326" ).authid() == "EPSG:4326" );
}

// ---------------------------------------------------------------------------
// RIO-13
// ---------------------------------------------------------------------------
TEST_CASE( "#1803 RIO-13 stripping the index does not downgrade a v2 mirror manifest",
           "[io][mirror][r27t02]" )
{
    QTemporaryDir base;
    const std::string scene = scratch( base.path(), "s0.tif" );
    {
        sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
            scene, 32, 32, { sicnu::geo::RasterBandSpec {} },
            { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
        writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
        writer.setGeotransform( { 0.0, 1.0, 0.0, 32.0, 0.0, -1.0 } );
        std::vector<double> raster( 32ull * 32 );
        for ( std::size_t i = 0; i < raster.size(); ++i )
            raster[i] = double( i % 251 );
        writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
        writer.finalize();
    }

    sicnu::geo::FabricIntent intent;
    sicnu::geo::AssetRecord record;
    record.id = "s0";
    record.path = scene;
    record.datetimeUtc = "2024-01-01T00:00:00Z";
    record.hasBbox = true;
    record.minX = 0.0;
    record.minY = 0.0;
    record.maxX = 32.0;
    record.maxY = 32.0;
    record.mediaType = "image/tiff";
    record.roles = { "data" };
    intent.records = { record };
    intent.sceneBudget = 2;
    intent.grid.explicitGrid = true;
    intent.grid.crs.valid = true;
    intent.grid.crs.authid = "EPSG:4326";
    intent.grid.scaleX = 1.0;
    intent.grid.scaleY = 1.0;
    intent.grid.minX = 0.0;
    intent.grid.minY = 0.0;
    intent.grid.maxX = 32.0;
    intent.grid.maxY = 32.0;
    intent.chunkShape = { 1, 16, 16, 1 };
    intent.hasWindow = true;
    intent.windowW = 16;
    intent.windowH = 16;
    const sicnu::geo::FabricPlan plan = sicnu::geo::planFabric( intent );

    const std::string mirrorDir = scratch( base.path(), "mirror" );
    sicnu::geo::MirrorOptions options;
    options.mirrorDirectory = mirrorDir;
    REQUIRE( sicnu::geo::mirrorChunks( plan, options ).failed == 0 );
    REQUIRE( sicnu::geo::verifyMirror( mirrorDir ).ok > 0 );

    // The attack: strip "index" AND every chunk sha256 — the manifest must
    // NOT pass as pre-checksum v1 (its chunks still carry the 11.0 chunkKey
    // and the version stamp), so verification reports bad entries.
    const std::string manifestPath = mirrorDir + "/manifest.json";
    std::ifstream in( manifestPath );
    std::stringstream buffer;
    buffer << in.rdbuf();
    Json::Value manifest;
    buffer >> manifest;
    REQUIRE( manifest.isMember( "index" ) );
    manifest.removeMember( "index" );
    manifest.removeMember( "manifestVersion" );
    for ( const std::string &token : manifest.getMemberNames() )
    {
        Json::Value &chunks = manifest[token];
        if ( !chunks.isObject() )
            continue;
        for ( const std::string &key : chunks.getMemberNames() )
            chunks[key].removeMember( "sha256" );
    }
    std::ofstream out( manifestPath, std::ios::trunc );
    out << manifest.toStyledString();
    out.close();
    // (The manifest snapshot cache is keyed by size+mtime, so the edited
    // file is naturally re-read — no invalidation needed.)

    const sicnu::geo::MirrorVerifyReport attacked = sicnu::geo::verifyMirror( mirrorDir );
    REQUIRE( attacked.badEntries > 0 );
    REQUIRE( attacked.ok == 0 );
}


// ---------------------------------------------------------------------------
// RIO-14 — the repetition predicate (pure, hoisted for exactly this oracle)
// ---------------------------------------------------------------------------
TEST_CASE( "#1803 RIO-14 repeated pagination continuations are detected",
           "[fabric][r27t02]" )
{
    sicnu::geo::CatalogContinuation a;
    a.hasMore = true;
    a.localOffset = 40;
    a.remoteHref = "https://catalog.example/search?token=t1";
    sicnu::geo::CatalogContinuation same = a;
    REQUIRE( sicnu::geo::catalogContinuationRepeats( a, same ) );

    sicnu::geo::CatalogContinuation advanced = a;
    advanced.localOffset = 80; // offset moved: the next page is NEW work
    REQUIRE_FALSE( sicnu::geo::catalogContinuationRepeats( a, advanced ) );

    sicnu::geo::CatalogContinuation newHref = a;
    newHref.remoteHref = "https://catalog.example/search?token=t2";
    REQUIRE_FALSE( sicnu::geo::catalogContinuationRepeats( a, newHref ) );

    // Empty-string hrefs (pure local backends) compare by offset alone.
    sicnu::geo::CatalogContinuation localA;
    localA.localOffset = 12;
    sicnu::geo::CatalogContinuation localSame;
    localSame.localOffset = 12;
    REQUIRE( sicnu::geo::catalogContinuationRepeats( localA, localSame ) );
    sicnu::geo::CatalogContinuation localNext;
    localNext.localOffset = 24;
    REQUIRE_FALSE( sicnu::geo::catalogContinuationRepeats( localA, localNext ) );

    // A STAC POST search keeps one href and advances the body. An href-only
    // compare would call page two a replay and stop the crawl (#1941).
    sicnu::geo::CatalogContinuation posted = a;
    posted.remoteMethod = "POST";
    posted.remoteBody = Json::Value( Json::objectValue );
    posted.remoteBody["page"] = 1;
    sicnu::geo::CatalogContinuation postedNext = posted;
    postedNext.remoteBody["page"] = 2;
    REQUIRE_FALSE( sicnu::geo::catalogContinuationRepeats( posted, postedNext ) );
    REQUIRE( sicnu::geo::catalogContinuationRepeats( posted, posted ) );
}

TEST_CASE( "fabricIntentFromJson nested bbox replaces top-level bounds",
           "[fabric][query]" )
{
    Json::Value bounds( Json::arrayValue );
    bounds.append( 0.0 );
    bounds.append( 0.0 );
    bounds.append( 1.0 );
    bounds.append( 1.0 );
    Json::Value bbox( Json::arrayValue );
    bbox.append( 10.0 );
    bbox.append( 20.0 );
    bbox.append( 11.0 );
    bbox.append( 21.0 );

    Json::Value nested( Json::objectValue );
    nested["bounds"] = bounds;
    nested["query"] = Json::Value( Json::objectValue );
    nested["query"]["bbox"] = bbox;
    const sicnu::geo::FabricIntent replaced = sicnu::geo::fabricIntentFromJson( nested );
    REQUIRE( replaced.query.bbox.size() == 4 );
    CHECK( replaced.query.bbox[0] == 10.0 );
    CHECK( replaced.query.bbox[1] == 20.0 );
    CHECK( replaced.query.bbox[2] == 11.0 );
    CHECK( replaced.query.bbox[3] == 21.0 );

    Json::Value kept( Json::objectValue );
    kept["bounds"] = bounds;
    kept["query"] = Json::Value( Json::objectValue );
    kept["query"]["collections"].append( "sentinel-2" );
    const sicnu::geo::FabricIntent boundsKept = sicnu::geo::fabricIntentFromJson( kept );
    REQUIRE( boundsKept.query.bbox.size() == 4 );
    CHECK( boundsKept.query.bbox[0] == 0.0 );
    CHECK( boundsKept.query.bbox[3] == 1.0 );

    Json::Value flat( Json::objectValue );
    flat["bounds"] = bounds;
    flat["bbox"] = bbox;
    const sicnu::geo::FabricIntent flatReplaced = sicnu::geo::fabricIntentFromJson( flat );
    REQUIRE( flatReplaced.query.bbox.size() == 4 );
    CHECK( flatReplaced.query.bbox[0] == 10.0 );
    CHECK( flatReplaced.query.bbox[3] == 21.0 );
}

// ---------------------------------------------------------------------------
// RIO-13 residual — genuine v1 manifests (NO v2 signal) still replay
// size-only: the lenient path is a contract, not an accident.
// ---------------------------------------------------------------------------
TEST_CASE( "#1803 RIO-13 genuine v1 manifest entries stay size-checked only",
           "[io][mirror][r27t02]" )
{
    QTemporaryDir base;
    const std::string mirrorDir = scratch( base.path(), "mirror-v1" );
    const std::string chunksDir = mirrorDir + "/chunks";
    std::filesystem::create_directories( chunksDir );

    // A hand-made v1 manifest: bytes+file ONLY (pre-11.0 writers emitted
    // exactly this shape); a size-matching payload file.
    const std::uint64_t payloadBytes = 4096;
    const std::string file = "v1chunk.tif";
    {
        std::vector<char> bytes( payloadBytes, 7 );
        std::ofstream( chunksDir + "/" + file, std::ios::binary )
            .write( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
        Json::Value manifest( Json::objectValue );
        Json::Value chunk;
        chunk["file"] = file;
        chunk["bytes"] = static_cast<Json::UInt64>( payloadBytes );
        Json::Value chunks( Json::objectValue );
        chunks["tok1"] = chunk;
        manifest["tok1"] = chunks; // no index, no manifestVersion, no chunkKey
        std::ofstream( mirrorDir + "/manifest.json", std::ios::trunc )
            << manifest.toStyledString();
    }
    const sicnu::geo::MirrorVerifyReport report = sicnu::geo::verifyMirror( mirrorDir );
    REQUIRE( report.ok == 1 );
    REQUIRE( report.badEntries == 0 );
}

// ---------------------------------------------------------------------------
// RIO-11 — retention: the audit log rolls over at the 100k boundary on open
// ---------------------------------------------------------------------------
TEST_CASE( "#1803 RIO-11 audit log retention bounds growth on open",
           "[governance][r27t02]" )
{
    QTemporaryDir base;
    const QString dbPath = base.filePath( "retention.sqlite" );

    // Create the store, then seed the audit table past the boundary with
    // the raw sqlite handle (the store API is deliberately append-per-event).
    {
        sicnu::workspace::GovernanceStore store;
        QString err;
        REQUIRE( store.open( dbPath, &err ) );
        store.appendAudit( "seed", "open", "asset", "a1" );
    }
    // Seed 100_002 rows in one transaction (~fast), then reopen: open()
    // must truncate to the newest 100_000.
    {
        sqlite3 *db = nullptr;
        REQUIRE( sqlite3_open( dbPath.toUtf8().constData(), &db ) == SQLITE_OK );
        char *err = nullptr;
        REQUIRE( sqlite3_exec(
                     db, "BEGIN; "
                         "WITH RECURSIVE seq(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM seq "
                         "WHERE i < 100002) "
                         "INSERT INTO audit_log(ts_ms, actor, action, entity_kind, "
                         "entity_id, detail_json) "
                         "SELECT 1000000 + i, 'bulk', 'seed', 'asset', 'a' || i, '{}' "
                         "FROM seq; "
                         "COMMIT;",
                     nullptr, nullptr, &err )
                 == SQLITE_OK );
        sqlite3_free( err );
        sqlite3_close( db );
    }
    {
        sicnu::workspace::GovernanceStore store;
        QString err;
        REQUIRE( store.open( dbPath, &err ) );
        const auto tail = store.auditTail( 1 );
        REQUIRE( tail.size() == 1 ); // newest row survived the rollover
    }
    {
        sqlite3 *db = nullptr;
        REQUIRE( sqlite3_open( dbPath.toUtf8().constData(), &db ) == SQLITE_OK );
        sqlite3_stmt *stmt = nullptr;
        long long rows = -1;
        if ( sqlite3_prepare_v2( db, "SELECT COUNT(*) FROM audit_log", -1, &stmt,
                                 nullptr )
                 == SQLITE_OK
             && sqlite3_step( stmt ) == SQLITE_ROW )
            rows = sqlite3_column_int64( stmt, 0 );
        sqlite3_finalize( stmt );
        sqlite3_close( db );
        REQUIRE( rows == 100000 ); // bounded — not 100002
    }
}
