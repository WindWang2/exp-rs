/***************************************************************************
  tests/test_io_stac_client.cpp — Cloud-Native Geospatial I/O 7.0 (M3):
  STAC API client proven against a loopback fixture API:
    * search encodes bbox/datetime/collections/ids/limit into GET params
    * the query extension switches to POST with a JSON body
    * pagination follows rel="next" (GET token pages and POST bodies)
    * searchAll is bounded by maxItems and reports truncation truthfully
    * client-side filters: asset roles, media type, cloud cover
    * asset hrefs are credential-safe on every display surface
 ***************************************************************************/

#include "geospatial/stac/stac_client.h"
#include "support/http_stac_server.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <json/json.h>

#include <sstream>
#include <string>
#include <vector>

using namespace sicnu::geo;
using sicnu::geo::testsupport::HttpStacServer;
using Catch::Matchers::ContainsSubstring;

namespace
{

std::string renderItem( const std::string &id, const std::string &datetime,
                        const std::string &cloudCoverJson, const std::string &bbox )
{
  std::string item = "{";
  item += "\"type\": \"Feature\",";
  item += "\"stac_version\": \"1.0.0\",";
  item += "\"id\": \"" + id + "\",";
  item += "\"properties\": {";
  item += "  \"datetime\": \"" + datetime + "\"";
  if ( !cloudCoverJson.empty() )
    item += ", \"eo:cloud_cover\": " + cloudCoverJson;
  item += ", \"platform\": \"sentinel-2\"";
  item += "},";
  item += "\"bbox\": " + bbox + ",";
  item += "\"geometry\": {\"type\": \"Polygon\", \"coordinates\": [[[10,40],[11,40],[11,41],[10,41],[10,40]]]},";
  item += "\"assets\": {";
  item += "  \"image\": {\"href\": \"https://data.example.com/" + id + ".tif\","
          " \"type\": \"image/tiff\", \"roles\": [\"data\"]},";
  item += "  \"thumb\": {\"href\": \"https://data.example.com/" + id + ".jpg\","
          " \"type\": \"image/jpeg\", \"roles\": [\"thumbnail\"]}";
  item += "}";
  item += "}";
  return item;
}

std::string renderPage( const std::vector<std::string> &items, const std::string &nextHref )
{
  std::string page = "{\"type\": \"FeatureCollection\", \"features\": [";
  for ( std::size_t i = 0; i < items.size(); ++i )
  {
    if ( i > 0 )
      page += ",";
    page += items[i];
  }
  page += "], \"links\": [";
  if ( !nextHref.empty() )
    page += "{\"rel\": \"next\", \"href\": \"" + nextHref + "\", \"method\": \"GET\"}";
  page += "]}";
  return page;
}

} // namespace

TEST_CASE( "StacClient search encodes core filters as GET query params",
           "[io][stac][client]" )
{
  HttpStacServer server;
  REQUIRE( server.port() > 0 );
  const std::string body = renderPage(
    { renderItem( "s1", "2026-08-01T10:00:00Z", "12.5", "[10.0,40.0,11.0,41.0]" ) }, "" );
  testsupport::StacRoute route;
  route.body = body;
  server.setRoute( "/search", route );

  StacClient client( server.url() );
  StacSearchQuery query;
  query.bbox = { 10.0, 40.0, 11.0, 41.0 };
  query.datetime = "2026-08-01T00:00:00Z/2026-08-31T23:59:59Z";
  query.collections = { "sentinel-2-l2a" };
  query.limit = 25;

  const StacPage page = client.search( query );
  REQUIRE( page.items.size() == 1 );
  CHECK( page.items[0].id == "s1" );
  CHECK( page.items[0].hasCloudCover );
  CHECK( page.items[0].cloudCover == 12.5 );
  CHECK( !page.hasMore() );

  // The request line must carry the encoded parameters.
  const auto requests = server.requests();
  REQUIRE( requests.size() == 1 );
  CHECK( requests[0].method == "GET" );
  CHECK( requests[0].path == "/search" );
  CHECK_THAT( requests[0].query, ContainsSubstring( "bbox=10%2C40%2C11%2C41" ) );
  CHECK_THAT( requests[0].query, ContainsSubstring( "datetime=2026-08-01T00" ) );
  CHECK_THAT( requests[0].query, ContainsSubstring( "collections=sentinel-2-l2a" ) );
  CHECK_THAT( requests[0].query, ContainsSubstring( "limit=25" ) );
}

TEST_CASE( "StacClient uses POST for the query extension and intersects",
           "[io][stac][client]" )
{
  HttpStacServer server;
  const std::string body = renderPage(
    { renderItem( "s2", "2026-08-02T10:00:00Z", "5", "[10.0,40.0,11.0,41.0]" ) }, "" );
  testsupport::StacRoute route;
  route.body = body;
  server.setRoute( "/search", route );

  StacClient client( server.url() );
  StacSearchQuery query;
  query.query = Json::Value( Json::objectValue );
  query.query["eo:cloud_cover"] = Json::Value( Json::objectValue );
  query.query["eo:cloud_cover"]["lt"] = 10.0;

  const StacPage page = client.search( query );
  REQUIRE( page.items.size() == 1 );
  const auto requests = server.requests();
  REQUIRE( requests.size() == 1 );
  CHECK( requests[0].method == "POST" );
  CHECK_THAT( requests[0].body, ContainsSubstring( "\"eo:cloud_cover\"" ) );
  CHECK_THAT( requests[0].body, ContainsSubstring( "\"lt\"" ) );
}

TEST_CASE( "pagination follows rel=next GET links and stays bounded",
           "[io][stac][client][pagination]" )
{
  HttpStacServer server;
  const std::string page1 = renderPage(
    { renderItem( "a", "2026-08-01T10:00:00Z", "1", "[10,40,11,41]" ),
      renderItem( "b", "2026-08-02T10:00:00Z", "2", "[10,40,11,41]" ) },
    server.url() + "/search?page=2" );
  const std::string page2 = renderPage(
    { renderItem( "c", "2026-08-03T10:00:00Z", "3", "[10,40,11,41]" ) },
    server.url() + "/search?page=3" );
  const std::string page3 = renderPage(
    { renderItem( "d", "2026-08-04T10:00:00Z", "4", "[10,40,11,41]" ) }, "" );
  testsupport::StacRoute r1; r1.body = page1;
  testsupport::StacRoute r2; r2.body = page2;
  testsupport::StacRoute r3; r3.body = page3;
  server.setRoute( "/search", r1 );
  server.setRoute( "/search?page=2", r2 );
  server.setRoute( "/search?page=3", r3 );

  StacClient client( server.url() );
  const StacSearchAllResult all = client.searchAll( StacSearchQuery{} );
  CHECK_FALSE( all.truncatedByLimit );
  REQUIRE( all.items.size() == 4 );
  CHECK( all.items[0].id == "a" );
  CHECK( all.items[3].id == "d" );

  // Explicit page walking matches.
  StacPage first = client.search( StacSearchQuery{} );
  REQUIRE( first.items.size() == 2 );
  StacPage second = client.nextPage( first );
  REQUIRE( second.items.size() == 1 );
  CHECK( second.items[0].id == "c" );
  StacPage third = client.nextPage( second );
  REQUIRE( third.items.size() == 1 );
  CHECK_FALSE( third.hasMore() );
  CHECK( client.nextPage( third ).items.empty() );
}

TEST_CASE( "searchAll reports truncation truthfully at maxItems",
           "[io][stac][client][pagination]" )
{
  HttpStacServer server;
  const std::string page1 = renderPage(
    { renderItem( "a", "2026-08-01T10:00:00Z", "1", "[10,40,11,41]" ) },
    server.url() + "/search?page=2" );
  const std::string page2 = renderPage(
    { renderItem( "b", "2026-08-02T10:00:00Z", "1", "[10,40,11,41]" ) },
    server.url() + "/search?page=3" );
  const std::string page3 = renderPage(
    { renderItem( "c", "2026-08-03T10:00:00Z", "1", "[10,40,11,41]" ) }, "" );
  testsupport::StacRoute r1; r1.body = page1;
  testsupport::StacRoute r2; r2.body = page2;
  testsupport::StacRoute r3; r3.body = page3;
  server.setRoute( "/search", r1 );
  server.setRoute( "/search?page=2", r2 );
  server.setRoute( "/search?page=3", r3 );

  StacClientOptions options;
  options.maxItems = 2;
  StacClient client( server.url(), options );
  const StacSearchAllResult all = client.searchAll( StacSearchQuery{} );
  CHECK( all.truncatedByLimit );
  REQUIRE( all.items.size() == 2 );
}

TEST_CASE( "collections surface and collection item pages",
           "[io][stac][client]" )
{
  HttpStacServer server;
  testsupport::StacRoute collectionsRoute;
  collectionsRoute.body =
    "{\"collections\": [{\"id\": \"sentinel-2-l2a\", \"type\": \"Collection\"}], \"links\": []}";
  server.setRoute( "/collections", collectionsRoute );
  testsupport::StacRoute collectionRoute;
  collectionRoute.body = "{\"id\": \"sentinel-2-l2a\", \"type\": \"Collection\"}";
  server.setRoute( "/collections/sentinel-2-l2a", collectionRoute );
  testsupport::StacRoute itemsRoute;
  itemsRoute.body = renderPage(
    { renderItem( "s1", "2026-08-01T10:00:00Z", "12.5", "[10,40,11,41]" ) }, "" );
  server.setRoute( "/collections/sentinel-2-l2a/items", itemsRoute );

  StacClient client( server.url() );
  const Json::Value collections = client.collections();
  CHECK( collections["collections"][0]["id"].asString() == "sentinel-2-l2a" );
  CHECK( client.collection( "sentinel-2-l2a" )["type"].asString() == "Collection" );

  StacSearchQuery query;
  query.datetime = "2026-08-01T00:00:00Z/..";
  query.limit = 10;
  const StacPage items = client.collectionItems( "sentinel-2-l2a", query );
  REQUIRE( items.items.size() == 1 );
  const auto requests = server.requests();
  CHECK( requests.back().path == "/collections/sentinel-2-l2a/items" );
  CHECK_THAT( requests.back().query, ContainsSubstring( "datetime=2026-08-01T00" ) );
  CHECK( requests.back().query.find( "collections=" ) == std::string::npos );
}

TEST_CASE( "client-side filters: roles, media type and cloud cover",
           "[io][stac][client][filters]" )
{
  const std::string itemJson = renderItem( "s1", "2026-08-01T10:00:00Z", "35", "[10,40,11,41]" );
  Json::Value parsed;
  Json::CharReaderBuilder builder;
  std::istringstream stream( itemJson );
  REQUIRE( Json::parseFromStream( builder, stream, &parsed, nullptr ) );
  const StacItem item = StacItem::parse( parsed );

  const std::vector<StacAsset> data = StacClient::assetsWithRole( item, "data" );
  REQUIRE( data.size() == 1 );
  CHECK( data[0].mediaType == "image/tiff" );
  const std::vector<StacAsset> thumbs = StacClient::assetsWithRole( item, "thumbnail" );
  REQUIRE( thumbs.size() == 1 );
  CHECK( StacClient::assetsWithRole( item, "missing" ).empty() );

  CHECK( StacClient::assetsOfMediaType( item, "image/tiff" ).size() == 1 );
  CHECK( StacClient::assetsOfMediaType( item, "jpeg" ).size() == 1 );
  CHECK( StacClient::assetsOfMediaType( item, "image/png" ).empty() );

  CHECK( StacClient::matchesCloudCover( item, 30.0 ) == false );
  CHECK( StacClient::matchesCloudCover( item, 40.0 ) == true );
  // Undeclared cloud cover is absence of evidence, not a match failure.
  StacItem noCloud = item;
  noCloud.hasCloudCover = false;
  CHECK( StacClient::matchesCloudCover( noCloud, 5.0 ) );
}

TEST_CASE( "asset href display is credential-safe",
           "[io][stac][client][redaction]" )
{
  StacAsset signedAsset;
  signedAsset.href = "https://data.example.com/a.tif?X-Amz-Signature=SECRETVALUE&x=1";
  const std::string display = StacClient::displayAssetHref( signedAsset );
  CHECK_THAT( display, !ContainsSubstring( "SECRETVALUE" ) );

  StacAsset userinfo;
  userinfo.href = "https://user:password@data.example.com/a.tif";
  CHECK_THAT( StacClient::displayAssetHref( userinfo ), !ContainsSubstring( "password" ) );
}

TEST_CASE( "temporal series adapter orders items and projects canonical metadata",
           "[io][stac][client][adapter]" )
{
  std::vector<std::string> jsons = {
    renderItem( "later", "2026-08-10T10:00:00Z", "5", "[10,40,11,41]" ),
    renderItem( "earlier", "2026-08-01T10:00:00Z", "5", "[10,40,11,41]" ),
    renderItem( "middle", "2026-08-05T10:00:00Z", "5", "[10,40,11,41]" ),
  };
  std::vector<StacItem> items;
  for ( const std::string &text : jsons )
  {
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::istringstream stream( text );
    REQUIRE( Json::parseFromStream( builder, stream, &parsed, nullptr ) );
    items.push_back( StacItem::parse( parsed ) );
  }
  const std::vector<StacSeriesEntry> series = buildTemporalSeries( items );
  REQUIRE( series.size() == 3 );
  CHECK( series[0].item.id == "earlier" );
  CHECK( series[1].item.id == "middle" );
  CHECK( series[2].item.id == "later" );
  // The canonical projection rides along (sensor/platform vocabulary).
  CHECK( series[0].canonical.platform == "sentinel-2" );
  CHECK( series[0].canonical.acquisitionTime == "2026-08-01T10:00:00Z" );
}

TEST_CASE( "temporal series orders mixed-offset datetimes by instant, with UTC normalization",
           "[io][stac][client][adapter][utc8]" )
{
  // The same acquisition window expressed in three different offsets plus a
  // later item. Raw-string comparison would order these by text; instant
  // comparison must interleave them by actual time.
  std::vector<std::string> jsons = {
    renderItem( "zulu", "2026-08-10T12:30:00Z", "5", "[10,40,11,41]" ),
    renderItem( "plus2", "2026-08-10T16:15:00+02:00", "5", "[10,40,11,41]" ),  // 14:15Z
    renderItem( "minus5", "2026-08-10T09:00:00-05:00", "5", "[10,40,11,41]" ), // 14:00Z
    renderItem( "later", "2026-08-10T20:00:00+02:00", "5", "[10,40,11,41]" ),  // 18:00Z
  };
  std::vector<StacItem> items;
  for ( const std::string &text : jsons )
  {
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::istringstream stream( text );
    REQUIRE( Json::parseFromStream( builder, stream, &parsed, nullptr ) );
    items.push_back( StacItem::parse( parsed ) );
  }
  const StacSeries series = buildTemporalSeriesDetailed( items );
  REQUIRE( series.entries.size() == 4 );
  // Ordered by instant: 12:30Z < 14:00Z < 14:15Z < 18:00Z.
  CHECK( series.entries[0].item.id == "zulu" );
  CHECK( series.entries[1].item.id == "minus5" );
  CHECK( series.entries[2].item.id == "plus2" );
  CHECK( series.entries[3].item.id == "later" );
  // Every entry's normalized UTC form is exposed and differs from the
  // verbatim source where the origin declared an offset.
  CHECK( series.entries[0].item.datetimeUtc == "2026-08-10T12:30:00Z" );
  CHECK( series.entries[1].item.datetimeUtc == "2026-08-10T14:00:00Z" );
  CHECK( series.entries[2].item.datetimeUtc == "2026-08-10T14:15:00Z" );
  CHECK( series.entries[2].item.datetimeNormalized );
  // No duplicate instants in this set.
  CHECK( series.duplicateEntryIndices.empty() );
}

TEST_CASE( "duplicate acquisitions keep deterministic order and are reported",
           "[io][stac][client][adapter][utc8]" )
{
  // Two items claiming the SAME instant (same acquisition, duplicate
  // publication) plus an unrelated third. Nothing is dropped; the duplicates
  // tie-break by item id; the second occurrence is reported.
  std::vector<std::string> jsons = {
    renderItem( "dup-b", "2026-08-10T12:00:00Z", "5", "[10,40,11,41]" ),
    renderItem( "other", "2026-08-11T09:00:00Z", "5", "[10,40,11,41]" ),
    renderItem( "dup-a", "2026-08-10T14:00:00+02:00", "5", "[10,40,11,41]" ), // 12:00Z — same instant as dup-b
  };
  std::vector<StacItem> items;
  for ( const std::string &text : jsons )
  {
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::istringstream stream( text );
    REQUIRE( Json::parseFromStream( builder, stream, &parsed, nullptr ) );
    items.push_back( StacItem::parse( parsed ) );
  }
  const StacSeries series = buildTemporalSeriesDetailed( items );
  REQUIRE( series.entries.size() == 3 );
  CHECK( series.entries[0].item.id == "dup-a" ); // earlier instant; id tie-break within the pair
  CHECK( series.entries[1].item.id == "dup-b" );
  CHECK( series.entries[2].item.id == "other" );
  REQUIRE( series.duplicateEntryIndices.size() == 1 );
  CHECK( series.duplicateEntryIndices[0] == 1 ); // dup-b is the second occurrence
}

TEST_CASE( "item UTC normalization flags assumed-UTC naive datetimes",
           "[io][stac][client][adapter][utc8]" )
{
  Json::Value parsed;
  Json::CharReaderBuilder builder;
  std::istringstream stream( renderItem( "naive", "2026-08-10T12:30:00", "5", "[10,40,11,41]" ) );
  REQUIRE( Json::parseFromStream( builder, stream, &parsed, nullptr ) );
  const StacItem item = StacItem::parse( parsed );
  // STAC's documented reading: offset-less datetimes are UTC — applied and
  // flagged, never silently treated as declared.
  CHECK( item.datetimeUtc == "2026-08-10T12:30:00Z" );
  CHECK( item.datetimeAssumedUtc );
  CHECK( item.datetimeNormalized );
  // toJson() stays the origin's verbatim wire form.
  CHECK( item.toJson()["properties"]["datetime"].asString() == "2026-08-10T12:30:00" );
}

TEST_CASE( "client contract violations are typed before the network",
           "[io][stac][client]" )
{
  CHECK_THROWS_AS( StacClient( "C:/local/root" ), GeoError );
  StacClient client( "http://127.0.0.1:1" ); // offline root: construction is fine
  StacSearchQuery badBbox;
  badBbox.bbox = { 1.0, 2.0, 3.0 };
  CHECK_THROWS_AS( client.search( badBbox ), GeoError );
  Json::Value notGeometry( Json::arrayValue );
  StacSearchQuery badIntersects;
  badIntersects.intersects = notGeometry;
  CHECK_THROWS_AS( client.search( badIntersects ), GeoError );
  CHECK_THROWS_AS( client.collection( "" ), GeoError );
}

TEST_CASE( "POST pagination merges the continuation into the original body",
           "[io][stac][client][pagination][merge]" )
{
  HttpStacServer server;
  const std::string page1 =
    "{\"type\": \"FeatureCollection\", \"features\": [" +
    renderItem( "m1", "2026-08-01T10:00:00Z", "1", "[10,40,11,41]" ) +
    "], \"links\": [{\"rel\": \"next\", \"href\": \"" + server.url() +
    "/search?page=2\", \"method\": \"POST\", \"merge\": true, \"body\": {\"page\": 2}}]}";
  const std::string page2 =
    "{\"type\": \"FeatureCollection\", \"features\": [" +
    renderItem( "m2", "2026-08-02T10:00:00Z", "1", "[10,40,11,41]" ) +
    "], \"links\": []}";
  testsupport::StacRoute r1;
  r1.body = page1;
  testsupport::StacRoute r2;
  r2.body = page2;
  server.setRoute( "/search", r1 );
  server.setRoute( "/search?page=2", r2 );

  StacClient client( server.url() );
  StacSearchQuery query;
  query.bbox = { 10.0, 40.0, 11.0, 41.0 };
  query.datetime = "2026-08-01T00:00:00Z/..";
  const StacSearchAllResult all = client.searchAll( query );
  REQUIRE( all.items.size() == 2 );

  // Page 2's POST body must carry the ORIGINAL filters (merged delta), not
  // the bare continuation — an unfiltered page-2 would silently widen the
  // query.
  const auto requests = server.requests();
  REQUIRE( requests.size() == 2 );
  CHECK( requests[1].method == "POST" );
  CHECK_THAT( requests[1].body, ContainsSubstring( "\"bbox\"" ) );
  CHECK_THAT( requests[1].body, ContainsSubstring( "\"datetime\"" ) );
  CHECK_THAT( requests[1].body, ContainsSubstring( "\"page\":2" ) );
}

TEST_CASE( "searchAll survives empty-page walkers and detects pagination loops",
           "[io][stac][client][pagination][bounds]" )
{
  HttpStacServer server;
  // An origin that answers EMPTY feature lists with a next link forever.
  const std::string empty =
    "{\"type\": \"FeatureCollection\", \"features\": [], \"links\": "
    "[{\"rel\": \"next\", \"href\": \"next-page\", \"method\": \"GET\"}]}";
  testsupport::StacRoute route;
  route.body = empty;
  server.setRoute( "/search", route );
  server.setRoute( "/next-page", route );

  StacClient client( server.url() );
  CHECK_THROWS_AS( client.searchAll( StacSearchQuery{} ), GeoError );
}
