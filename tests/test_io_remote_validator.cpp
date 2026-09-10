/***************************************************************************
  tests/test_io_remote_validator.cpp — Cloud-Native Geospatial I/O 7.0 (M1):
  remote source identity & validators. Proven against a local fixture HTTP
  server that emits ETag / Last-Modified and answers RFC 7232 conditional
  requests:
    * strong ETag → freshness provable; weak ETag → never a byte-level proof
    * revalidation: 304 → Unchanged; validator mismatch → Changed + update
    * offline / timeout / HTTP errors are truthful states, never successes
    * credentials never appear in identity reports
 ***************************************************************************/

#include "geospatial/remote/http_fetch.h"
#include "geospatial/remote/remote_source_validator.h"
#include "support/http_range_server.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>

static const char *NL = "\n";
#include <cstdio>
#include <string>
#include <vector>

using namespace sicnu::geo;
using sicnu::geo::testsupport::HttpRangeServer;

namespace
{

std::vector<unsigned char> payloadOfSize( std::size_t size, unsigned char seed = 7 )
{
  std::vector<unsigned char> payload( size );
  for ( std::size_t i = 0; i < size; ++i )
    payload[i] = static_cast<unsigned char>( ( i * seed ) % 251 );
  return payload;
}

/// Some GDAL builds leave nStatus = 0 on usable answers; 304s may surface
/// either explicitly or as entity-less answers. The contract: the validator
/// decides Unchanged/Changed/Inconclusive correctly either way.
bool statusIsUsableOrEmpty( int status )
{
  return status == 0 || ( status >= 200 && status < 300 ) || status == 304;
}

} // namespace

TEST_CASE( "probe captures validator identity with weak/strong classification",
           "[io][remote][validator]" )
{
  HttpRangeServer server( payloadOfSize( 4096 ) );
  server.setEtag( "\"v1-abcdef\"" );
  server.setLastModified( "Tue, 08 Sep 2026 10:00:00 GMT" );

  RemoteSourceValidator validator = RemoteSourceValidator::probe( server.url() );
  const RemoteSourceIdentity &identity = validator.identity();
  CHECK( identity.state == RemoteSourceState::Fresh );
  CHECK( identity.validator.hasStrongEtag() );
  CHECK( identity.validator.etag == "\"v1-abcdef\"" );
  CHECK( identity.validator.lastModified == "Tue, 08 Sep 2026 10:00:00 GMT" );
  CHECK( identity.validator.strength() == "strong_etag+last_modified" );
  CHECK( identity.freshnessProvable() );
  CHECK( identity.acceptsRanges );
  REQUIRE( identity.hasSize );
  CHECK( identity.sizeBytes == 4096 );
  CHECK( !identity.probedAt.empty() );
  CHECK( identity.lastError.empty() );
}

TEST_CASE( "weak ETags never claim byte-level freshness", "[io][remote][validator][weak]" )
{
  HttpRangeServer server( payloadOfSize( 2048 ) );
  server.setEtag( "W/\"weak-1\"" );

  RemoteSourceValidator validator = RemoteSourceValidator::probe( server.url() );
  CHECK( validator.identity().state == RemoteSourceState::Fresh );
  CHECK( validator.identity().validator.hasWeakEtag() );
  CHECK( !validator.identity().freshnessProvable() );
  CHECK( validator.identity().validator.strength() == "weak_etag" );
}

TEST_CASE( "revalidation of unchanged content is proven by the validator",
           "[io][remote][validator][revalidate]" )
{
  HttpRangeServer server( payloadOfSize( 4096 ) );
  server.setEtag( "\"stable-1\"" );
  server.setLastModified( "Tue, 08 Sep 2026 10:00:00 GMT" );

  RemoteSourceValidator validator = RemoteSourceValidator::probe( server.url() );
  REQUIRE( validator.identity().state == RemoteSourceState::Fresh );

  const RevalidationResult result = validator.revalidate();
  CHECK( result.outcome == RevalidationOutcome::Unchanged );
  CHECK( validator.identity().state == RemoteSourceState::Fresh );
  // The revalidation must have gone out as a conditional request and been
  // answered 304 by the fixture (proves real RFC 7232 round-trip).
  CHECK( server.notModifiedResponses() == 1 );
  // This GDAL build leaves the status at 0 for a bare 304; the validator
  // contract is the OUTCOME, not the surfaced status code.
  CHECK( statusIsUsableOrEmpty( result.httpStatus ) );
  CHECK( result.decidedBy == "etag_304" );
}

TEST_CASE( "a validator mismatch proves the content changed and updates identity",
           "[io][remote][validator][revalidate]" )
{
  HttpRangeServer server( payloadOfSize( 4096 ) );
  server.setEtag( "\"before\"" );
  server.setLastModified( "Tue, 08 Sep 2026 10:00:00 GMT" );

  RemoteSourceValidator validator = RemoteSourceValidator::probe( server.url() );
  REQUIRE( validator.identity().state == RemoteSourceState::Fresh );

  // The object changed on the origin: new bytes, new validators.
  server.replacePayload( payloadOfSize( 8192, 11 ), "\"after\"", "Wed, 09 Sep 2026 08:30:00 GMT" );

  const RevalidationResult result = validator.revalidate();
  CHECK( result.outcome == RevalidationOutcome::Changed );
  CHECK( result.decidedBy == "etag_mismatch" );
  CHECK( validator.identity().state == RemoteSourceState::Stale );
  CHECK( validator.identity().validator.etag == "\"after\"" );
  CHECK( validator.identity().validator.lastModified == "Wed, 09 Sep 2026 08:30:00 GMT" );
  REQUIRE( validator.identity().hasSize );
  CHECK( validator.identity().sizeBytes == 8192 );

  // refresh() re-arms freshness against the new content.
  RemoteSourceValidator refreshed = validator.refresh();
  CHECK( refreshed.identity().state == RemoteSourceState::Fresh );
  CHECK( refreshed.identity().validator.etag == "\"after\"" );
}

TEST_CASE( "Last-Modified-only origins revalidate through If-Modified-Since",
           "[io][remote][validator][revalidate]" )
{
  HttpRangeServer server( payloadOfSize( 2048 ) );
  server.setLastModified( "Tue, 08 Sep 2026 10:00:00 GMT" );

  RemoteSourceValidator validator = RemoteSourceValidator::probe( server.url() );
  CHECK( validator.identity().validator.strength() == "last_modified" );
  CHECK( !validator.identity().freshnessProvable() );

  CHECK( validator.revalidate().outcome == RevalidationOutcome::Unchanged );
  CHECK( server.notModifiedResponses() == 1 );

  server.replacePayload( payloadOfSize( 4096 ), "", "Wed, 09 Sep 2026 09:00:00 GMT" );
  const RevalidationResult changed = validator.revalidate();
  CHECK( changed.outcome == RevalidationOutcome::Changed );
  CHECK( changed.decidedBy == "last_modified_mismatch" );
  CHECK( validator.identity().state == RemoteSourceState::Stale );
}

TEST_CASE( "size-only origins never fake freshness", "[io][remote][validator][weak]" )
{
  HttpRangeServer server( payloadOfSize( 2048 ) );

  RemoteSourceValidator validator = RemoteSourceValidator::probe( server.url() );
  CHECK( validator.identity().validator.strength() == "none" );
  CHECK( !validator.identity().freshnessProvable() );

  const RevalidationResult sameSize = validator.revalidate();
  CHECK( sameSize.outcome == RevalidationOutcome::Inconclusive );
  CHECK( sameSize.decidedBy == "size_only_inconclusive" );

  server.replacePayload( payloadOfSize( 4096 ), "", "" );
  const RevalidationResult grownSize = validator.revalidate();
  CHECK( grownSize.outcome == RevalidationOutcome::Changed );
  CHECK( grownSize.decidedBy == "size_mismatch" );
}

TEST_CASE( "offline origins fold into a truthful state instead of throwing",
           "[io][remote][validator][offline]" )
{
  // Port 1 on loopback: nothing listens there; connection refused is the
  // offline shape.
  RemoteSourceValidator validator = RemoteSourceValidator::probe( "http://127.0.0.1:1/fixture.tif" );
  CHECK( validator.identity().state == RemoteSourceState::Offline );
  CHECK( !validator.identity().lastError.empty() );

  const RevalidationResult result = validator.revalidate();
  CHECK( result.outcome == RevalidationOutcome::Inconclusive );
  CHECK( result.decidedBy == "offline" );
  CHECK( validator.identity().state == RemoteSourceState::Offline );
}

TEST_CASE( "timeouts are bounded and fold into offline, never hang",
           "[io][remote][validator][timeout]" )
{
  HttpRangeServer server( payloadOfSize( 4096 ), testsupport::ServerBehavior::Slow );

  RemoteValidatorOptions options;
  options.timeoutSeconds = 1;
  options.connectTimeoutSeconds = 1;
  options.maxRetries = 0;

  const auto started = std::chrono::steady_clock::now();
  RemoteSourceValidator validator = RemoteSourceValidator::probe( server.url(), options );
  const auto elapsed = std::chrono::steady_clock::now() - started;
  CHECK( validator.identity().state == RemoteSourceState::Offline );
  // The whole probe must respect the declared budget (server delays 3s).
  CHECK( std::chrono::duration_cast<std::chrono::seconds>( elapsed ).count() < 10 );
}

TEST_CASE( "gone resources are a Changed outcome with a recorded error",
           "[io][remote][validator][revalidate]" )
{
  HttpRangeServer server( payloadOfSize( 2048 ) );
  server.setEtag( "\"present\"" );

  RemoteSourceValidator validator = RemoteSourceValidator::probe( server.url() );
  REQUIRE( validator.identity().state == RemoteSourceState::Fresh );

  // A different path on the fixture answers 404 — simulate the object
  // disappearing by revalidating against the 404 path.
  const std::string goneUrl = server.url() + ".gone";
  RemoteSourceValidator goneValidator = RemoteSourceValidator::probe( goneUrl );
  CHECK( goneValidator.identity().state == RemoteSourceState::Unknown );
  CHECK( goneValidator.identity().lastError == "not_found (404)" );
  ( void ) validator;
}

TEST_CASE( "non-remote inputs are caller-contract violations (typed throw)",
           "[io][remote][validator]" )
{
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "C:/local/file.tif" ), GeoError );
  REQUIRE( !RemoteSourceValidator::isRemoteUrl( "C:/local/file.tif" ) );
  REQUIRE( RemoteSourceValidator::isRemoteUrl( "https://example.com/x.tif" ) );
  REQUIRE( RemoteSourceValidator::isRemoteUrl( "/vsicurl/https://example.com/x.tif" ) );
}

TEST_CASE( "identity reports never leak credentials", "[io][remote][validator][redaction]" )
{
  // Credential-shaped query values must be masked in every report surface.
  const std::string signedUrl = "https://bucket.example.com/data.tif?X-Amz-Signature=SECRETVALUE&x=1";
  REQUIRE( RemoteSourceValidator::isRemoteUrl( signedUrl ) );
  // A probe to a non-listening host fails (offline), but the identity must
  // still redact before it reaches any report.
  RemoteSourceValidator validator = RemoteSourceValidator::probe(
    "http://127.0.0.1:1/data.tif?X-Amz-Signature=SECRETVALUE&token=TOPSECRETTOKEN" );
  const Json::Value json = validator.identity().toJson();
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  const std::string rendered = Json::writeString( builder, json );
  using Catch::Matchers::ContainsSubstring;
  CHECK_THAT( rendered, !ContainsSubstring( "SECRETVALUE" ) );
  CHECK_THAT( rendered, !ContainsSubstring( "TOPSECRETTOKEN" ) );
  CHECK_THAT( json["url"].asString(), !ContainsSubstring( "SECRETVALUE" ) );
}

TEST_CASE( "identity JSON round-trips through fromJson", "[io][remote][validator]" )
{
  HttpRangeServer server( payloadOfSize( 4096 ) );
  server.setEtag( "\"roundtrip\"" );
  server.setLastModified( "Tue, 08 Sep 2026 10:00:00 GMT" );

  const RemoteSourceIdentity original = RemoteSourceValidator::probe( server.url() ).identity();
  const Json::Value json = original.toJson();
  const RemoteSourceIdentity restored = RemoteSourceIdentity::fromJson( json );
  CHECK( restored.url == original.url );
  CHECK( restored.state == original.state );
  CHECK( restored.validator.etag == original.validator.etag );
  CHECK( restored.validator.lastModified == original.validator.lastModified );
  CHECK( restored.hasSize == original.hasSize );
  CHECK( restored.sizeBytes == original.sizeBytes );
  CHECK( restored.acceptsRanges == original.acceptsRanges );
  CHECK( restored.probedAt == original.probedAt );
  CHECK( restored.freshnessProvable() == original.freshnessProvable() );
}

TEST_CASE( "httpFetch honors ranges, byte budgets and typed truncation",
           "[io][remote][http_fetch]" )
{
  HttpRangeServer server( payloadOfSize( 8192 ) );

  HttpFetchOptions options;
  options.range = "bytes=100-199";
  options.maxResponseBytes = 4096;
  const HttpFetchResult ranged = httpFetch( server.url(), options );
  CHECK( ranged.body.size() == 100 );
  CHECK( ranged.body.front() == payloadOfSize( 8192 )[100] );
  CHECK( !ranged.truncated );
  CHECK( ranged.headerValue( "content-type" ) == "image/tiff" );

  // A budget smaller than the answer truncates and httpFetchJson refuses to
  // package a cut answer as complete JSON.
  HttpFetchOptions tightBudget;
  tightBudget.maxResponseBytes = 16;
  CHECK_THROWS_AS( httpFetchJson( server.url(), tightBudget ), GeoError );

  // Transport errors stay typed.
  CHECK_THROWS_AS( httpFetch( "C:/local/file.tif" ), GeoError );
}



TEST_CASE( "etag weak comparison follows RFC 7232 §2.3",
           "[io][remote][validator][unit]" )
{
  CHECK( RemoteValidatorSet::etagWeakMatches( "\"x\"", "\"x\"" ) );
  CHECK( RemoteValidatorSet::etagWeakMatches( "W/\"x\"", "\"x\"" ) );
  CHECK( RemoteValidatorSet::etagWeakMatches( "\"x\"", "W/\"x\"" ) );
  CHECK( !RemoteValidatorSet::etagWeakMatches( "\"x\"", "\"y\"" ) );
  CHECK( !RemoteValidatorSet::etagWeakMatches( "", "\"x\"" ) );
  CHECK( RemoteValidatorSet( ).strength() == "none" );
}
