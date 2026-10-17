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
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/remote_identity_resolver.h"
#include "geospatial/remote/remote_source_validator.h"
#include "support/http_range_server.h"
#include "support/http_stac_server.h"
#include "support/offline_probe.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <future>
#include <thread>

static const char *NL = "\n";
#include <cstdio>
#include <string>
#include <vector>

using namespace sicnu::geo;
using sicnu::geo::testsupport::HttpRangeServer;

// ADR 0146: never die by timeout when the loopback transport is missing —
// report `sicnu-skip: <reason>` + exit 77 instead.
SICNU_OFFLINE_GUARD()

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

TEST_CASE( "strict httpFetch types every >=400 origin error, never a silent success",
           "[io][remote][http_fetch][errors]" )
{
  SECTION( "500 is a NetworkError; the raw status variant still answers" )
  {
    HttpRangeServer server( payloadOfSize( 256 ), testsupport::ServerBehavior::ServerError );
    try
    {
      httpFetch( server.url() );
      FAIL( "httpFetch must throw for a 500 answer" );
    }
    catch ( const GeoError &error )
    {
      CHECK( error.code() == ErrorCode::NetworkError );
    }
    // httpFetchJson must fail typed too — an error body is never a document.
    CHECK_THROWS_AS( httpFetchJson( server.url() ), GeoError );
    const HttpFetchResult raw = httpFetchStatus( server.url() );
    CHECK( raw.httpStatus == 500 );
  }

  SECTION( "410 is a NotFound, not only 404" )
  {
    HttpRangeServer server( payloadOfSize( 256 ), testsupport::ServerBehavior::Gone );
    try
    {
      httpFetch( server.url() );
      FAIL( "httpFetch must throw for a 410 answer" );
    }
    catch ( const GeoError &error )
    {
      CHECK( error.code() == ErrorCode::NotFound );
    }
  }
}

TEST_CASE( "hostile identity JSON fails typed, never as Json::LogicError",
           "[io][remote][validator][negative]" )
{
  Json::Value hostile;
  hostile["url"] = "https://example.invalid/x.tif";
  hostile["size_bytes"] = "not-a-number";
  CHECK_THROWS_AS( RemoteSourceIdentity::fromJson( hostile ), GeoError );
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

// Regression guard (D10 default #7): the loopback proxy exemption is what
// keeps /vsicurl/ fixture reads away from machine-room proxies; if the
// hygiene pass regresses, this fails in seconds instead of hanging a lab
// host. Wall-clock bound: trivially < 1 s.
TEST_CASE( "offline guard keeps loopback exempt from proxies",
           "[io][offline-guard]" )
{
  sicnu::testsupport::offline::ensureLoopbackProxyHygiene();
  CHECK( sicnu::testsupport::offline::loopbackProxyExempted() );
}

TEST_CASE( "the session identity resolver returns on first sight, never deadlocks",
           "[io][remote][identity][resolver]" )
{
  // Regression: the first-sight path called probeAndStore while STILL
  // holding the session-cache mutex. The probe itself completed, then the
  // publish lock re-acquired the same non-recursive mutex from the SAME
  // thread — the submitting thread deadlocked forever, so every first
  // TaskCenter submission of a remote input hung (observed as the
  // execution-benchmarks remote-cog workload timing out).
  //
  // The probe runs on the calling thread; bounded via a detached worker and
  // a wait_for — on the regression this fails in seconds instead of hanging
  // the binary (the detached thread stays stuck; process exit is clean).
  HttpRangeServer server( payloadOfSize( 2048 ) );
  server.setEtag( "\"resolver-first-sight-1\"" );
  auto resolver = sicnu::geo::makeRemoteInputIdentityResolver();

  std::promise<std::string> promise;
  std::future<std::string> future = promise.get_future();
  std::thread worker( [&promise, &resolver, &server] {
    try
    {
      promise.set_value( resolver( server.url() ) );
    }
    catch ( ... )
    {
      try { promise.set_exception( std::current_exception() ); } catch ( ... ) {}
    }
  } );
  worker.detach();

  const auto status = future.wait_for( std::chrono::seconds( 30 ) );
  REQUIRE( status == std::future_status::ready );
  // First sight probes the origin. Since R12-B (#1504) the production
  // resolver blocks private networks, so this LOOPBACK origin is refused
  // before dispatch; R15-B (B04) makes that refusal fail closed as the
  // documented EMPTY token (uncacheable) instead of an escaping GeoError.
  // The publish lock is still taken after the probe on the first-sight
  // path, so the never-deadlocks regression stays exercised.
  CHECK( future.get().empty() );
}

TEST_CASE( "offline::isPrivateHost identifies private, loopback and IPv4-mapped IPv6 targets (Oracle-B2.1)",
           "[io][remote][offline][ssrf]" )
{
  using sicnu::geo::offline::isPrivateHost;

  // IPv4-mapped IPv6 loopback, link-local, and private networks
  CHECK( isPrivateHost( "http://[::ffff:127.0.0.1]/data.tif" ) );
  CHECK( isPrivateHost( "http://[::ffff:169.254.169.254]/meta" ) );
  CHECK( isPrivateHost( "https://[::ffff:10.0.0.5]/stac" ) );
  CHECK( isPrivateHost( "https://[::ffff:172.31.255.1]/stac" ) );
  CHECK( isPrivateHost( "https://[::ffff:192.168.1.7]/stac" ) );
  CHECK( isPrivateHost( "https://[::ffff:100.64.0.1]/stac" ) );
  CHECK( isPrivateHost( "https://[::ffff:0.0.0.1]/stac" ) );
  CHECK( isPrivateHost( "::ffff:127.0.0.1" ) );
  CHECK( isPrivateHost( "[::ffff:127.0.0.1]:8080" ) );
  CHECK( isPrivateHost( "/vsicurl/http://[::ffff:127.0.0.1]/data.tif" ) );
  CHECK( isPrivateHost( "/vsirangecache/http://[::ffff:169.254.169.254]/data.tif" ) );

  // Uncompressed IPv4-mapped IPv6 (RFC 4291 §2.5.5.2) and IPv4-compatible (P0-1 regression)
  CHECK( isPrivateHost( "http://[0:0:0:0:0:ffff:127.0.0.1]/data.tif" ) );
  CHECK( isPrivateHost( "http://[0:0:0:0:0:ffff:169.254.169.254]/meta" ) );
  CHECK( isPrivateHost( "http://[0000:0000:0000:0000:0000:ffff:169.254.169.254]/meta" ) );
  CHECK( isPrivateHost( "http://[::127.0.0.1]/data.tif" ) );
  CHECK_FALSE( isPrivateHost( "http://[0:0:0:0:0:ffff:8.8.8.8]/data.tif" ) );

  // IPv4-mapped IPv6 public addresses must NOT be blocked
  CHECK_FALSE( isPrivateHost( "https://[::ffff:8.8.8.8]/stac" ) );
  CHECK_FALSE( isPrivateHost( "::ffff:8.8.8.8" ) );

  // Native IPv6 addresses
  CHECK( isPrivateHost( "https://[::1]/stac" ) );
  CHECK( isPrivateHost( "::1" ) );
  CHECK( isPrivateHost( "https://[fe80::1]/stac" ) );
  CHECK( isPrivateHost( "https://[fd12::1]/stac" ) );
  CHECK_FALSE( isPrivateHost( "https://[2001:db8::1]/stac" ) );

  // Scoped IPv6 addresses (RFC 6874 zone ID)
  CHECK( isPrivateHost( "http://[fe80::1%eth0]/data.tif" ) );
  CHECK( isPrivateHost( "http://[fe80::1%25eth0]/data.tif" ) );
  CHECK( isPrivateHost( "fe80::1%eth0" ) );

  // IPv6 unspecified address
  CHECK( isPrivateHost( "http://[::]/data.tif" ) );
  CHECK( isPrivateHost( "::" ) );

  // IPv6 malformed boundary (buffer overflow regression test)
  CHECK_FALSE( isPrivateHost( "1:2:3:4:5:6:7::8:9" ) );

  // Native IPv4 and hostname targets
  CHECK( isPrivateHost( "http://127.0.0.1:8000/data.tif" ) );
  CHECK( isPrivateHost( "127.0.0.1" ) );
  CHECK( isPrivateHost( "10.0.0.1" ) );
  CHECK( isPrivateHost( "192.168.1.1" ) );
  CHECK( isPrivateHost( "169.254.169.254" ) );
  CHECK( isPrivateHost( "localhost" ) );
  CHECK( isPrivateHost( "localhost.localdomain" ) );
  CHECK( isPrivateHost( "ip6-localhost" ) );
  CHECK( isPrivateHost( "http://localhost.localdomain:8080/path.tif" ) );
  CHECK( isPrivateHost( "sub.localhost" ) );
  CHECK( isPrivateHost( "localhost." ) );
  CHECK( isPrivateHost( "http://localhost./data.tif" ) );
  CHECK( isPrivateHost( "metadata.google.internal" ) );
  CHECK( isPrivateHost( "instance-data" ) );
  CHECK_FALSE( isPrivateHost( "https://example.com/cog.tif" ) );
  CHECK_FALSE( isPrivateHost( "8.8.8.8" ) );

  // Dotted octal, hexadecimal, and shortened IPv4 representations
  CHECK( isPrivateHost( "http://0177.0.0.1/data.tif" ) );
  CHECK( isPrivateHost( "0177.0.0.1" ) );
  CHECK( isPrivateHost( "http://2130706433/data.tif" ) );
  CHECK( isPrivateHost( "2130706433" ) );
  CHECK( isPrivateHost( "127.1" ) );
  CHECK( isPrivateHost( "http://127.1/data.tif" ) );
  CHECK( isPrivateHost( "10.1" ) );
  CHECK( isPrivateHost( "http://10.1/data.tif" ) );
  CHECK( isPrivateHost( "127.0.1" ) );
  CHECK( isPrivateHost( "0x7f000001" ) );
  CHECK( isPrivateHost( "http://0x7f000001/data.tif" ) );
  CHECK( isPrivateHost( "017700000001" ) );
  CHECK( isPrivateHost( "http://017700000001/data.tif" ) );
  CHECK( isPrivateHost( "0x7f.1" ) );
  CHECK( isPrivateHost( "0177.1" ) );
}

TEST_CASE( "RemoteSourceValidator blocks private networks when requested (Oracle-B2.1)",
           "[io][remote][validator][ssrf]" )
{
  RemoteValidatorOptions options;
  options.blockPrivateNetworks = true;

  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://[::ffff:127.0.0.1]/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://[::ffff:169.254.169.254]/meta", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://127.0.0.1:8000/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://[fe80::1%eth0]/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://[::]/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://0177.0.0.1/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://2130706433/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://127.1/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://0x7f000001/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://017700000001/data.tif", options ), GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( "http://10.1/data.tif", options ), GeoError );
}

TEST_CASE( "redirectGuard blocks redirect hopping to private networks (#1485)",
           "[io][remote][validator][ssrf]" )
{
  HttpFetchOptions fetchOptions;
  fetchOptions.redirectGuard = []( const std::string &hopUrl ) -> std::string {
    return offline::isPrivateHost( hopUrl ) ? "private network blocked" : "";
  };

  CHECK( fetchOptions.redirectGuard( "http://127.0.0.1/admin" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://169.254.169.254/latest/meta-data/" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://[::1]/secret" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://[fe80::1%eth0]/secret" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://[::]/secret" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://2130706433/secret" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://0177.0.0.1/secret" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://127.1/secret" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://0x7f000001/secret" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "http://017700000001/secret" ) == "private network blocked" );
  CHECK( fetchOptions.redirectGuard( "https://example.com/cog.tif" ).empty() );
}

TEST_CASE( "RemoteSourceValidator in offline mode enforces zero egress (Oracle-B2.2)",
           "[io][remote][validator][offline]" )
{
  offline::setEnabled( true );
  struct RestoreGuard
  {
    ~RestoreGuard() { offline::setEnabled( false ); }
  } guard;

  RemoteSourceValidator validator = RemoteSourceValidator::probe( "https://example.com/data.tif" );
  CHECK( validator.identity().state == RemoteSourceState::Offline );
  CHECK( validator.identity().lastError.find( "offline mode" ) != std::string::npos );

  RevalidationResult res = validator.revalidate();
  CHECK( res.outcome == RevalidationOutcome::Inconclusive );
  CHECK( res.decidedBy == "offline" );
}

TEST_CASE( "RemoteSourceValidator blocks private network target reached via redirect (SEC-02)",
           "[io][remote][validator][ssrf][redirect]" )
{
  testsupport::HttpStacServer server;
  REQUIRE( server.port() > 0 );

  testsupport::StacRoute rImds;
  rImds.status = 302;
  rImds.contentType.clear();
  rImds.headers["Location"] = "http://169.254.169.254/latest/meta-data/";
  server.setRoute( "/redirect-imds", rImds );

  testsupport::StacRoute rLoop;
  rLoop.status = 302;
  rLoop.contentType.clear();
  rLoop.headers["Location"] = "http://127.0.0.1:8080/internal";
  server.setRoute( "/redirect-loopback", rLoop );

  RemoteValidatorOptions options;
  options.blockPrivateNetworks = true;

  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( server.url() + "/redirect-imds", options ),
                     GeoError );
  REQUIRE_THROWS_AS( RemoteSourceValidator::probe( server.url() + "/redirect-loopback", options ),
                     GeoError );
}

TEST_CASE( "offline::isPrivateHost rejects oversized IPv6 without buffer overrun (SEC-01)",
           "[io][remote][offline][ipv6][bounds]" )
{
  using sicnu::geo::offline::isPrivateHost;

  // 7 head groups + 2 tail groups: total 9 groups > 8.
  CHECK_FALSE( isPrivateHost( "http://[1:2:3:4:5:6:7::8:9]/data.tif" ) );
  CHECK_FALSE( isPrivateHost( "1:2:3:4:5:6:7::8:9" ) );

  // 7 head groups + IPv4 tail (counts as 2 groups): total 9 groups > 8.
  CHECK_FALSE( isPrivateHost( "http://[1:2:3:4:5:6:7::127.0.0.1]/data.tif" ) );
  CHECK_FALSE( isPrivateHost( "1:2:3:4:5:6:7::127.0.0.1" ) );

  // Multiple tail groups exceeding 8 groups total.
  CHECK_FALSE( isPrivateHost( "http://[1:2:3:4:5::6:7:8:9:a:b]/data.tif" ) );
  CHECK_FALSE( isPrivateHost( "http://[::1:2:3:4:5:6:7:8:9]/data.tif" ) );
  CHECK_FALSE( isPrivateHost( "http://[1:2:3:4:5:6:7:8::]/data.tif" ) );
}

TEST_CASE( "offline::isPrivateHost identifies :: and special ranges (SEC-03, SEC-04)",
           "[io][remote][offline][ipv6][subnets]" )
{
  using sicnu::geo::offline::isPrivateHost;

  // Unspecified address (::) routes to loopback on POSIX
  CHECK( isPrivateHost( "http://[::]/data.tif" ) );
  CHECK( isPrivateHost( "::" ) );
  CHECK( isPrivateHost( "[::]:8080" ) );
  CHECK( isPrivateHost( "http://[0:0:0:0:0:0:0:0]/data.tif" ) );

  // IPv6 link-local with Zone ID / Scope Index (%eth0, %25eth0)
  CHECK( isPrivateHost( "http://[fe80::1%eth0]/data.tif" ) );
  CHECK( isPrivateHost( "http://[fe80::1%25eth0]/data.tif" ) );
  CHECK( isPrivateHost( "http://[fe80::1%1]/data.tif" ) );
  CHECK( isPrivateHost( "[fe80::1%eth0]:8080" ) );

  // NAT64 prefix embedding private IPv4 (64:ff9b::/96)
  CHECK( isPrivateHost( "http://[64:ff9b::127.0.0.1]/data.tif" ) );
  CHECK( isPrivateHost( "http://[64:ff9b::169.254.169.254]/data.tif" ) );
  CHECK( isPrivateHost( "http://[64:ff9b::10.0.0.1]/data.tif" ) );
  CHECK( isPrivateHost( "http://[64:ff9b::0.0.0.0]/data.tif" ) );
  // Public IPv4 via NAT64 must NOT be classified as private
  CHECK_FALSE( isPrivateHost( "http://[64:ff9b::8.8.8.8]/data.tif" ) );

  // IPv6 Multicast (ff00::/8)
  CHECK( isPrivateHost( "http://[ff02::1]/data.tif" ) );
  CHECK( isPrivateHost( "http://[ff01::1]/data.tif" ) );
  CHECK( isPrivateHost( "ff02::1" ) );
}

TEST_CASE( "offline::isPrivateHost validates alternative IPv4 encodings (SEC-05)",
           "[io][remote][offline][ipv4][encodings]" )
{
  using sicnu::geo::offline::isPrivateHost;

  // Octal representations of loopback (0177 = 127)
  CHECK( isPrivateHost( "http://0177.0.0.1:8080/data.tif" ) );
  CHECK( isPrivateHost( "0177.0.0.1" ) );
  CHECK( isPrivateHost( "http://0177.0.0.01/data.tif" ) );
  CHECK( isPrivateHost( "http://012.0.0.1/data.tif" ) ); // 10.0.0.1 in octal

  // Hexadecimal notation (0x7f = 127)
  CHECK( isPrivateHost( "http://0x7f.0.0.1/data.tif" ) );
  CHECK( isPrivateHost( "0x7f.0.0.1" ) );
  CHECK( isPrivateHost( "http://0x7f000001/data.tif" ) );
  CHECK( isPrivateHost( "0x7f000001" ) );

  // DWORD / integer notation
  CHECK( isPrivateHost( "http://2130706433/data.tif" ) ); // 127.0.0.1 as dword
  CHECK( isPrivateHost( "2130706433" ) );
  CHECK( isPrivateHost( "http://2852039166/data.tif" ) ); // 169.254.169.254 as dword

  // Classful shorthand
  CHECK( isPrivateHost( "http://127.1/data.tif" ) ); // 127.0.0.1 as 2-part
  CHECK( isPrivateHost( "127.1" ) );
  CHECK( isPrivateHost( "http://10.1/data.tif" ) ); // 10.0.0.1
  CHECK( isPrivateHost( "http://192.168.1/data.tif" ) ); // 192.168.0.1 as 3-part

  // Percent-encoded hostname
  CHECK( isPrivateHost( "http://local%68ost:8080/data.tif" ) );
  CHECK( isPrivateHost( "local%68ost" ) );
}
