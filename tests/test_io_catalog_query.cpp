/***************************************************************************
  tests/test_io_catalog_query.cpp — 9.0 M7: non-UI catalog query primitives.
  Pure in-memory engine over detached records: predicates, temporal/spatial
  filters, stable ordering, bounded pagination, summaries, adapters.
 ***************************************************************************/

#include "geospatial/catalog/asset_query.h"
#include "geospatial/stac/stac_mapper.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <string>
#include <vector>

using namespace sicnu::geo;

namespace
{

AssetRecord recordWith( const std::string &id, const std::string &datetimeUtc,
                        const std::string &collection = "scenes" )
{
  AssetRecord record;
  record.id = id;
  record.collection = collection;
  record.datetimeUtc = datetimeUtc;
  record.mediaType = "image/tiff";
  record.roles = { "data" };
  record.hasBbox = true;
  record.minX = 100.0;
  record.minY = 20.0;
  record.maxX = 101.0;
  record.maxY = 21.0;
  return record;
}

} // namespace

TEST_CASE( "asset query predicates filter by id, collection, role, media type and metadata",
           "[io][catalog][fabric9]" )
{
  std::vector<AssetRecord> records = {
    recordWith( "a", "2026-01-01T00:00:00Z" ),
    recordWith( "b", "2026-02-01T00:00:00Z", "other" ),
    recordWith( "c", "2026-03-01T00:00:00Z" ),
  };
  records[1].mediaType = "image/jpeg";
  records[1].roles = { "overview" };
  records[2].metadata["platform"] = "sentinel-2";

  AssetQuery query;
  query.collectionEquals = "other";
  CHECK( queryAssets( records, query ).totalMatches == 1 );

  AssetQuery roleQuery;
  roleQuery.roleEquals = "overview";
  CHECK( queryAssets( records, roleQuery ).totalMatches == 1 );
  CHECK( queryAssets( records, roleQuery ).records[0].id == "b" );

  AssetQuery mediaQuery;
  mediaQuery.mediaTypeSubstring = "TIFF"; // case-insensitive containment
  CHECK( queryAssets( records, mediaQuery ).totalMatches == 2 );

  AssetQuery metadataQuery;
  metadataQuery.metadataKeyEquals = "platform";
  metadataQuery.metadataValueEquals = "sentinel-2";
  CHECK( queryAssets( records, metadataQuery ).records[0].id == "c" );

  AssetQuery absentMetadata;
  absentMetadata.metadataKeyEquals = "platform";
  absentMetadata.metadataValueEquals = "landsat-8";
  CHECK( queryAssets( records, absentMetadata ).totalMatches == 0 );
}

TEST_CASE( "temporal filters are instant-based and honest about undated records",
           "[io][catalog][fabric9][temporal]" )
{
  std::vector<AssetRecord> records = {
    recordWith( "jan", "2026-01-15T00:00:00Z" ),
    recordWith( "feb", "2026-02-15T00:00:00Z" ),
    recordWith( "mar", "2026-03-15T00:00:00Z" ),
  };
  AssetRecord undated = recordWith( "undated", "" );
  records.push_back( undated );

  AssetQuery window;
  window.temporalStartUtc = "2026-01-31T00:00:00Z";
  window.temporalEndUtc = "2026-03-01T00:00:00Z";
  const AssetQueryPage page = queryAssets( records, window );
  CHECK( page.totalMatches == 1 );
  CHECK( page.records[0].id == "feb" );
  // The undated record never sneaks into a temporal filter.
  CHECK( std::none_of( page.records.begin(), page.records.end(),
                       []( const AssetRecord &r ) { return r.id == "undated"; } ) );

  // Open-ended bounds.
  AssetQuery openStart;
  openStart.temporalStartUtc = "2026-02-10T00:00:00Z";
  CHECK( queryAssets( records, openStart ).totalMatches == 2 );

  // Malformed bounds are typed, before any work.
  AssetQuery malformed;
  malformed.temporalStartUtc = "not-a-time";
  CHECK_THROWS_AS( queryAssets( records, malformed ), GeoError );
}

TEST_CASE( "spatial filters intersect inclusively and refuse bbox-less records",
           "[io][catalog][fabric9][spatial]" )
{
  std::vector<AssetRecord> records = { recordWith( "inside", "" ), recordWith( "far", "" ) };
  records[1].minX = 0.0;
  records[1].minY = 0.0;
  records[1].maxX = 1.0;
  records[1].maxY = 1.0;

  AssetQuery query;
  query.requiresBbox = true;
  query.minX = 100.5;
  query.minY = 20.5;
  query.maxX = 200.5;
  query.maxY = 120.5;
  const AssetQueryPage page = queryAssets( records, query );
  CHECK( page.totalMatches == 1 );
  CHECK( page.records[0].id == "inside" );

  // A record without any bbox never matches a spatial filter.
  AssetRecord bboxLess = recordWith( "nobox", "" );
  bboxLess.hasBbox = false;
  records.push_back( bboxLess );
  CHECK( queryAssets( records, query ).totalMatches == 1 );

  // Malformed bbox is a typed error.
  AssetQuery malformed = query;
  malformed.minX = 10.0;
  malformed.maxX = 5.0;
  CHECK_THROWS_AS( queryAssets( records, malformed ), GeoError );
}

TEST_CASE( "pagination is bounded, stable and honest about continuation",
           "[io][catalog][fabric9][paging]" )
{
  std::vector<AssetRecord> records;
  for ( int i = 0; i < 250; ++i )
  {
    const int second = i % 60;
    records.push_back( recordWith( "id-" + std::to_string( i ),
                                   std::string( "2026-01-01T00:00:" ) + ( second < 10 ? "0" : "" )
                                     + std::to_string( second ) + "Z" ) );
  }

  AssetQuery query;
  AssetQueryOptions options;
  options.limit = 100;
  query.sortByTemporal = "asc";

  const AssetQueryPage first = queryAssets( records, query, options );
  CHECK( first.records.size() == 100 );
  CHECK( first.totalMatches == 250 );
  CHECK( first.hasMore );
  CHECK( first.records[0].id == "id-0" );

  AssetQueryOptions secondOptions = options;
  secondOptions.offset = 100;
  const AssetQueryPage second = queryAssets( records, query, secondOptions );
  CHECK( second.records.size() == 100 );
  // Offset 100 lands mid-group: seconds 0-9 hold 5 records each (i, i+60,
  // i+120, i+180, i+240 for i<10), seconds 10+ hold 4. 50 records cover
  // seconds 0-9, then 12 full groups of 4 (seconds 10-21) = offset 98, so
  // the page opens with the third record of second 22 (i = 22, 82, 142, 202).
  CHECK( second.records[0].id == "id-142" );

  AssetQueryOptions tailOptions = options;
  tailOptions.offset = 200;
  const AssetQueryPage tail = queryAssets( records, query, tailOptions );
  CHECK( tail.records.size() == 50 );
  CHECK_FALSE( tail.hasMore );

  // Determinism: identical query ⇒ identical page (stable ordering).
  const AssetQueryPage replay = queryAssets( records, query, options );
  CHECK( replay.records[7].id == first.records[7].id );
}

TEST_CASE( "summaries stay bounded and reports record temporal coverage",
           "[io][catalog][fabric9][summary]" )
{
  std::vector<AssetRecord> records = {
    recordWith( "a", "2026-01-01T12:00:00+05:00" ), // normalized: 07:00Z
    recordWith( "b", "2026-06-01T00:00:00Z" ),
    recordWith( "c", "" ),
  };
  records[0].minX = 0.0;
  records[0].minY = 0.0;
  records[2].maxX = 500.0;
  records[2].maxY = 500.0;

  AssetQuery all;
  const AssetQuerySummary summary = summarizeAssets( records, all );
  CHECK( summary.totalMatches == 3 );
  CHECK( summary.earliestUtc == "2026-01-01T07:00:00Z" ); // mixed offsets order correctly
  CHECK( summary.latestUtc == "2026-06-01T00:00:00Z" );
  CHECK( summary.withoutDatetime == 1 );
  CHECK( summary.hasSpatialExtent );
  CHECK( summary.maxX == Approx( 500.0 ) );
}

TEST_CASE( "records adapt from STAC items and round-trip through JSON",
           "[io][catalog][fabric9][adapter]" )
{
  const StacItem item = StacItem::parseText( "{"
    "\"type\": \"Feature\", \"stac_version\": \"1.0.0\", \"id\": \"cube-1\","
    "\"properties\": {\"datetime\": \"2026-03-01T02:00:00+02:00\", \"eo:cloud_cover\": 12.5, \"platform\": \"s2\"},"
    "\"bbox\": [10.0, 40.0, 11.0, 41.0],"
    "\"assets\": {\"image\": {\"href\": \"https://example.test/cube-1.tif\","
    " \"type\": \"image/tiff\", \"roles\": [\"data\"]}}}" );
  const AssetRecord record = assetRecordFromStacItem( item, "https://example.test/cube-1.tif" );
  CHECK( record.id == "cube-1" );
  CHECK( record.datetimeUtc == "2026-03-01T00:00:00Z" ); // normalized on the record
  CHECK( record.mediaType == "image/tiff" );
  CHECK( record.hasCloudCover );
  CHECK( record.cloudCover == Approx( 12.5 ) );

  // JSON round-trip: record → JSON → record equality (semantic).
  const Json::Value json = record.toJson();
  const AssetRecord restored = AssetRecord::fromJson( json );
  CHECK( restored.toJson() == json );

  // An id-less record is a typed violation.
  Json::Value broken = json;
  broken["id"] = "";
  CHECK_THROWS_AS( AssetRecord::fromJson( broken ), GeoError );
}

TEST_CASE( "6-value STAC bboxes map the horizontal extent correctly (review)",
           "[io][catalog][fabric9][adapter][bbox3d]" )
{
  // STAC bbox [w, s, minZ, e, n, maxZ] — the horizontal extent lives at
  // indices 0/1/3/4. The old indexing produced maxX = minZ.
  const StacItem item = StacItem::parseText( "{"
    "\"type\": \"Feature\", \"stac_version\": \"1.0.0\", \"id\": \"cube-3d\","
    "\"properties\": {\"datetime\": \"2026-03-01T00:00:00Z\"},"
    "\"bbox\": [10.0, 40.0, -50.0, 11.0, 41.0, -40.0],"
    "\"assets\": {\"image\": {\"href\": \"https://example.test/c.tif\", \"roles\": [\"data\"]}}}" );
  const AssetRecord record = assetRecordFromStacItem( item, "https://example.test/c.tif" );
  REQUIRE( record.hasBbox );
  CHECK( record.minX == Approx( 10.0 ) );
  CHECK( record.minY == Approx( 40.0 ) );
  CHECK( record.maxX == Approx( 11.0 ) ); // was -50 (minZ) with the old indexing
  CHECK( record.maxY == Approx( 41.0 ) ); // was 11 (east) with the old indexing
}

TEST_CASE( "100k-record queries stay bounded (M9 scale case)",
           "[io][catalog][fabric9][scale]" )
{
  std::vector<AssetRecord> records;
  records.reserve( 100000 );
  for ( int i = 0; i < 100000; ++i )
  {
    AssetRecord record = recordWith( "asset-" + std::to_string( i ), "2026-05-01T00:00:00Z" );
    record.metadata["tile"] = std::to_string( i % 1000 );
    records.push_back( std::move( record ) );
  }

  AssetQuery query;
  query.metadataKeyEquals = "tile";
  query.metadataValueEquals = "7";
  AssetQueryOptions options;
  options.limit = 25;
  const AssetQueryPage page = queryAssets( records, query, options );
  CHECK( page.totalMatches == 100 );
  CHECK( page.records.size() == 25 );
  CHECK( page.hasMore );

  // The summary pass materializes nothing beyond the aggregate.
  const AssetQuerySummary summary = summarizeAssets( records, AssetQuery{} );
  CHECK( summary.totalMatches == 100000 );
}

TEST_CASE( "the hard cap is a typed backstop in count-off mode (review)",
           "[io][catalog][fabric9][cap]" )
{
  std::vector<AssetRecord> records;
  for ( int i = 0; i < 50; ++i )
    records.push_back( recordWith( "c-" + std::to_string( i ), "2026-01-01T00:00:00Z" ) );

  AssetQuery query;
  AssetQueryOptions options;
  options.limit = 10;
  options.hardCap = 10;
  options.countMatches = false; // caller skipped the full count: the cap guards it

  bool threw = false;
  try
  {
    queryAssets( records, query, options );
  }
  catch ( const GeoError &error )
  {
    threw = true;
    CHECK( error.code() == sicnu::geo::ErrorCode::ResourceExhausted );
  }
  CHECK( threw );

  // With the count enabled, a small page still answers (the cap only
  // backstops the count-off mode).
  options.countMatches = true;
  const AssetQueryPage page = queryAssets( records, query, options );
  CHECK( page.records.size() == 10 );
  CHECK( page.totalMatches == 50 );
}
