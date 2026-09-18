/***************************************************************************
  tests/test_io_http_fetch.cpp — strict HTTP status → typed GeoError
  contract at the ADR 0139 choke point (issue #1034 regression):
    * strict mode (httpFetch/httpFetchJson): 404/410 → NotFound, every
      other status >= 400 → NetworkError carrying the status
    * a JSON error document is NEVER handed back as a fetched document
    * httpFetchStatus stays non-strict: raw statuses surface for the
      revalidation callers that branch on them (validator, range cache)
    * a valid 2xx JSON answer keeps working end to end
 ***************************************************************************/

#include "geospatial/remote/http_fetch.h"
#include "geospatial/stac/stac_client.h"
#include "support/http_stac_server.h"
#include "support/offline_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <exception>
#include <string>

using namespace sicnu::geo;
using sicnu::geo::testsupport::HttpStacServer;
using sicnu::geo::testsupport::StacRoute;

// ADR 0146: never die by timeout when the loopback transport is missing —
// report `sicnu-skip: <reason>` + exit 77 instead.
SICNU_OFFLINE_GUARD()

namespace
{

struct StrictCase
{
  int status;
  ErrorCode expected;
};

/// Every error row answers a STAC-shaped JSON error document: with the
/// strict gate broken, httpFetchJson parses and RETURNS it — the exact
/// silent-success shape #1034 describes.
const StrictCase kStrictCases[] = {
  { 400, ErrorCode::NetworkError },
  { 401, ErrorCode::NetworkError },
  { 403, ErrorCode::NetworkError },
  { 404, ErrorCode::NotFound },
  { 410, ErrorCode::NotFound },
  { 429, ErrorCode::NetworkError },
  { 500, ErrorCode::NetworkError },
  { 503, ErrorCode::NetworkError },
};

std::string errorBody( int status )
{
  return "{\"code\": \"Error" + std::to_string( status ) +
         "\", \"description\": \"fixture error answer\"}";
}

StacRoute errorRoute( int status )
{
  StacRoute route;
  route.status = status;
  route.contentType = "application/json";
  route.body = errorBody( status );
  return route;
}

void registerErrorRoutes( HttpStacServer &server )
{
  for ( const StrictCase &row : kStrictCases )
    server.setRoute( "/status/" + std::to_string( row.status ), errorRoute( row.status ) );
}

HttpFetchOptions fastOptions()
{
  HttpFetchOptions options;
  options.maxRetries = 0; // deterministic single attempt per fetch
  return options;
}

/// Runs @p fn, which must throw a GeoError; returns it for code/details
/// assertions. Completing normally — or throwing anything else — fails the
/// test with the row context recorded by CAPTURE.
template <typename Fn>
GeoError expectGeoError( Fn &&fn )
{
  try
  {
    fn();
  }
  catch ( const GeoError &error )
  {
    return error;
  }
  catch ( const std::exception &error )
  {
    FAIL( "expected GeoError, got a different exception: " << error.what() );
  }
  FAIL( "expected GeoError, call completed without throwing" );
  return GeoError( ErrorCode::InvalidArgument, "unreachable" );
}

} // namespace

TEST_CASE( "strict fetch maps every HTTP error status to a typed GeoError",
           "[io][remote][http_fetch][strict]" )
{
  HttpStacServer server;
  REQUIRE( server.port() > 0 );
  registerErrorRoutes( server );

  for ( const StrictCase &row : kStrictCases )
  {
    CAPTURE( row.status );
    const std::string url = server.url() + "/status/" + std::to_string( row.status );

    const GeoError fetchError =
      expectGeoError( [&] { ( void ) httpFetch( url, fastOptions() ); } );
    CHECK( fetchError.code() == row.expected );
    if ( row.expected == ErrorCode::NetworkError )
      CHECK( fetchError.details()["status"].asInt() == row.status );

    // The error route serves a JSON document: a JSON error page must still
    // surface as a typed error, never as a successfully parsed document.
    const GeoError jsonError =
      expectGeoError( [&] { ( void ) httpFetchJson( url, fastOptions() ); } );
    CHECK( jsonError.code() == row.expected );
  }
}

TEST_CASE( "httpFetchStatus surfaces error statuses without throwing",
           "[io][remote][http_fetch][revalidate]" )
{
  HttpStacServer server;
  REQUIRE( server.port() > 0 );
  registerErrorRoutes( server );

  for ( const StrictCase &row : kStrictCases )
  {
    CAPTURE( row.status );
    const std::string url = server.url() + "/status/" + std::to_string( row.status );
    HttpFetchResult result;
    REQUIRE_NOTHROW( result = httpFetchStatus( url, fastOptions() ) );
    CHECK( result.httpStatus == row.status );
  }
}

TEST_CASE( "valid 2xx answers still parse end to end",
           "[io][remote][http_fetch]" )
{
  HttpStacServer server;
  REQUIRE( server.port() > 0 );
  StacRoute route;
  route.body = "{\"type\": \"FeatureCollection\", \"features\": []}";
  server.setRoute( "/search", route );

  const HttpFetchResult fetched = httpFetch( server.url() + "/search", fastOptions() );
  CHECK( fetched.reachedServer );
  CHECK( !fetched.body.empty() );

  const Json::Value document = httpFetchJson( server.url() + "/search", fastOptions() );
  CHECK( document["type"].asString() == "FeatureCollection" );
  CHECK( document["features"].isArray() );
}

TEST_CASE( "a non-JSON error page is still a NetworkError, not a media-type error",
           "[io][remote][http_fetch][strict]" )
{
  HttpStacServer server;
  REQUIRE( server.port() > 0 );
  // A proxy/gateway-style HTML error page: the status classification must
  // win over the media-type gate (typed NetworkError, never UnsupportedFormat).
  StacRoute route;
  route.status = 503;
  route.contentType = "text/html";
  route.body = "<html><body>temporarily unavailable</body></html>";
  server.setRoute( "/html503", route );

  const GeoError error = expectGeoError( [&] {
    ( void ) httpFetchJson( server.url() + "/html503", fastOptions() );
  } );
  CHECK( error.code() == ErrorCode::NetworkError );
  CHECK( error.details()["status"].asInt() == 503 );
}

TEST_CASE( "STAC client surfaces typed errors on HTTP failure",
           "[io][remote][http_fetch][stac]" )
{
  HttpStacServer server;
  REQUIRE( server.port() > 0 );
  server.setRoute( "/search", errorRoute( 503 ) );
  server.setRoute( "/collections/gone-collection", errorRoute( 404 ) );

  StacClient client( server.url() );
  const GeoError searchError = expectGeoError( [&] {
    StacSearchQuery query;
    ( void ) client.search( query );
  } );
  CHECK( searchError.code() == ErrorCode::NetworkError );

  const GeoError collectionError = expectGeoError( [&] {
    ( void ) client.collection( "gone-collection" );
  } );
  CHECK( collectionError.code() == ErrorCode::NotFound );
}
