/***************************************************************************
  tests/test_io_fabric_object_store.cpp — fabric 10.0: object storage profile
  seam. Profile table, URI resolution typing, credential RAII (exact restore),
  offline refusal, and a REAL /vsis3/ open against a loopback S3-shaped
  endpoint (GDAL speaks plain HTTP to a custom endpoint).
 ***************************************************************************/

#include "geospatial/fabric/object_store.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/remote/offline_gate.h"
#include "geospatial/remote/range_cache.h"
#include "support/http_s3_server.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cpl_conv.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace sicnu::geo;
using Catch::Approx;

namespace
{

/// A throwaway directory under the build-side scratch area.
std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "obj_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

std::vector<unsigned char> fileBytes( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  return std::vector<unsigned char>( ( std::istreambuf_iterator<char>( in ) ),
                                     std::istreambuf_iterator<char>() );
}

/// 96×96 tiled GTiff with a ramp — small, real, and window-readable.
std::vector<unsigned char> buildTiff( const std::string &path )
{
  const int size = 96;
  {
    RasterWriter writer = RasterWriter::create( path, size, size, { RasterBandSpec {} },
                                                { "GTiff", { "TILED=YES", "BLOCKXSIZE=64", "BLOCKYSIZE=64" }, true } );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
    std::vector<double> raster( static_cast<std::size_t>( size ) * size );
    for ( std::size_t i = 0; i < raster.size(); ++i )
      raster[i] = static_cast<double>( ( i * 7 ) % 251 );
    writer.writeWindow( 1, { 0, 0, size, size }, raster.data() );
    writer.finalize();
  }
  return fileBytes( path );
}

/// Counts read pixels that differ from the written ramp (0 = exact). The
/// WINDOW buffer is packed (y*windowWidth + x); the source ramp was written
/// over the FULL 96-wide raster — expected(x,y) reads the source formula at
/// the window's source pixel, never at the buffer index (that confusion is
/// exactly what a wrong window contract would look like).
std::size_t rampMismatches( const std::vector<double> &values, int width, int height,
                            int sourceWidth = 96, int xOffset = 0, int yOffset = 0 )
{
  std::size_t mismatches = 0;
  for ( int y = 0; y < height; ++y )
    for ( int x = 0; x < width; ++x )
    {
      const std::size_t index = static_cast<std::size_t>( y ) * width + x;
      const std::size_t source = static_cast<std::size_t>( yOffset + y ) * sourceWidth + xOffset + x;
      const double expected = static_cast<double>( ( source * 7 ) % 251 );
      if ( values[index] != expected )
        ++mismatches;
    }
  return mismatches;
}

} // namespace

TEST_CASE( "object store profile table exposes built-ins and accepts extension",
           "[io][fabric][object_store]" )
{
  const std::vector<ObjectStoreProfile> profiles = objectStoreProfiles();
  REQUIRE( profiles.size() >= 5 );

  const ObjectStoreProfile s3 = findObjectStoreProfile( "s3" );
  CHECK( s3.scheme == "s3" );   // by-value lookup: empty scheme = miss
  CHECK( s3.vsiPrefix == "/vsis3/" );
  CHECK( s3.provider == "aws" );
  CHECK( findObjectStoreProfile( "S3" ).scheme == "s3" );   // case-insensitive
  CHECK( findObjectStoreProfile( "gs" ).scheme == "gs" );
  CHECK( findObjectStoreProfile( "az" ).scheme == "az" );
  CHECK( findObjectStoreProfile( "s3a" ).scheme == "s3a" );
  CHECK( findObjectStoreProfile( "https" ).scheme.empty() ); // http(s) never claimable
  CHECK( findObjectStoreProfile( "nope" ).scheme.empty() );

  ObjectStoreProfile custom { "cos", "/vsis3/", "s3-compatible", true };
  registerObjectStoreProfile( custom );
  CHECK( findObjectStoreProfile( "cos" ).vsiPrefix == "/vsis3/" );

  ObjectStoreProfile duplicate { "s3", "/vsis3/", "aws", true };
  REQUIRE_THROWS_AS( registerObjectStoreProfile( duplicate ), GeoError );
  ObjectStoreProfile prefixless { "bad", "vsis3", "x", true };
  REQUIRE_THROWS_AS( registerObjectStoreProfile( prefixless ), GeoError );
}

TEST_CASE( "resolveObjectStore maps scheme and VSI spellings to fetchable paths",
           "[io][fabric][object_store]" )
{
  const ObjectStoreResolution resolved = resolveObjectStore( "s3://eo-scenes/2024/scene.tif" );
  CHECK( resolved.profile.scheme == "s3" );
  CHECK( resolved.bucket == "eo-scenes" );
  CHECK( resolved.key == "2024/scene.tif" );
  CHECK( resolved.vsiPath == "/vsis3/eo-scenes/2024/scene.tif" );
  CHECK( resolved.needsCredentials );

  const ObjectStoreResolution direct = resolveObjectStore( "/vsis3/bucket-a/key b.tif" );
  CHECK( direct.bucket == "bucket-a" );
  CHECK( direct.key == "key b.tif" );
  CHECK( direct.vsiPath == "/vsis3/bucket-a/key b.tif" );
  CHECK( !direct.needsCredentials );   // profile declares anonymous-capable

  // Typed refusals — every malformed input has a distinct refusal, none
  // guesses.
  REQUIRE_THROWS_AS( resolveObjectStore( "unknown://bucket/key" ), GeoError );
  REQUIRE_THROWS_AS( resolveObjectStore( "s3://just-a-bucket" ), GeoError );
  REQUIRE_THROWS_AS( resolveObjectStore( "s3://user:secret@bucket/key" ), GeoError );
  REQUIRE_THROWS_AS( resolveObjectStore( "s3://bucket/key?X-Amz-Signature=abc" ), GeoError );
  REQUIRE_THROWS_AS( resolveObjectStore( "/vsis3/nosecondsegment" ), GeoError );
  REQUIRE_THROWS_AS( resolveObjectStore( "/local/path.tif" ), GeoError );

  // The credential shape report carries booleans only — never values.
  ObjectStoreCredentials credentials;
  credentials.accessKeyId = "AKIA_TEST";
  credentials.secretAccessKey = "TOPSECRET";
  const Json::Value shape = credentials.toJsonShape();
  CHECK( shape["hasAccessKeyId"].asBool() );
  CHECK( shape["hasSecretAccessKey"].asBool() );
  CHECK( shape["anonymous"].asBool() == false );
  CHECK( shape["secretAccessKey"].isNull() );
  const Json::Value report = resolved.toJson();
  CHECK( report["bucket"].asString() == "eo-scenes" );
  CHECK( report["needsCredentials"].asBool() );
}

TEST_CASE( "credential windows install and exactly restore GDAL config state",
           "[io][fabric][object_store]" )
{
  REQUIRE( activeScopedCredentialWindows() == 0 );

  const std::string before = CPLGetConfigOption( "AWS_ACCESS_KEY_ID", "" );
  {
    ObjectStoreCredentials credentials;
    credentials.accessKeyId = "AKIA_TEST";
    credentials.secretAccessKey = "TOPSECRET";
    credentials.region = "us-west-2a";
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    CHECK( activeScopedCredentialWindows() == 1 );
    CHECK( std::string( CPLGetConfigOption( "AWS_ACCESS_KEY_ID", "" ) ) == "AKIA_TEST" );
    CHECK( std::string( CPLGetConfigOption( "AWS_SECRET_ACCESS_KEY", "" ) ) == "TOPSECRET" );
    CHECK( std::string( CPLGetConfigOption( "AWS_REGION", "" ) ) == "us-west-2a" );
    CHECK( std::string( CPLGetConfigOption( "AWS_NO_SIGN_REQUEST", "" ) ).empty() );
    CHECK( std::string( CPLGetConfigOption( "AWS_NO_SIGNREQUEST", "" ) ).empty() );
  }
  CHECK( activeScopedCredentialWindows() == 0 );
  CHECK( std::string( CPLGetConfigOption( "AWS_ACCESS_KEY_ID", "" ) ) == before );   // exactly restored

  // A prior ambient value survives the window untouched.
  CPLSetConfigOption( "AWS_SESSION_TOKEN", "ambient" );
  {
    ObjectStoreCredentials credentials;
    credentials.anonymous = true;
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    CHECK( std::string( CPLGetConfigOption( "AWS_NO_SIGN_REQUEST", "" ) ) == "YES" );
  }
  CHECK( std::string( CPLGetConfigOption( "AWS_SESSION_TOKEN", "" ) ) == "ambient" );
  CHECK( CPLGetConfigOption( "AWS_NO_SIGN_REQUEST", nullptr ) == nullptr );
  CPLSetConfigOption( "AWS_SESSION_TOKEN", nullptr );

  // Typed contract failures.
  ObjectStoreCredentials incomplete;
  incomplete.accessKeyId = "only-the-id";
  REQUIRE_THROWS_AS( ScopedObjectStoreCredentials( "/vsis3/", incomplete ), GeoError );
  ObjectStoreCredentials azureWithEndpoint;
  azureWithEndpoint.accessKeyId = "account";
  azureWithEndpoint.secretAccessKey = "key";
  azureWithEndpoint.endpoint = "http://127.0.0.1:10000";
  REQUIRE_THROWS_AS( ScopedObjectStoreCredentials( "/vsiaz/", azureWithEndpoint ), GeoError );
  ObjectStoreCredentials gcsSigned;
  gcsSigned.accessKeyId = "x";
  gcsSigned.secretAccessKey = "y";
  REQUIRE_THROWS_AS( ScopedObjectStoreCredentials( "/vsigs/", gcsSigned ), GeoError );
  REQUIRE_THROWS_AS( ScopedObjectStoreCredentials( "/vsimem/", ObjectStoreCredentials{} ), GeoError );
  CHECK( activeScopedCredentialWindows() == 0 );   // failures never leak windows
}

TEST_CASE( "the credential window hands GDAL a scheme-free endpoint",
           "[io][fabric][object_store][endpoint]" )
{
  // GDAL versions disagree on whether AWS_S3_ENDPOINT may carry a scheme:
  // builds that prepend their own compose "http://http://…" request URLs.
  // The window, not the GDAL build, owns the decomposition — bare
  // host[:port] plus an explicit AWS_HTTPS — so the transport behaves the
  // same on every supported GDAL.
  ObjectStoreCredentials credentials;
  credentials.accessKeyId = "k";
  credentials.secretAccessKey = "s";

  credentials.endpoint = "http://127.0.0.1:9000";
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    CHECK( std::string( CPLGetConfigOption( "AWS_S3_ENDPOINT", "" ) ) == "127.0.0.1:9000" );
    CHECK( std::string( CPLGetConfigOption( "AWS_HTTPS", "" ) ) == "NO" );
    CHECK( std::string( CPLGetConfigOption( "AWS_VIRTUAL_HOSTING", "" ) ) == "FALSE" );
  }

  credentials.endpoint = "HTTPS://Files.Example.com/";
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    CHECK( std::string( CPLGetConfigOption( "AWS_S3_ENDPOINT", "" ) ) == "Files.Example.com" );
    CHECK( std::string( CPLGetConfigOption( "AWS_HTTPS", "" ) ) == "YES" );
  }

  // A scheme-less endpoint reaches GDAL verbatim (host[:port] only, no
  // trailing slash) and leaves AWS_HTTPS exactly as the ambient environment
  // had it — captured so a developer machine that exports AWS_HTTPS does
  // not produce a false red.
  credentials.endpoint = "127.0.0.1:9000";
  const bool hadAmbientHttps = CPLGetConfigOption( "AWS_HTTPS", nullptr ) != nullptr;
  const std::string ambientHttps = CPLGetConfigOption( "AWS_HTTPS", "" );
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    CHECK( std::string( CPLGetConfigOption( "AWS_S3_ENDPOINT", "" ) ) == "127.0.0.1:9000" );
    CHECK( ( CPLGetConfigOption( "AWS_HTTPS", nullptr ) != nullptr ) == hadAmbientHttps );
    if ( hadAmbientHttps )
      CHECK( std::string( CPLGetConfigOption( "AWS_HTTPS", "" ) ) == ambientHttps );
  }
  CHECK( activeScopedCredentialWindows() == 0 );
}

TEST_CASE( "an s3:// asset opens through the real GDAL S3 stack against a loopback endpoint",
           "[io][fabric][object_store][integration]")
{
  const std::string dir = scratchDir( "s3loop" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  REQUIRE( payload.size() > 4096 );

  testsupport::HttpS3Server server( "eo-bucket", "scene.tif", payload, "\"etag-loop-1\"" );
  REQUIRE( server.valid() );
  REQUIRE( server.port() > 0 );

  ObjectStoreCredentials credentials;
  credentials.accessKeyId = "loopback";
  credentials.secretAccessKey = "loopback-secret";
  credentials.endpoint = server.endpoint();   // path-style HTTP against loopback

  {
    // The window spans open AND read: GDAL authenticates lazily per request.
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    RasterReader reader = RasterReader::open( "/vsis3/eo-bucket/scene.tif" );
    REQUIRE( reader.isOpen() );
    CHECK( reader.metadata().width == 96 );
    CHECK( reader.metadata().height == 96 );

    const std::vector<double> window64 =
      reader.readWindow( { 1 }, { 0, 0, 64, 64 } );
    REQUIRE( window64.size() == 64 * 64 );
    CHECK( rampMismatches( window64, 64, 64 ) == 0 );
  }
  CHECK( activeScopedCredentialWindows() == 0 );

  // The origin saw real HTTP traffic (not a cached path): at least the open
  // probe and the window reads landed.
  CHECK( server.requestCount() >= 1 );
  CHECK( server.bytesServed() > 0 );
  CHECK( server.bytesServed() < payload.size() * 4 );   // bounded reads, not loops

  // Anonymous windows work for public buckets on the same endpoint.
  ObjectStoreCredentials anonymous;
  anonymous.anonymous = true;
  anonymous.endpoint = server.endpoint();
  {
    ScopedObjectStoreCredentials window( "/vsis3/", anonymous );
    RasterReader reader = RasterReader::open( "/vsis3/eo-bucket/scene.tif" );
    CHECK( reader.isOpen() );
  }
}

TEST_CASE( "offline refusal and cached-path helpers follow the layer contracts",
           "[io][fabric][object_store][offline]" )
{
  // Offline gate off: no refusal, helper passes through.
  offline::setEnabled( false );
  std::string refusal;
  CHECK( !fabricOfflineRefusal( "/vsis3/bucket/key", refusal ) );
  CHECK( refusal.empty() );

  // Offline gate on: network VSI targets are refused with the typed message.
  offline::setEnabled( true );
  CHECK( fabricOfflineRefusal( "/vsis3/bucket/key", refusal ) );
  CHECK( !refusal.empty() );
  CHECK( fabricOfflineRefusal( "https://host/file.tif", refusal ) );
  std::string localRefusal;
  CHECK( !fabricOfflineRefusal( "/vsimem/x.tif", localRefusal ) );   // local VSI is not remote
  CHECK( !fabricOfflineRefusal( "/tmp/plain.tif", localRefusal ) );
  offline::setEnabled( false );

  // fabricCachedPath defers to the range cache install state.
  CHECK( fabricCachedPath( "https://host/file.tif" ) == "https://host/file.tif" );
  RemoteRangeCache::install( {} );
  CHECK( fabricCachedPath( "https://host/file.tif" ) == "/vsirangecache/https://host/file.tif" );
  RemoteRangeCache::uninstall();
  CHECK( RemoteRangeCache::installed() == false );
}

TEST_CASE( "nested credential windows hand the cache fingerprint back to the outer window",
           "[io][fabric][object_store][identity]" )
{
  // Exact restore, same discipline as the config-key journal: closing the
  // inner window must restore the still-open outer window's OWN fingerprint.
  // Resetting to the empty context silently re-keys the outer principal's
  // fetches into the shared (context-less) keyspace — the cross-principal
  // merge D-1102 exists to prevent.
  ObjectStoreCredentials outerCreds;
  outerCreds.accessKeyId = "AKIA_TEST_OUTER";
  outerCreds.secretAccessKey = "outer-secret";
  outerCreds.region = "us-west-2a";
  ObjectStoreCredentials innerCreds;
  innerCreds.accessKeyId = "AKIA_TEST_INNER";
  innerCreds.secretAccessKey = "inner-secret";
  innerCreds.region = "us-west-2a";

  const std::string outerContext = objectStoreCredentialContext( "/vsis3/", outerCreds );
  const std::string innerContext = objectStoreCredentialContext( "/vsis3/", innerCreds );
  REQUIRE( !outerContext.empty() );
  REQUIRE( outerContext != innerContext );
  REQUIRE( currentRangeCacheCredentialContext().empty() );

  {
    ScopedObjectStoreCredentials outer( "/vsis3/", outerCreds );
    CHECK( currentRangeCacheCredentialContext() == outerContext );
    {
      ScopedObjectStoreCredentials inner( "/vsis3/", innerCreds );
      CHECK( currentRangeCacheCredentialContext() == innerContext );
    }
    // The outer window is still open and must still be keyed as itself.
    CHECK( currentRangeCacheCredentialContext() == outerContext );
  }
  CHECK( currentRangeCacheCredentialContext().empty() );
  CHECK( activeScopedCredentialWindows() == 0 );
}

TEST_CASE( "closing the LAST credential window always lands the context at empty",
           "[io][fabric][object_store][identity]" )
{
  // Non-LIFO interleaving (outside the D-1003 contract, but it must not
  // REGRESS): A opens, B opens, A closes, B closes. B's exact-restore would
  // pin A's fingerprint — a principal with no live window — into the
  // process-global context, merging every later unwindowed fetch into A's
  // cache partition. The at-rest answer is the shared empty context.
  ObjectStoreCredentials credsA;
  credsA.accessKeyId = "AKIA_TEST_A";
  credsA.secretAccessKey = "a-secret";
  credsA.region = "us-west-2a";
  ObjectStoreCredentials credsB;
  credsB.accessKeyId = "AKIA_TEST_B";
  credsB.secretAccessKey = "b-secret";
  credsB.region = "us-west-2a";
  const std::string contextA = objectStoreCredentialContext( "/vsis3/", credsA );
  const std::string contextB = objectStoreCredentialContext( "/vsis3/", credsB );
  REQUIRE( contextA != contextB );

  ScopedObjectStoreCredentials *a = new ScopedObjectStoreCredentials( "/vsis3/", credsA );
  {
    ScopedObjectStoreCredentials b( "/vsis3/", credsB );
    CHECK( currentRangeCacheCredentialContext() == contextB );
    delete a;   // A closes while B is live: B stays keyed as itself
    CHECK( currentRangeCacheCredentialContext() == contextB );
  }
  // B was the LAST window: at rest the context must be empty, never A's.
  CHECK( currentRangeCacheCredentialContext().empty() );
  CHECK( activeScopedCredentialWindows() == 0 );
}
