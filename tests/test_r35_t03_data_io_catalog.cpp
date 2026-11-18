/***************************************************************************
  tests/test_r35_t03_data_io_catalog.cpp — R35-T03 geospatial IO/catalog
  regressions: finite-value query admission (WP01), exact-token instrument
  parity across backends (WP02), byte-bounded catalog JSON reads (WP04),
  bounded directory walks (WP05), checked tile math (WP10) and the
  single-open reader metadata contract (WP11).
 ***************************************************************************/

#include "geospatial/catalog/asset_query.h"
#include "geospatial/fabric/catalog_service.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_r35_t03" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

void writeFile( const std::string &path, const std::string &content )
{
  std::ofstream out( path, std::ios::binary );
  out << content;
}

std::string itemJson( const std::string &id, const std::string &instrumentsCsv )
{
  Json::Value item;
  item["type"] = "Feature";
  item["stac_version"] = "1.0.0";
  item["id"] = id;
  item["collection"] = "r35-t03";
  item["bbox"] = Json::Value( Json::arrayValue );
  item["bbox"].append( 8.0 );
  item["bbox"].append( 50.0 );
  item["bbox"].append( 9.0 );
  item["bbox"].append( 51.0 );
  item["properties"]["datetime"] = "2024-05-01T10:00:00Z";
  item["properties"]["eo:cloud_cover"] = 5.0;
  item["properties"]["platform"] = "S2A";
  item["properties"]["instruments"] = Json::Value( Json::arrayValue );
  std::string segment;
  for ( const char c : instrumentsCsv )
  {
    if ( c == ',' )
    {
      if ( !segment.empty() )
        item["properties"]["instruments"].append( segment );
      segment.clear();
    }
    else
    {
      segment += c;
    }
  }
  if ( !segment.empty() )
    item["properties"]["instruments"].append( segment );
  Json::Value data;
  data["href"] = id + ".tif";
  data["type"] = "image/tiff";
  Json::Value roles( Json::arrayValue );
  roles.append( "data" );
  data["roles"] = roles;
  item["assets"][ "image" ] = data;
  Json::StreamWriterBuilder builder;
  return Json::writeString( builder, item );
}

AssetRecord instrumentRecord( const std::string &id, const std::string &instruments )
{
  AssetRecord record;
  record.id = id;
  record.collection = "r35-t03";
  record.datetimeUtc = "2024-05-01T10:00:00Z";
  record.mediaType = "image/tiff";
  record.roles = { "data" };
  record.metadata["platform"] = "S2A";
  record.metadata["instruments"] = instruments;
  record.path = "/data/" + id + ".tif";
  return record;
}

std::set<std::string> resultIds( const std::vector<AssetRecord> &records )
{
  std::set<std::string> ids;
  for ( const AssetRecord &record : records )
    ids.insert( record.id );
  return ids;
}

std::set<std::string> searchIds( const CatalogService &service, const CatalogQuery &query )
{
  return resultIds( service.searchAll( query ).records );
}

} // namespace

// --- WP01: finite-value admission ------------------------------------------

TEST_CASE( "queries refuse non-finite bounds as typed errors before any backend work",
           "[io][catalog][r35t03]" )
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  CatalogQuery nanBbox;
  nanBbox.bbox = { 8.0, 50.0, nan, 51.0 };
  REQUIRE_THROWS_AS( nanBbox.validate(), GeoError );
  CatalogQuery infBbox;
  infBbox.bbox = { 8.0, -inf, 9.0, 51.0 };
  REQUIRE_THROWS_AS( infBbox.validate(), GeoError );
  CatalogQuery nanCloud;
  nanCloud.hasCloudCoverMax = true;
  nanCloud.cloudCoverMax = nan;
  REQUIRE_THROWS_AS( nanCloud.validate(), GeoError );
  CatalogQuery infCloud;
  infCloud.hasCloudCoverMax = true;
  infCloud.cloudCoverMax = inf;
  REQUIRE_THROWS_AS( infCloud.validate(), GeoError );

  // The legal 3D bbox (west below zero, z in slots 2/5) stays admitted.
  CatalogQuery legal3d;
  legal3d.bbox = { -9.0, 50.0, 100.0, 9.0, 51.0, 200.0 };
  CHECK_NOTHROW( legal3d.validate() );

  // The refusal rides on the search entry points too — a NaN query over a
  // memory backend throws instead of scanning records.
  std::vector<AssetRecord> records = { instrumentRecord( "a", "MSI" ) };
  const CatalogService service = catalogServiceOverRecords( std::move( records ) );
  CHECK_THROWS_AS( service.searchAll( nanBbox ), GeoError );

  // The asset_query engine guards its own bbox arm the same way.
  AssetQuery nanAsset;
  nanAsset.requiresBbox = true;
  nanAsset.minX = nan;
  nanAsset.maxX = 9.0;
  nanAsset.minY = 50.0;
  nanAsset.maxY = 51.0;
  REQUIRE_THROWS_AS( nanAsset.validate(), GeoError );
  AssetQueryOptions options;
  options.limit = 10;
  options.hardCap = 100;
  CHECK_THROWS_AS( queryAssets( {}, nanAsset, options ), GeoError );

  AssetQuery infAsset;
  infAsset.requiresBbox = true;
  infAsset.minX = 8.0;
  infAsset.maxX = inf;
  infAsset.minY = 50.0;
  infAsset.maxY = 51.0;
  CHECK_THROWS_AS( infAsset.validate(), GeoError );
}

// --- WP02: exact-token instrument parity -----------------------------------

TEST_CASE( "instrument filtering is exact-token on memory records — no substring collisions",
           "[io][fabric][catalog][r35t03]" )
{
  std::vector<AssetRecord> records = {
    instrumentRecord( "s2", "MSI" ),
    instrumentRecord( "s3", "OLI,OLI_TIRS" ),
    instrumentRecord( "msi2", "MSI2" ),
    instrumentRecord( "spaced", " MSI , OLI " ),
    instrumentRecord( "multiword", "OLI TIRS" ),
    instrumentRecord( "undated", "" ),
  };
  const CatalogService service = catalogServiceOverRecords( std::move( records ) );

  CatalogQuery wantsMsi;
  wantsMsi.sensorInstruments = { "MSI" };
  CHECK( searchIds( service, wantsMsi ) == std::set<std::string>{ "s2", "spaced" } );

  CatalogQuery wantsLower;
  wantsLower.sensorInstruments = { "msi" };
  CHECK( searchIds( service, wantsLower ) == std::set<std::string>{ "s2", "spaced" } );

  CatalogQuery wantsOli;
  wantsOli.sensorInstruments = { "OLI" };
  CHECK( searchIds( service, wantsOli ) == std::set<std::string>{ "s3", "spaced" } );

  // Multi-word instrument names keep their interior spaces on BOTH sides
  // of the comparison — collapsing them would re-split record and item.
  CatalogQuery wantsMultiword;
  wantsMultiword.sensorInstruments = { "OLI TIRS" };
  CHECK( searchIds( service, wantsMultiword ) == std::set<std::string>{ "multiword" } );

  // Any-of contract: one hit in the wanted list qualifies the record.
  CatalogQuery wantsAny;
  wantsAny.sensorInstruments = { "MSI2", "TIRS" };
  CHECK( searchIds( service, wantsAny ) == std::set<std::string>{ "s3", "msi2" } );

  // Nothing matches a wanted token no record carries exactly.
  CatalogQuery wantsTirs;
  wantsTirs.sensorInstruments = { "TIRS" };
  CHECK( searchIds( service, wantsTirs ).empty() );
}

TEST_CASE( "instrument filtering agrees between memory records and local STAC items",
           "[io][fabric][catalog][r35t03]" )
{
  const std::string root = scratchDir( "instrument_parity" );
  writeFile( root + "/msi.json", itemJson( "msi-item", "MSI" ) );
  writeFile( root + "/msi2.json", itemJson( "msi2-item", "MSI2" ) );
  writeFile( root + "/oli.json", itemJson( "oli-item", "OLI,OLI_TIRS" ) );
  writeFile( root + "/multiword.json", itemJson( "multiword-item", "OLI TIRS" ) );

  const CatalogService local = openCatalogService( root );
  REQUIRE( local.info().kind == CatalogBackendKind::LocalStacFiles );

  // The memory backend holds the records the SAME items would enrich to
  // (enrichment joins item.instruments with ",").
  std::vector<AssetRecord> records = {
    instrumentRecord( "msi-item", "MSI" ),
    instrumentRecord( "msi2-item", "MSI2" ),
    instrumentRecord( "oli-item", "OLI,OLI_TIRS" ),
    instrumentRecord( "multiword-item", "OLI TIRS" ),
  };
  const CatalogService memory = catalogServiceOverRecords( std::move( records ) );

  for ( const std::string &wanted :
        { std::string( "MSI" ), std::string( "OLI" ), std::string( "MSI2" ), std::string( "OLI TIRS" ) } )
  {
    CatalogQuery query;
    query.sensorInstruments = { wanted };
    CHECK( searchIds( local, query ) == searchIds( memory, query ) );
  }

  CatalogQuery wantsMsi;
  wantsMsi.sensorInstruments = { "MSI" };
  CHECK( searchIds( local, wantsMsi ) == std::set<std::string>{ "msi-item" } );
  CatalogQuery wantsOli;
  wantsOli.sensorInstruments = { "OLI" };
  CHECK( searchIds( local, wantsOli ) == std::set<std::string>{ "oli-item" } );
}

// --- WP04: byte-bounded JSON reads ------------------------------------------

TEST_CASE( "oversized and unreadable candidates are typed skips, never parses or crashes",
           "[io][fabric][catalog][r35t03]" )
{
  const std::string root = scratchDir( "json_bounds" );
  // Just above the 16 MiB catalog-document cap.
  std::string big( 16ull * 1024ull * 1024ull + 1, ' ' );
  big.front() = '{';
  writeFile( root + "/big.json", big );
  // A directory named like a candidate: open/read failure, not a walk kill.
  fs::create_directories( root + "/fake.json" );
  // Broken JSON and a good item share the same walk.
  writeFile( root + "/broken.json", "{ not json" );
  writeFile( root + "/ok.json", itemJson( "ok-item", "MSI" ) );

  const CatalogService service = openCatalogService( root );
  const CatalogService::SearchAllResult all = service.searchAll( {} );
  CHECK( resultIds( all.records ) == std::set<std::string>{ "ok-item" } );
  CHECK( all.unresolvable == 0 );

  // A root catalog.json that is actually a DIRECTORY: the link-following
  // branch sees it "exist", tries to read it as JSON and gets a typed
  // failure — the service still opens and serves nothing, without crashing.
  const std::string linkRoot = scratchDir( "json_bounds_link" );
  fs::create_directories( linkRoot + "/catalog.json" );
  writeFile( linkRoot + "/stray.json", itemJson( "stray-item", "MSI" ) );
  const CatalogService linkService = openCatalogService( linkRoot );
  CHECK( linkService.searchAll( {} ).records.empty() );
}

// --- WP05: bounded directory walks ------------------------------------------

TEST_CASE( "plain directory walks honor depth and file caps with honest truncation",
           "[io][fabric][catalog][r35t03]" )
{
  const std::string root = scratchDir( "walk_bounds" );
  writeFile( root + "/flat.json", itemJson( "flat-item", "MSI" ) );
  fs::create_directories( root + "/a/b/c" );
  writeFile( root + "/a/b/c/deep.json", itemJson( "deep-item", "MSI" ) );

  CatalogServiceOptions shallow;
  shallow.localMaxDepth = 2;   // /a (1), /a/b (2) are in; /a/b/c (3) is not
  const CatalogService shallowService = openCatalogService( root, shallow );
  CHECK( searchIds( shallowService, {} ) == std::set<std::string>{ "flat-item" } );

  CatalogServiceOptions deeper;
  deeper.localMaxDepth = 3;
  const CatalogService deepService = openCatalogService( root, deeper );
  CHECK( searchIds( deepService, {} ) ==
         std::set<std::string>{ "flat-item", "deep-item" } );

  // File-count cap: two candidates fit, the rest refuse with a truncation
  // marker instead of a silently shorter result.
  const std::string capped = scratchDir( "walk_capped" );
  writeFile( capped + "/one.json", itemJson( "one", "MSI" ) );
  writeFile( capped + "/two.json", itemJson( "two", "MSI" ) );
  writeFile( capped + "/three.json", itemJson( "three", "MSI" ) );
  CatalogServiceOptions tiny;
  tiny.localMaxFiles = 2;
  const CatalogService cappedService = openCatalogService( capped, tiny );
  const CatalogPage page = cappedService.searchPage( {}, {} );
  CHECK( page.records.size() == 2 );
  CHECK( page.truncatedByCap );
  const CatalogService::SearchAllResult all = cappedService.searchAll( {}, {} );
  CHECK( all.records.size() == 2 );
  CHECK( all.truncatedByCap );
}

#if defined( __unix__ ) || defined( __APPLE__ )
TEST_CASE( "symlink loops in directory walks stay bounded by the depth cap",
           "[io][fabric][catalog][r35t03]" )
{
  const std::string root = scratchDir( "walk_symlink" );
  writeFile( root + "/item.json", itemJson( "loop-item", "MSI" ) );
  fs::create_symlink( root, root + "/loop" );   // root → root/loop → root…

  CatalogServiceOptions options;
  options.localMaxDepth = 4;
  options.localMaxFiles = 64;
  const CatalogService service = openCatalogService( root, options );
  // The walk terminates (bounded by depth/visited caps) and still serves
  // the real item.
  CHECK( searchIds( service, {} ) == std::set<std::string>{ "loop-item" } );
}
#endif

// --- WP10: checked tile math ------------------------------------------------

TEST_CASE( "window clamping and tile planning survive far-edge geometry without int overflow",
           "[io][raster][r35t03]" )
{
  RasterMetadata meta;
  meta.width = 1000;
  meta.height = 1000;
  meta.bandCount = 1;

  // {xOff=2, width=INT_MAX}: int math overflowed here (xOff+width UB) and
  // could produce a bogus "clamped" window; now it clamps exactly.
  RasterWindow far;
  far.xOff = 2;
  far.yOff = 0;
  far.width = std::numeric_limits<int>::max();
  far.height = 1;
  RasterWindow clamped = far;
  REQUIRE( clampWindowToRaster( meta, clamped ) );
  CHECK( clamped.xOff == 2 );
  CHECK( clamped.yOff == 0 );
  CHECK( clamped.width == 998 );
  CHECK( clamped.height == 1 );

  // Fully disjoint far-edge window: refused, not wrapped.
  RasterWindow disjoint;
  disjoint.xOff = 2000;
  disjoint.yOff = 0;
  disjoint.width = std::numeric_limits<int>::max();
  disjoint.height = 1;
  RasterWindow disjointClamped = disjoint;
  CHECK_FALSE( clampWindowToRaster( meta, disjointClamped ) );

  // Pure-plan grid at the INT_MAX edge: representable grid, exact corner
  // slice, no raster allocation anywhere in the path.
  RasterMetadata huge;
  huge.width = std::numeric_limits<int>::max();
  huge.height = 4;
  huge.bandCount = 1;
  RasterWindow whole;
  whole.xOff = 0;
  whole.yOff = 0;
  whole.width = std::numeric_limits<int>::max();
  whole.height = 4;
  const TilePlan plan = planTileWalk( huge, whole, 1 << 30, 1 << 30 );
  CHECK( plan.tilesX == 2 );
  CHECK( plan.tilesY == 1 );
  const TileSlice corner = plan.slice( 1, 0 );
  CHECK( corner.xOff == ( 1 << 30 ) );
  CHECK( corner.width == std::numeric_limits<int>::max() - ( 1 << 30 ) );
  CHECK( corner.height == 4 );

  // Out-of-grid tile indices are rejected (typed), not wrapped.
  CHECK_THROWS_AS( plan.slice( 2, 0 ), GeoError );
  CHECK_THROWS_AS( plan.slice( 0, -1 ), GeoError );
  CHECK_THROWS_AS( plan.slice( -1, 0 ), GeoError );

  // Every slice of a normal grid covers the window exactly once.
  const TilePlan normal = planTileWalk( meta, { 0, 0, 1000, 1000 }, 128, 128 );
  int covered = 0;
  for ( int ty = 0; ty < normal.tilesY; ++ty )
    for ( int tx = 0; tx < normal.tilesX; ++tx )
    {
      const TileSlice slice = normal.slice( tx, ty );
      REQUIRE( slice.xOff >= 0 );
      REQUIRE( slice.yOff >= 0 );
      REQUIRE( slice.xOff + slice.width <= 1000 );
      REQUIRE( slice.yOff + slice.height <= 1000 );
      covered += slice.width * slice.height;
    }
  CHECK( covered == 1000 * 1000 );
}

// --- WP11: single-open metadata ---------------------------------------------

// POSIX-guarded: the barrier replaces the path via staged rename while the
// reader holds the old inode open — Windows rename-over-open-handle depends
// on share modes this test does not control.
#if defined( __unix__ ) || defined( __APPLE__ )
TEST_CASE( "an open reader keeps one file generation across on-disk replacement",
           "[io][raster][r35t03]" )
{
  const std::string dir = scratchDir( "replace_barrier" );
  const std::string target = ( fs::path( dir ) / "ab.tif" ).string();

  // Generation A: 4x4 of 9.0.
  {
    RasterWriter writer = RasterWriter::create( target, 4, 4, { {} }, RasterWriteOptions{ "GTiff", {}, true } );
    RasterWindow full;
    full.width = 4;
    full.height = 4;
    const std::vector<double> values( 16, 9.0 );
    writer.writeWindow( 1, full, values.data() );
    writer.finalize();
  }

  RasterReader reader = RasterReader::open( target );
  REQUIRE( reader.metadata().width == 4 );
  REQUIRE( reader.metadata().height == 4 );

  // Generation B replaces the path: 8x8 of 5.0.
  {
    RasterWriter writer = RasterWriter::create( target, 8, 8, { {} }, RasterWriteOptions{ "GTiff", {}, true } );
    RasterWindow full;
    full.width = 8;
    full.height = 8;
    const std::vector<double> values( 64, 5.0 );
    writer.writeWindow( 1, full, values.data() );
    writer.finalize();
  }

  // The already-open reader still serves generation A END TO END — its
  // metadata and pixels describe the same open file, never a B/A mix.
  CHECK( reader.metadata().width == 4 );
  const std::vector<double> aPixels = reader.readWindow( { 1 }, { 0, 0, 4, 4 } );
  REQUIRE( aPixels.size() == 16 );
  for ( const double pixel : aPixels )
    CHECK( pixel == 9.0 );

  // A fresh open observes generation B.
  RasterReader fresh = RasterReader::open( target );
  CHECK( fresh.metadata().width == 8 );
  const std::vector<double> bPixels = fresh.readWindow( { 1 }, { 0, 0, 8, 8 } );
  REQUIRE( bPixels.size() == 64 );
  for ( const double pixel : bPixels )
    CHECK( pixel == 5.0 );
}
#endif
