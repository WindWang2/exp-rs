/***************************************************************************
  tests/test_io_fabric_catalog.cpp — fabric 10.0: unified catalog service.
  Three backends (in-memory records, local STAC files via VSI walk, remote
  STAC API over loopback) behind one query vocabulary: shared filters,
  bounded pagination, cancel, offline typing.
 ***************************************************************************/

#include "geospatial/fabric/catalog_service.h"
#include "geospatial/remote/offline_gate.h"
#include "support/http_stac_server.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "cat_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

void writeFile( const std::string &path, const std::string &content )
{
  std::ofstream out( path, std::ios::binary );
  out << content;
}

AssetRecord recordWith( const std::string &id, const std::string &instantUtc, double cloudCover,
                        bool hasCloudCover, const std::string &platform = "S2A",
                        const std::vector<std::string> &instruments = { "MSI" } )
{
  AssetRecord record;
  record.id = id;
  record.collection = "scenes";
  record.datetimeUtc = instantUtc;
  record.mediaType = "image/tiff";
  record.roles = { "data" };
  record.hasCloudCover = hasCloudCover;
  record.cloudCover = cloudCover;
  record.hasBbox = true;
  record.minX = 8.0;
  record.minY = 50.0;
  record.maxX = 9.0;
  record.maxY = 51.0;
  record.metadata["platform"] = platform;
  record.metadata["instruments"] = [] ( const std::vector<std::string> &values ) {
    std::string joined;
    for ( const std::string &value : values )
    {
      if ( !joined.empty() )
        joined += ",";
      joined += value;
    }
    return joined;
  }( instruments );
  record.path = "/data/" + id + ".tif";
  return record;
}

std::string itemJson( const std::string &id, const std::string &datetime, double cloudCover,
                      const std::string &platform, const std::string &assetHref,
                      const std::string &instrument = "MSI" )
{
  Json::Value item;
  item["type"] = "Feature";
  item["stac_version"] = "1.0.0";
  item["id"] = id;
  item["collection"] = "loop-scenes";
  item["bbox"] = Json::Value( Json::arrayValue );
  item["bbox"].append( 8.0 );
  item["bbox"].append( 50.0 );
  item["bbox"].append( 9.0 );
  item["bbox"].append( 51.0 );
  item["properties"]["datetime"] = datetime;
  item["properties"]["eo:cloud_cover"] = cloudCover;
  item["properties"]["platform"] = platform;
  item["properties"]["instruments"] = Json::Value( Json::arrayValue );
  item["properties"]["instruments"].append( instrument );
  Json::Value assets;
  Json::Value data;
  data["href"] = assetHref;
  data["type"] = "image/tiff";
  Json::Value roles( Json::arrayValue );
  roles.append( "data" );
  data["roles"] = roles;
  assets["image"] = data;
  item["assets"] = assets;
  Json::StreamWriterBuilder builder;
  return Json::writeString( builder, item );
}

} // namespace

TEST_CASE( "catalog query validation refuses malformed shapes before any backend work",
           "[io][fabric][catalog]" )
{
  CatalogQuery ok;
  ok.bbox = { 8.0, 50.0, 9.0, 51.0 };
  ok.temporalStartUtc = "2024-01-01T00:00:00Z";
  ok.temporalEndUtc = "2024-02-01T00:00:00Z";
  CHECK_NOTHROW( ok.validate() );

  CatalogQuery badBbox;
  badBbox.bbox = { 8.0, 50.0, 9.0 };
  REQUIRE_THROWS_AS( badBbox.validate(), GeoError );
  CatalogQuery crossedBbox;
  crossedBbox.bbox = { 9.0, 50.0, 8.0, 51.0 };
  REQUIRE_THROWS_AS( crossedBbox.validate(), GeoError );
  CatalogQuery badInstant;
  badInstant.temporalStartUtc = "2024-01-01";   // date-only is not an instant
  REQUIRE_THROWS_AS( badInstant.validate(), GeoError );
  CatalogQuery badCloud;
  badCloud.hasCloudCoverMax = true;
  badCloud.cloudCoverMax = 150.0;
  REQUIRE_THROWS_AS( badCloud.validate(), GeoError );
}

TEST_CASE( "in-memory backend applies the full filter vocabulary with honest paging",
           "[io][fabric][catalog]" )
{
  std::vector<AssetRecord> records;
  records.push_back( recordWith( "a", "2024-01-10T00:00:00Z", 5.0, true ) );
  records.push_back( recordWith( "b", "2024-02-10T00:00:00Z", 40.0, true, "S2B", { "MSI" } ) );
  records.push_back( recordWith( "c", "2024-03-10T00:00:00Z", 0.0, false, "L8", { "OLI", "TIRS" } ) );
  const CatalogService service = catalogServiceOverRecords( records );
  CHECK( service.info().kind == CatalogBackendKind::InMemoryRecords );

  // Cloud cover: records without declared cover FAIL the filter (absence is
  // not evidence) — "c" drops even though its undeclared cover might be low.
  CatalogQuery cloudQuery;
  cloudQuery.hasCloudCoverMax = true;
  cloudQuery.cloudCoverMax = 20.0;
  const CatalogService::SearchAllResult cloudResult = service.searchAll( cloudQuery );
  REQUIRE( cloudResult.records.size() == 1 );
  CHECK( cloudResult.records[0].id == "a" );

  // Platform filter is case-insensitive equality.
  CatalogQuery platformQuery;
  platformQuery.platformEquals = "s2b";
  const CatalogService::SearchAllResult platformResult = service.searchAll( platformQuery );
  REQUIRE( platformResult.records.size() == 1 );
  CHECK( platformResult.records[0].id == "b" );

  // Instruments: any-of across the comma-joined facts.
  CatalogQuery sensorQuery;
  sensorQuery.sensorInstruments = { "OLI" };
  const CatalogService::SearchAllResult sensorResult = service.searchAll( sensorQuery );
  REQUIRE( sensorResult.records.size() == 1 );
  CHECK( sensorResult.records[0].id == "c" );

  // Temporal window is half-open: start inclusive, end exclusive.
  CatalogQuery temporalQuery;
  temporalQuery.temporalStartUtc = "2024-01-10T00:00:00Z";
  temporalQuery.temporalEndUtc = "2024-02-10T00:00:00Z";
  const CatalogService::SearchAllResult temporalResult = service.searchAll( temporalQuery );
  REQUIRE( temporalResult.records.size() == 1 );
  CHECK( temporalResult.records[0].id == "a" );

  // Undated records fail temporal filters — never silently included.
  AssetRecord undated = recordWith( "u", "", 1.0, true );
  const CatalogService undatedService = catalogServiceOverRecords( { undated } );
  CatalogQuery wantsTemporal;
  wantsTemporal.temporalStartUtc = "2024-01-01T00:00:00Z";
  CHECK( undatedService.searchAll( wantsTemporal ).records.empty() );

  // Pagination: limit 1 with continuation walks the whole set honestly.
  CatalogQuery all;
  CatalogContinuation continuation;
  std::vector<std::string> seen;
  while ( true )
  {
    const CatalogPage page = service.searchPage( all, continuation );
    for ( const AssetRecord &record : page.records )
      seen.push_back( record.id );
    if ( !page.next.hasMore )
      break;
    continuation = page.next;
  }
  REQUIRE( seen.size() == 3 );
  // CatalogQuery.limit=1 keeps pages at one record.
  CatalogQuery limited;
  limited.limit = 1;
  const CatalogPage first = service.searchPage( limited, {} );
  CHECK( first.records.size() == 1 );
  CHECK( first.next.hasMore );
  const Json::Value stats = first.statsJson();
  CHECK( stats["records"].asUInt64() == 1 );
  CHECK( stats["hasMore"].asBool() );
  CHECK( stats["unresolvable"].asUInt64() == 0 );
}

TEST_CASE( "cancellation stops an in-memory crawl with the typed error",
           "[io][fabric][catalog][cancel]" )
{
  std::vector<AssetRecord> records;
  for ( int i = 0; i < 50; ++i )
    records.push_back( recordWith( "r" + std::to_string( i ), "2024-01-10T00:00:00Z", 1.0, true ) );
  const CatalogService service = catalogServiceOverRecords( records );

  CancelToken cancel;
  cancel.cancel();
  CatalogQuery query;
  query.limit = 1;
  REQUIRE_THROWS_AS( service.searchPage( query, {}, cancel ), GeoError );
  bool cancelledCode = false;
  try
  {
    service.searchPage( query, {}, cancel );
  }
  catch ( const GeoError &error )
  {
    cancelledCode = error.code() == ErrorCode::Cancelled;
  }
  CHECK( cancelledCode );
}

TEST_CASE( "local STAC trees serve through the same query vocabulary — link-following and plain scans both",
           "[io][fabric][catalog][local]" )
{
  // --- link-following tree: catalog.json → collection.json → items ------
  const std::string root = scratchDir( "tree" );
  std::filesystem::create_directories( root + "/tile-a" );
  std::filesystem::create_directories( root + "/tile-b" );
  writeFile( root + "/catalog.json", R"({
    "type": "Catalog", "stac_version": "1.0.0", "id": "root",
    "links": [
      {"rel": "child", "href": "tile-a/collection.json"},
      {"rel": "child", "href": "tile-b/collection.json"},
      {"rel": "self", "href": "http://irrelevant.example/catalog.json"}
    ]})" );
  writeFile( root + "/tile-a/collection.json", R"({
    "type": "Collection", "stac_version": "1.0.0", "id": "tile-a",
    "license": "CC0",
    "links": [
      {"rel": "item", "href": "item-1.json"},
      {"rel": "item", "href": "item-2.json"}
    ]})" );
  writeFile( root + "/tile-a/item-1.json",
             itemJson( "item-1", "2024-05-01T10:00:00Z", 3.0, "S2A", "pixels/p1.tif" ) );
  // item-2 uses a sibling-relative href (absolute local paths are REFUSED
  // by the 9.0 containment rule — only remote absolute URLs pass through).
  writeFile( root + "/tile-a/item-2.json",
             itemJson( "item-2", "2024-05-02T10:00:00Z", 60.0, "S2A", "p2.tif" ) );
  // item-3's relative href climbs out of the item's directory — the
  // containment rule refuses it and the item is dropped with its OWN
  // counter (unresolvable), not mixed into query filtering.
  writeFile( root + "/tile-b/item-3.json",
             itemJson( "item-3", "2024-05-03T10:00:00Z", 10.0, "S2A", "../../../escape/p3.tif" ) );
  writeFile( root + "/tile-b/collection.json", R"({
    "type": "Collection", "stac_version": "1.0.0", "id": "tile-b", "license": "CC0",
    "links": [
      {"rel": "child", "href": "../tile-a/collection.json"},
      {"rel": "item", "href": "item-3.json"},
      {"rel": "item", "href": "../tile-a/item-1.json"}
    ]})" );

  const CatalogService service = openCatalogService( root );
  CHECK( service.info().kind == CatalogBackendKind::LocalStacFiles );

  const CatalogService::SearchAllResult all = service.searchAll( {} );
  // Cycle safety: tile-b links back into tile-a; each item lands ONCE, and
  // item-3 (escaping href) drops into the unresolvable counter.
  REQUIRE( all.records.size() == 2 );
  CHECK( all.records[0].id == "item-1" );
  CHECK( all.records[1].id == "item-2" );
  CHECK( all.unresolvable == 1 );
  CHECK( all.clientFilteredOut == 0 );
  // item-1's relative href resolved against the ITEM's directory.
  CHECK( all.records[0].path.find( "tile-a/pixels/p1.tif" ) != std::string::npos );
  CHECK( all.records[1].path.find( "tile-a/p2.tif" ) != std::string::npos );

  // The same filters work here: cloud cover is service-side.
  CatalogQuery cloudy;
  cloudy.hasCloudCoverMax = true;
  cloudy.cloudCoverMax = 20.0;
  const CatalogService::SearchAllResult clearOnly = service.searchAll( cloudy );
  REQUIRE( clearOnly.records.size() == 1 );
  CHECK( clearOnly.records[0].id == "item-1" );
  CHECK( clearOnly.records[0].hasCloudCover );
  CHECK( clearOnly.records[0].cloudCover == 3.0 );

  // Platform + sensor enrichment rode along.
  CatalogQuery bySensor;
  bySensor.sensorInstruments = { "msi" };
  CHECK( service.searchAll( bySensor ).records.size() == 2 );
  CatalogQuery byPlatform;
  byPlatform.platformEquals = "S2A";
  CHECK( service.searchAll( byPlatform ).records.size() == 2 );

  // --- plain directory scan (no catalog.json) --------------------------
  const std::string plain = scratchDir( "plain" );
  writeFile( plain + "/s1.json", itemJson( "s1", "2024-06-01T00:00:00Z", 1.0, "S2A", "bytes/p1.tif" ) );
  writeFile( plain + "/not-an-item.json", R"({"type": "Catalog", "id": "x"})" );
  writeFile( plain + "/broken.json", "{ not json" );
  const CatalogService plainService = openCatalogService( plain );
  const CatalogService::SearchAllResult plainResult = plainService.searchAll( {} );
  // One item survives: stray JSONs and broken files never kill the walk.
  REQUIRE( plainResult.records.size() == 1 );
  CHECK( plainResult.records[0].id == "s1" );

  // Single-file root: one record.
  const CatalogService fileService = openCatalogService( plain + "/s1.json" );
  CHECK( fileService.searchAll( {} ).records.size() == 1 );
}

TEST_CASE( "the remote backend reuses the STAC client and refuses offline by type",
           "[io][fabric][catalog][remote]" )
{
  testsupport::HttpStacServer server;
  const std::string item1 =
    itemJson( "r1", "2024-07-01T00:00:00Z", 12.0, "S2A", "https://assets.example/r1.tif" );
  const std::string item2 =
    itemJson( "r2", "2024-07-02T00:00:00Z", 80.0, "S2B", "https://assets.example/r2.tif" );
  const std::string pageBody = "{\"type\": \"FeatureCollection\", \"features\": [" + item1 + "," +
                               item2 + "], \"links\": []}";
  server.setRoute( "/search", { 200, "application/geo+json", pageBody } );

  // Offline: the remote backend refuses at OPEN (never a network attempt).
  offline::setEnabled( true );
  REQUIRE_THROWS_AS( openCatalogService( server.url() ), GeoError );
  offline::setEnabled( false );

  const CatalogService service = openCatalogService( server.url() );
  CHECK( service.info().kind == CatalogBackendKind::RemoteStacApi );

  const CatalogService::SearchAllResult all = service.searchAll( {} );
  REQUIRE( all.records.size() == 2 );
  CHECK( all.records[0].id == "r1" );
  CHECK( all.records[1].id == "r2" );
  CHECK( all.records[0].hasCloudCover );
  CHECK( all.records[0].cloudCover == 12.0 );
  CHECK( !all.truncatedByCap );

  // Client-side filter narrows on the SAME vocabulary.
  CatalogQuery cloud;
  cloud.hasCloudCoverMax = true;
  cloud.cloudCoverMax = 50.0;
  const CatalogService::SearchAllResult filtered = service.searchAll( cloud );
  REQUIRE( filtered.records.size() == 1 );
  CHECK( filtered.records[0].id == "r1" );

  // maxItems truncation reports honestly.
  CatalogQuery capped;
  capped.maxItems = 1;
  const CatalogService::SearchAllResult truncated = service.searchAll( capped );
  CHECK( truncated.records.size() == 1 );
  CHECK( truncated.truncatedByCap );

  // Offline AFTER open: page fetches refuse too.
  offline::setEnabled( true );
  CatalogQuery query;
  REQUIRE_THROWS_AS( service.searchPage( query, {} ), GeoError );
  offline::setEnabled( false );
}

TEST_CASE( "invalid roots and unparsable filters are typed, never guessed",
           "[io][fabric][catalog]" )
{
  REQUIRE_THROWS_AS( openCatalogService( "s3://bucket/without-classification" ), GeoError );
  REQUIRE_THROWS_AS( openCatalogService( "/definitely/not/here/at/all" ), GeoError );

  const CatalogService service = catalogServiceOverRecords( {} );
  CatalogQuery bad;
  bad.temporalEndUtc = "yesterday";
  REQUIRE_THROWS_AS( service.searchAll( bad ), GeoError );
}
