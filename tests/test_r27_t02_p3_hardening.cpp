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
#include "geospatial/raster/raster_writer.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"

#include <QTemporaryDir>

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
std::string scratch( const QString &parent, const char *name )
{
    const auto dir = std::filesystem::path( parent.toStdString() ) / name;
    std::filesystem::create_directories( dir );
    return dir.string();
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
        sicnu::data::GovernanceStore store;
        QString err;
        REQUIRE( store.open( dbPath, &err ) );
        REQUIRE( static_cast<bool>( store.appendAudit( "test", "open", "asset", "a1" ) ) );
    }
    {
        sicnu::data::GovernanceStore store;
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
    // With ALLOW_NETWORK/FILE_ACCESS=NO these fail FAST and typed; without
    // the gate PROJ would attempt an actual fetch of the URL.
    REQUIRE_THROWS_AS( sicnu::geo::Crs::fromUserInput( "https://169.254.169.254/wkt" ),
                       sicnu::geo::GeoError );
    REQUIRE_THROWS_AS( sicnu::geo::Crs::fromUserInput( "/etc/passwd" ),
                       sicnu::geo::GeoError );
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

    const sicnu::geo::MirrorVerifyReport attacked = sicnu::geo::verifyMirror( mirrorDir );
    REQUIRE( attacked.badEntries > 0 );
    REQUIRE( attacked.ok == 0 );
}
