/***************************************************************************
  tests/test_io_fabric_identity_11.cpp — Cloud Data Fabric 11.0 (WP A/B):
  canonical object keys (cross-spelling identity convergence), VSI-object
  identity probes over a REAL loopback S3 endpoint, credential-context
  fingerprinting (secret never enters), and range-cache wrapping of object
  paths with per-principal separation.
 ***************************************************************************/

#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/object_store.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"
#include "support/http_s3_server.h"

#include <catch2/catch_test_macros.hpp>

#include <cpl_conv.h>
#include <cpl_vsi.h>

#include <filesystem>
#include <string>

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "ident_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

} // namespace

TEST_CASE( "canonicalObjectKey converges every spelling of one object and refuses malformed ones",
           "[io][fabric][identity][wp_a]" )
{
  // Convergence table: one object, four spellings, ONE canonical form.
  const CanonicalObjectKey s3 = canonicalObjectKey( "s3://eo-bucket/scenes/a.tif" );
  const CanonicalObjectKey s3a = canonicalObjectKey( "s3a://eo-bucket/scenes/a.tif" );
  const CanonicalObjectKey s3c = canonicalObjectKey( "s3c://eo-bucket/scenes/a.tif" );
  const CanonicalObjectKey vsi = canonicalObjectKey( "/vsis3/eo-bucket/scenes/a.tif" );
  REQUIRE( s3.valid );
  REQUIRE( s3a.valid );
  REQUIRE( s3c.valid );
  REQUIRE( vsi.valid );
  CHECK( s3.canonical == "s3://eo-bucket/scenes/a.tif" );
  CHECK( s3a.canonical == s3.canonical );
  CHECK( s3c.canonical == s3.canonical );
  CHECK( vsi.canonical == s3.canonical );
  CHECK( s3.scheme == "s3" );
  CHECK( s3.provider == "aws" );
  CHECK( s3a.provider == "s3-compatible" );
  CHECK( s3.bucket == "eo-bucket" );
  CHECK( s3.key == "scenes/a.tif" );

  const CanonicalObjectKey gcs = canonicalObjectKey( "gs://pub/gs.tif" );
  const CanonicalObjectKey gcsVsi = canonicalObjectKey( "/vsigs/pub/gs.tif" );
  REQUIRE( gcs.valid );
  CHECK( gcs.canonical == "gs://pub/gs.tif" );
  CHECK( gcs.provider == "gcs" );
  // /vsigs/pub/gs.tif parses as bucket "pub", key "gs.tif" — same object.
  CHECK( gcsVsi.canonical == gcs.canonical );

  // Refusals: not object-store shapes, or credential-bearing shapes.
  CHECK( !canonicalObjectKey( "s3://bucket" ).valid );                    // no key
  CHECK( !canonicalObjectKey( "s3:///key.tif" ).valid );                  // no bucket
  CHECK( !canonicalObjectKey( "/vsis3/bucket" ).valid );                  // no key
  CHECK( !canonicalObjectKey( "https://host/file.tif" ).valid );          // http is not object-store
  CHECK( !canonicalObjectKey( "C:/data/file.tif" ).valid );               // local
  CHECK( !canonicalObjectKey( "/vsimem/file.tif" ).valid );               // local VSI
  CHECK( !canonicalObjectKey( "garbage" ).valid );
  CHECK( !canonicalObjectKey( "s3://user:pass@bucket/key" ).valid );      // userinfo refused
  CHECK( !canonicalObjectKey( "s3://bucket/key?X-Amz-Signature=abc" ).valid ); // query refused
}

TEST_CASE( "fabricMirrorIndexKey is spelling-stable and credential-free",
           "[io][fabric][identity][wp_a][wp_c]" )
{
  const std::string viaScheme = fabricMirrorIndexKey( "s3://eo-bucket/scenes/a.tif" );
  const std::string viaS3a = fabricMirrorIndexKey( "s3a://eo-bucket/scenes/a.tif" );
  const std::string viaVsi = fabricMirrorIndexKey( "/vsis3/eo-bucket/scenes/a.tif" );
  CHECK( viaScheme == viaS3a );
  CHECK( viaScheme == viaVsi );

  // A signed http href collapses to the credential-free identity URL: the
  // signature value must NOT be part of the key.
  const std::string plain = fabricMirrorIndexKey( "https://host/data/a.tif" );
  const std::string signedHref =
    fabricMirrorIndexKey( "https://host/data/a.tif?X-Amz-Signature=SECRETVALUE&X-Amz-Expires=60" );
  CHECK( plain == signedHref );
  CHECK( signedHref.find( "SECRETVALUE" ) == std::string::npos );
}

TEST_CASE( "objectStoreCredentialContext separates principals and never carries the secret",
           "[io][fabric][identity][wp_a][wp_b]" )
{
  ObjectStoreCredentials a;
  a.accessKeyId = "principal-a";
  a.secretAccessKey = "secret-a-value";
  a.endpoint = "http://127.0.0.1:9000";
  ObjectStoreCredentials b = a;
  b.accessKeyId = "principal-b";
  ObjectStoreCredentials aOtherSecret = a;
  aOtherSecret.secretAccessKey = "DIFFERENT-secret";   // secret is not in the basis
  ObjectStoreCredentials aOtherEndpoint = a;
  aOtherEndpoint.endpoint = "http://127.0.0.1:9001";
  ObjectStoreCredentials anon;
  anon.anonymous = true;
  anon.endpoint = a.endpoint;

  const std::string ctxA = objectStoreCredentialContext( "/vsis3/", a );
  const std::string ctxB = objectStoreCredentialContext( "/vsis3/", b );
  const std::string ctxASecret = objectStoreCredentialContext( "/vsis3/", aOtherSecret );
  const std::string ctxAEndpoint = objectStoreCredentialContext( "/vsis3/", aOtherEndpoint );
  const std::string ctxAnon = objectStoreCredentialContext( "/vsis3/", anon );
  const std::string ctxGs = objectStoreCredentialContext( "/vsigs/", a );

  CHECK( ctxA != ctxB );                    // different principal ⇒ different context
  CHECK( ctxA == ctxASecret );              // the SECRET never enters the fingerprint
  CHECK( ctxA != ctxAEndpoint );            // different store ⇒ different context
  CHECK( ctxA != ctxAnon );                 // signed vs anonymous never share
  CHECK( ctxA != ctxGs );                   // different provider prefix never shares
  CHECK( ctxA.find( "secret-a-value" ) == std::string::npos );
  CHECK( ctxA.size() == 16 );               // bounded fingerprint (16 hex chars)
}

TEST_CASE( "identity probes and tokens converge over the real loopback S3 stack",
           "[io][fabric][identity][wp_a][integration]" )
{
  const std::string dir = scratchDir( "s3ident" );
  const std::vector<unsigned char> payload( 8192, 0x5A );
  testsupport::HttpS3Server server( "eo-bucket", "scene.tif", payload, "\"etag-ident-1\"" );
  REQUIRE( server.valid() );

  ObjectStoreCredentials credentials;
  credentials.accessKeyId = "loopback";
  credentials.secretAccessKey = "loopback-secret";
  credentials.endpoint = server.endpoint();

  std::string tokenViaVsi;
  std::string tokenViaScheme;
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );

    // HEAD-equivalent probe through the VSI stack (GDAL signs).
    const ObjectStoreIdentityFacts facts = probeObjectStoreIdentity( "/vsis3/eo-bucket/scene.tif" );
    REQUIRE( facts.probed );
    REQUIRE( facts.provable() );
    CHECK( facts.etag == "\"etag-ident-1\"" );
    REQUIRE( facts.hasSize );
    CHECK( facts.sizeBytes == payload.size() );

    // The fabric identity entry point: same token for both spellings —
    // this is the WP A convergence contract over REAL GDAL traffic.
    const AssetIdentity viaVsi = fabricAssetIdentity( "/vsis3/eo-bucket/scene.tif" );
    REQUIRE( viaVsi.provable() );
    CHECK( viaVsi.strength == "etag" );
    tokenViaVsi = viaVsi.token;
    tokenViaScheme = fabricAssetIdentity( "s3://eo-bucket/scene.tif" ).token;
    CHECK( tokenViaVsi == tokenViaScheme );
    CHECK( tokenViaVsi.rfind( "ri1:v1:", 0 ) == 0 );   // the 8.0 scheme, no second identity
  }
  CHECK( activeScopedCredentialWindows() == 0 );

  // Offline: probing and identity both fold to fail-closed ("" / !probed),
  // never a stale claim and never a throw. The GDAL network deny is the
  // backstop that makes any accidental /vsi* open fail fast — a probe that
  // DID reach the loopback server would show up as a request-count change.
  const std::uint64_t requestsBefore = server.requestCount();
  offline::setEnabled( true );
  offline::applyGdalNetworkDeny();
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    const ObjectStoreIdentityFacts offlineFacts =
      probeObjectStoreIdentity( "/vsis3/eo-bucket/scene.tif" );
    CHECK( !offlineFacts.probed );
    CHECK( !offlineFacts.provable() );
    CHECK( !offlineFacts.errorText.empty() );
    CHECK( fabricAssetIdentity( "/vsis3/eo-bucket/scene.tif" ).token.empty() );
    CHECK( server.requestCount() == requestsBefore );   // ZERO dispatch
  }
  offline::clearGdalNetworkDeny();
  offline::setEnabled( false );
  CHECK( activeScopedCredentialWindows() == 0 );
}

TEST_CASE( "range cache wraps object-store paths and separates cache entries per principal",
           "[io][fabric][range_cache][wp_b][integration]" )
{
  const std::string dir = scratchDir( "s3cache" );
  const std::vector<unsigned char> payload( 16384, 0x3C );
  testsupport::HttpS3Server server( "eo-bucket", "cached.tif", payload, "\"etag-cache-1\"" );
  REQUIRE( server.valid() );

  ObjectStoreCredentials a;
  a.accessKeyId = "principal-a";
  a.secretAccessKey = "secret-a";
  a.endpoint = server.endpoint();

  RemoteRangeCache::install( {} );
  struct CacheGuard { ~CacheGuard() { RemoteRangeCache::uninstall(); } } cacheGuard;

  // fabricCachedPath wraps object spellings; scheme spellings converge on
  // the /vsis3/ form BEFORE wrapping (one object ⇒ one cached resource
  // spelling per principal).
  CHECK( fabricCachedPath( "https://host/f.tif" ) == "/vsirangecache/https://host/f.tif" );
  const std::string wrappedVsi = fabricCachedPath( "/vsis3/eo-bucket/cached.tif" );
  const std::string wrappedScheme = fabricCachedPath( "s3://eo-bucket/cached.tif" );
  const std::string wrappedS3a = fabricCachedPath( "s3a://eo-bucket/cached.tif" );
  CHECK( wrappedVsi == "/vsirangecache//vsis3/eo-bucket/cached.tif" );
  CHECK( wrappedScheme == wrappedVsi );
  CHECK( wrappedS3a == wrappedVsi );

  // A read through the wrapped path serves the real bytes (loopback S3).
  std::uint64_t servedUnderA = 0;
  {
    ScopedObjectStoreCredentials window( "/vsis3/", a );
    const std::string cached = fabricCachedPath( "/vsis3/eo-bucket/cached.tif" );
    VSILFILE *handle = VSIFOpenL( cached.c_str(), "rb" );
    REQUIRE( handle != nullptr );
    std::vector<unsigned char> buffer( payload.size() );
    const std::size_t got = VSIFReadL( buffer.data(), 1, buffer.size(), handle );
    VSIFCloseL( handle );
    REQUIRE( got == payload.size() );
    CHECK( buffer == payload );
    servedUnderA = RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
    CHECK( servedUnderA > 0 );
  }

  // Principal separation: a DIFFERENT principal's entry is a different
  // cache resource — the shared entry under no window is not consulted
  // either (entries created under a context never leak across contexts).
  ObjectStoreCredentials b = a;
  b.accessKeyId = "principal-b";
  {
    ScopedObjectStoreCredentials window( "/vsis3/", b );
    const Json::Value before = RemoteRangeCache::telemetryJson();
    const std::string cached = fabricCachedPath( "/vsis3/eo-bucket/cached.tif" );
    VSILFILE *handle = VSIFOpenL( cached.c_str(), "rb" );
    REQUIRE( handle != nullptr );
    std::vector<unsigned char> buffer( 64 );
    VSIFReadL( buffer.data(), 1, buffer.size(), handle );
    VSIFCloseL( handle );
    const Json::Value after = RemoteRangeCache::telemetryJson();
    // Principal B fetched its OWN bytes (a fresh miss, not A's blocks):
    CHECK( after["bytes_fetched"].asUInt64() > before["bytes_fetched"].asUInt64() );
    CHECK( after["hits"].asUInt64() == before["hits"].asUInt64() );
  }
}

TEST_CASE( "#1162: a weak-ETag VSI object still invalidates when its size changes",
           "[io][fabric][range_cache][issue1162]" )
{
  // The object-store revalidation arm used to gate ALL change detection on
  // a strong ETag: a weak/absent ETag (multipart "null", GS) only refreshed
  // the entry SIZE, serving old-generation blocks under the new size. The
  // size itself is a change signal — mismatch must drop the blocks exactly
  // like the http arm's size_mismatch fallback.
  std::vector<unsigned char> payload( 16384, 0x3C );
  for ( std::size_t i = 0; i < payload.size(); ++i )
    payload[i] = static_cast<unsigned char>( i & 0xFF );
  testsupport::HttpS3Server server( "eo-bucket", "weak.tif", payload, "W/\"weak-1\"" );
  REQUIRE( server.valid() );

  ObjectStoreCredentials creds;
  creds.accessKeyId = "principal-a";
  creds.secretAccessKey = "secret-a";
  creds.endpoint = server.endpoint();

  RangeCacheConfig config;
  config.stalePolicy = RangeCacheStalePolicy::RevalidateOnOpen;
  RemoteRangeCache::install( config );
  struct CacheGuard { ~CacheGuard() { RemoteRangeCache::uninstall(); } } cacheGuard;

  const auto readAll = [&]( std::vector<unsigned char> &out ) {
    const std::string cached = fabricCachedPath( "/vsis3/eo-bucket/weak.tif" );
    VSILFILE *handle = VSIFOpenL( cached.c_str(), "rb" );
    REQUIRE( handle != nullptr );
    out.resize( 65536 );
    const std::size_t got = VSIFReadL( out.data(), 1, out.size(), handle );
    VSIFCloseL( handle );
    out.resize( got );
  };

  {
    ScopedObjectStoreCredentials window( "/vsis3/", creds );
    std::vector<unsigned char> first;
    readAll( first );
    REQUIRE( first == payload );

    // Replace the object: DIFFERENT SIZE, same (weak) ETag — the one
    // signal an unprovable identity must not miss.
    std::vector<unsigned char> replacement( 24576, 0x5A );
    for ( std::size_t i = 0; i < replacement.size(); ++i )
      replacement[i] = static_cast<unsigned char>( ( i * 7 ) & 0xFF );
    server.replacePayload( replacement, "W/\"weak-1\"" );

    const Json::Value before = RemoteRangeCache::telemetryJson();
    std::vector<unsigned char> second;
    readAll( second );
    const Json::Value after = RemoteRangeCache::telemetryJson();
    CHECK( after["invalidations"].asUInt64() > before["invalidations"].asUInt64() );
    // Fresh bytes at the fresh size — never old blocks under the new size.
    REQUIRE( second.size() == replacement.size() );
    CHECK( second == replacement );
  }
}
