/***************************************************************************
  tests/test_io_identity.cpp
  Cloud-Native Geospatial Data Fabric 8.0 — identity primitives:
    * SHA-256 known-answer vectors (FIPS 180-4)
    * ISO-8601/RFC 3339 instant normalization (mixed offsets, naive UTC,
      refusals)
    * fail-closed remote identity tokens (strong-ETag provability, stability,
      change-on-change, credential-shape stripping) over the loopback fixture
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "geospatial/remote/remote_identity_token.h"
#include "geospatial/util/sha256.h"
#include "geospatial/util/time_normalization.h"

#include "support/http_range_server.h"

#include <json/json.h>

#include <fstream>
#include <vector>

namespace
{

std::vector<unsigned char> payloadOfSize( std::size_t size )
{
  return std::vector<unsigned char>( size, 0xAB );
}

std::vector<unsigned char> otherPayloadOfSize( std::size_t size )
{
  std::vector<unsigned char> bytes( size, 0xCD );
  return bytes;
}

} // namespace

TEST_CASE( "SHA-256 matches the FIPS 180-4 known-answer vectors",
           "[io][identity][sha256]" )
{
  using sicnu::geo::sha256Hex;
  // "" → e3b0c442...
  REQUIRE( sha256Hex( "" ) ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
  // "abc" → ba7816bf...
  REQUIRE( sha256Hex( "abc" ) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
  // 448-bit two-block message ("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
  REQUIRE( sha256Hex( "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" ) ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" );
  // A million 'a' via the streaming interface exercises multi-block updates.
  sicnu::geo::Sha256 hash;
  const std::string chunk( 1000, 'a' );
  for ( int i = 0; i < 1000; ++i )
    hash.update( chunk );
  REQUIRE( sicnu::geo::toHex( hash.finalize() ) ==
           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" );
}

TEST_CASE( "ISO-8601 instants normalize across mixed offsets",
           "[io][identity][time]" )
{
  using sicnu::geo::InstantParse;
  using sicnu::geo::instantToUtcString;
  using sicnu::geo::parseIso8601Instant;

  // Same instant in three offsets collapses to one epoch value.
  const InstantParse utc = parseIso8601Instant( "2021-09-03T13:42:12Z" );
  const InstantParse plus2 = parseIso8601Instant( "2021-09-03T15:42:12+02:00" );
  const InstantParse minus5 = parseIso8601Instant( "2021-09-03T08:42:12-05:00" );
  const InstantParse compact = parseIso8601Instant( "2021-09-03T18:12:12+0430" );
  const InstantParse hoursOnly = parseIso8601Instant( "2021-09-03T16:42:12+03" );
  REQUIRE( utc.ok );
  REQUIRE( plus2.ok );
  REQUIRE( minus5.ok );
  REQUIRE( compact.ok );
  REQUIRE( hoursOnly.ok );
  REQUIRE( plus2.hadOffset );
  REQUIRE( utc.epochNanos == plus2.epochNanos );
  REQUIRE( utc.epochNanos == minus5.epochNanos );
  REQUIRE( utc.epochNanos == hoursOnly.epochNanos );
  // A genuinely different instant must not collapse into the same value.
  const InstantParse other = parseIso8601Instant( "2021-09-03T18:42:12+0430" );
  REQUIRE( other.ok );
  CHECK( other.epochNanos != utc.epochNanos );

  // Canonical rendering is Z-normalized and stable.
  REQUIRE( instantToUtcString( plus2.epochNanos ) == "2021-09-03T13:42:12Z" );

  // Fractional seconds survive round-trip and order correctly.
  const InstantParse millis = parseIso8601Instant( "2021-09-03T13:42:12.250Z" );
  const InstantParse micros = parseIso8601Instant( "2021-09-03T13:42:12.250500Z" );
  REQUIRE( millis.ok );
  REQUIRE( micros.ok );
  REQUIRE( instantToUtcString( millis.epochNanos ) == "2021-09-03T13:42:12.250Z" );
  REQUIRE( instantToUtcString( micros.epochNanos ) == "2021-09-03T13:42:12.250500Z" );
  REQUIRE( micros.epochNanos > millis.epochNanos );

  // Naive timestamps parse as ASSUMED UTC (STAC's documented reading) —
  // flagged, never silently treated as declared.
  const InstantParse naive = parseIso8601Instant( "2021-09-03 13:42:12" );
  REQUIRE( naive.ok );
  REQUIRE( naive.assumedUtc );
  REQUIRE_FALSE( naive.hadOffset );
  REQUIRE( naive.epochNanos == utc.epochNanos );

  // Refusals: date-only, garbage, truncated, impossible dates.
  for ( const char *bad : { "2021-09-03", "not-a-time", "2021-09-03T13:42",
                            "2021-02-30T00:00:00Z", "2021-09-03T25:00:00Z",
                            "2021-09-03T13:42:12Z trailing", "" } )
  {
    INFO( "input: " << bad );
    const InstantParse parsed = parseIso8601Instant( bad );
    CHECK_FALSE( parsed.ok );
  }

  // Epoch-nanosecond representability (int64): outside ≈1678–2262 the parse
  // refuses instead of wrapping (a wrapped instant would silently poison
  // ordering and freshness verdicts).
  for ( const char *unrepresentable : { "2263-01-01T00:00:00Z", "1677-01-01T00:00:00Z",
                                        "9999-12-31T23:59:59Z", "0001-01-01T00:00:00Z" } )
  {
    INFO( "input: " << unrepresentable );
    const InstantParse parsed = parseIso8601Instant( unrepresentable );
    CHECK_FALSE( parsed.ok );
  }
  const InstantParse inRange = parseIso8601Instant( "2262-01-01T00:00:00Z" );
  REQUIRE( inRange.ok );

  // Pre-epoch (negative) instants round-trip through the canonical rendering.
  const InstantParse historical = parseIso8601Instant( "1900-06-15T12:00:00Z" );
  REQUIRE( historical.ok );
  CHECK( historical.epochNanos < 0 );
  CHECK( instantToUtcString( historical.epochNanos ) == "1900-06-15T12:00:00Z" );

  // Leap second ":60" carries the ":59" epoch (clamped, never wraps).
  const InstantParse leap = parseIso8601Instant( "2016-12-31T23:59:60Z" );
  const InstantParse beforeLeap = parseIso8601Instant( "2016-12-31T23:59:59Z" );
  REQUIRE( leap.ok );
  REQUIRE( beforeLeap.ok );
  CHECK( leap.epochNanos == beforeLeap.epochNanos );
}

TEST_CASE( "remote identity tokens are proven by strong ETags only",
           "[io][identity][token]" )
{
  // Strong ETag → non-empty token.
  sicnu::geo::testsupport::HttpRangeServer strong( payloadOfSize( 2048 ) );
  strong.setEtag( "\"strong-1\"" );
  const std::string token = sicnu::geo::remoteIdentityToken( strong.url() );
  REQUIRE( !token.empty() );
  REQUIRE( token.rfind( "ri1:v1:", 0 ) == 0 );

  // Weak ETag → "" (semantic equality is not byte-level freshness).
  sicnu::geo::testsupport::HttpRangeServer weak( payloadOfSize( 2048 ) );
  weak.setEtag( "W/\"weak-1\"" );
  CHECK( sicnu::geo::remoteIdentityToken( weak.url() ).empty() );

  // No validator at all → "".
  sicnu::geo::testsupport::HttpRangeServer none( payloadOfSize( 2048 ) );
  CHECK( sicnu::geo::remoteIdentityToken( none.url() ).empty() );

  // Non-remote input is a caller-contract violation (typed throw).
  REQUIRE_THROWS_AS( sicnu::geo::remoteIdentityToken( "/etc/hostname" ),
                     sicnu::geo::GeoError );
}

TEST_CASE( "remote identity tokens are stable and change with the content",
           "[io][identity][token]" )
{
  sicnu::geo::testsupport::HttpRangeServer server( payloadOfSize( 4096 ) );
  server.setEtag( "\"content-v1\"" );
  const std::string first = sicnu::geo::remoteIdentityToken( server.url() );
  REQUIRE( !first.empty() );

  // Same content, fresh probe: identical token (stable across calls).
  const std::string again = sicnu::geo::remoteIdentityToken( server.url() );
  REQUIRE( again == first );

  // Content changes (new ETag): token changes.
  server.replacePayload( otherPayloadOfSize( 4096 ), "\"content-v2\"",
                         "Tue, 08 Sep 2026 10:00:00 GMT" );
  const std::string changed = sicnu::geo::remoteIdentityToken( server.url() );
  REQUIRE( !changed.empty() );
  REQUIRE( changed != first );

  // Reverting the content reverts the token (content-addressed, not
  // probe-history-addressed).
  server.replacePayload( payloadOfSize( 4096 ), "\"content-v1\"",
                         "Mon, 07 Sep 2026 10:00:00 GMT" );
  REQUIRE( sicnu::geo::remoteIdentityToken( server.url() ) == first );
}

TEST_CASE( "remote identity tokens never carry credentials and survive re-signing",
           "[io][identity][token][redaction]" )
{
  sicnu::geo::testsupport::HttpRangeServer server( payloadOfSize( 2048 ) );
  server.setEtag( "\"signed-content\"" );
  const std::string plain = server.url();
  const std::string signedUrl = plain + "?X-Goog-Signature=abc123secret&X-Goog-Expires=900";
  // The fixture serves any query (path route only matches "/fixture.tif" —
  // the signed URL keeps that path, the extra query is ignored by the
  // fixture's exact path check).
  const std::string plainToken = sicnu::geo::remoteIdentityToken( plain );
  const std::string signedToken = sicnu::geo::remoteIdentityToken( signedUrl );
  REQUIRE( !plainToken.empty() );
  REQUIRE( !signedToken.empty() );
  // Credential-shaped query values are stripped from the basis: re-signing
  // the same object does not invalidate the identity.
  CHECK( signedToken == plainToken );
  // The BASIS (not just the digest) must be credential-free — check the
  // diagnostic surface directly for both the plain and the signed URL.
  const std::string plainBasis = sicnu::geo::remoteIdentityBasis( plain );
  const std::string signedBasis = sicnu::geo::remoteIdentityBasis( signedUrl );
  REQUIRE( !plainBasis.empty() );
  CHECK( plainBasis.find( "secret" ) == std::string::npos );
  CHECK( signedBasis.find( "secret" ) == std::string::npos );
  CHECK( signedBasis.find( "X-Goog-Signature" ) == std::string::npos );

  // The /vsicurl/ spelling of the same signed object is the SAME identity:
  // VSI payloads route through the same credential scan (not canonical()
  // which keeps the query verbatim).
  const std::string vsiToken =
    sicnu::geo::remoteIdentityToken( "/vsicurl/" + signedUrl );
  REQUIRE( !vsiToken.empty() );
  CHECK( vsiToken == plainToken );
}

// ---------------------------------------------------------------------------
// 9.0 M1 — unified asset identity: strong local identity, honest strength
// labelling, fail-closed unprovability, subdataset qualification.
// ---------------------------------------------------------------------------

#include "geospatial/identity/asset_identity.h"

#include <filesystem>

#include "geospatial/util/atomic_fs.h"

namespace
{

std::string identityScratchDir( const std::string &name )
{
  const std::filesystem::path dir =
    std::filesystem::temp_directory_path() / "sicnu_io_identity9" / name;
  std::error_code ec;
  std::filesystem::remove_all( dir, ec );
  std::filesystem::create_directories( dir );
  return dir.string();
}

void writeIdentityFile( const std::string &path, const std::string &content )
{
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  out << content;
}

} // namespace

TEST_CASE( "local identity tokens are stable and content-sensitive",
           "[io][identity][local][fabric9]" )
{
  const std::string dir = identityScratchDir( "local" );
  const std::string file = ( std::filesystem::path( dir ) / "asset.tif" ).string();
  writeIdentityFile( file, "IDENTITY-CONTENT-V1" );

  const sicnu::geo::LocalIdentityToken first = sicnu::geo::localIdentityToken( file );
  REQUIRE( first.provable() );
  CHECK( first.strength == "content" );
  CHECK( first.token.rfind( "li1:v1:", 0 ) == 0 );
  CHECK( first.hashedBytes == 19 ); // whole file fits the default budget
  CHECK( first.sizeBytes == 19 );

  // Stable across re-derivation.
  const sicnu::geo::LocalIdentityToken again = sicnu::geo::localIdentityToken( file );
  CHECK( again.token == first.token );

  // Content change ⇒ different token (invalidation by construction).
  writeIdentityFile( file, "IDENTITY-CONTENT-V2" );
  const sicnu::geo::LocalIdentityToken changed = sicnu::geo::localIdentityToken( file );
  REQUIRE( changed.provable() );
  CHECK( changed.token != first.token );

  // Metadata-only identity (budget 0) is honestly labelled weaker.
  sicnu::geo::LocalIdentityOptions metadataOnly;
  metadataOnly.hashBytes = 0;
  const sicnu::geo::LocalIdentityToken weak = sicnu::geo::localIdentityToken( file, metadataOnly );
  REQUIRE( weak.provable() );
  CHECK( weak.strength == "metadata" );
  CHECK( weak.hashedBytes == 0 );
  CHECK( weak.token != first.token );

  // Hash budget is honored: a 1-byte budget differs from the full hash.
  sicnu::geo::LocalIdentityOptions tiny;
  tiny.hashBytes = 1;
  const sicnu::geo::LocalIdentityToken tinyHash = sicnu::geo::localIdentityToken( file, tiny );
  CHECK( tinyHash.hashedBytes == 1 );
  CHECK( tinyHash.token != weak.token );
}

TEST_CASE( "local identity fails closed on unprovable inputs",
           "[io][identity][local][fabric9]" )
{
  const std::string dir = identityScratchDir( "failclosed" );
  const sicnu::geo::LocalIdentityToken missing =
    sicnu::geo::localIdentityToken( ( std::filesystem::path( dir ) / "nope.tif" ).string() );
  CHECK_FALSE( missing.provable() );
  CHECK( missing.token.empty() );
  CHECK( missing.strength.empty() );

  const sicnu::geo::LocalIdentityToken empty = sicnu::geo::localIdentityToken( std::string() );
  CHECK_FALSE( empty.provable() );

  // A directory is not a file: unprovable, never a directory identity.
  const sicnu::geo::LocalIdentityToken directory = sicnu::geo::localIdentityToken( dir );
  CHECK_FALSE( directory.provable() );
}

TEST_CASE( "asset identity dispatches by resource kind and qualifies subdatasets",
           "[io][identity][asset][fabric9]" )
{
  const std::string dir = identityScratchDir( "dispatch" );
  const std::string file = ( std::filesystem::path( dir ) / "cube.nc" ).string();
  writeIdentityFile( file, "NETCDF-CONTAINER-BYTES" );

  // Local dispatch shares the local token.
  const sicnu::geo::AssetIdentity local =
    sicnu::geo::assetIdentityToken( file, { {}, 0, 0, 0, 0 } );
  const sicnu::geo::LocalIdentityToken direct = sicnu::geo::localIdentityToken( file );
  REQUIRE( local.provable() );
  CHECK( local.token == direct.token );
  CHECK( local.strength == "content" );

  // Subdataset qualification: same container, different selectors ⇒
  // different identities; both carry the container's strength.
  const std::string selA = "NETCDF:\"" + file + "\":band_a";
  const std::string selB = "NETCDF:\"" + file + "\":band_b";
  const sicnu::geo::AssetIdentity subA =
    sicnu::geo::assetIdentityToken( selA, { {}, 0, 0, 0, 0 } );
  const sicnu::geo::AssetIdentity subB =
    sicnu::geo::assetIdentityToken( selB, { {}, 0, 0, 0, 0 } );
  REQUIRE( subA.provable() );
  REQUIRE( subB.provable() );
  CHECK( subA.token.rfind( "sd1:v1:", 0 ) == 0 );
  CHECK( subA.token != subB.token );
  CHECK( subA.strength == "content" );

  // A mutated container invalidates every derived subdataset identity.
  writeIdentityFile( file, "NETCDF-CONTAINER-BYTES-EDITED" );
  const sicnu::geo::AssetIdentity subAAfterEdit =
    sicnu::geo::assetIdentityToken( selA, { {}, 0, 0, 0, 0 } );
  CHECK( subAAfterEdit.provable() );
  CHECK( subAAfterEdit.token != subA.token );

  // Directory-shaped and empty inputs stay unprovable (fail-closed).
  const sicnu::geo::AssetIdentity directory =
    sicnu::geo::assetIdentityToken( dir, { {}, 0, 0, 0, 0 } );
  CHECK_FALSE( directory.provable() );
  const sicnu::geo::AssetIdentity nothing = sicnu::geo::assetIdentityToken( std::string() );
  CHECK_FALSE( nothing.provable() );
}
