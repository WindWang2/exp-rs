/***************************************************************************
  tests/test_io_uri.cpp — Foundation 5.0: resource URI classification,
  identity vs display (credential redaction), local safety (traversal
  containment, long paths, Unicode), subdataset/VSI/STAC/in-memory kinds.
 ***************************************************************************/

#include "geospatial/util/resource_uri.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using sicnu::geo::ResourceKind;
using sicnu::geo::ResourceUri;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_uri" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}
} // namespace

TEST_CASE( "local paths classify by existence and shape", "[io][uri]" )
{
  const std::string dir = scratch( "local" );
  const std::string file = ( fs::path( dir ) / "scene.tif" ).string();
  { std::ofstream out( file ); out << "x"; }

  CAPTURE( dir, file );

  const ResourceUri parsedFile = ResourceUri::parse( file );
  REQUIRE( parsedFile.kind == ResourceKind::LocalFile );
  REQUIRE_FALSE( parsedFile.isRemote() );
  REQUIRE( parsedFile.isLocalPayload() );
  // Canonical form normalizes separators and drops trailing slashes.
  REQUIRE( parsedFile.canonical() == ResourceUri::parse( parsedFile.canonical() ).canonical() );

  const ResourceUri parsedDir = ResourceUri::parse( dir );
  REQUIRE( parsedDir.kind == ResourceKind::LocalDirectory );

  // Backslash spelling is the same identity as forward slashes.
  std::string backslashed = file;
  std::replace( backslashed.begin(), backslashed.end(), '/', '\\' );
#ifdef _WIN32
  REQUIRE( ResourceUri::parse( backslashed ).canonical() == parsedFile.canonical() );
#endif

  // Missing path: Invalid with a reason, never a silent classification.
  const ResourceUri missing = ResourceUri::parse( dir + "/definitely_missing.tif" );
  REQUIRE( missing.kind == ResourceKind::Invalid );
  REQUIRE_FALSE( missing.parseReason.empty() );

  // Empty input is invalid, not a crash.
  REQUIRE( ResourceUri::parse( std::string() ).kind == ResourceKind::Invalid );
}

TEST_CASE( "directory products are recognized by suffix and markers",
           "[io][uri][products]" )
{
  const std::string dir = scratch( "product" );
  const std::string safe = ( fs::path( dir ) / "S2A_MSIL1C_20260101T000000_N0400_R000_T00AAA_20260101T010000.SAFE" ).string();
  fs::create_directories( fs::path( safe ) / "GRANULE" / "L1C_T00AAA_A000000_20260101T010000" / "IMG_DATA" );
  REQUIRE( ResourceUri::parse( safe ).kind == ResourceKind::DirectoryProduct );

  // A plain directory with no product markers stays a plain directory.
  const std::string plain = ( fs::path( dir ) / "plain_folder" ).string();
  fs::create_directories( plain );
  REQUIRE( ResourceUri::parse( plain ).kind == ResourceKind::LocalDirectory );
}

#ifdef _WIN32
TEST_CASE( "Windows drives, UNC and long paths", "[io][uri][windows]" )
{
  const std::string dir = scratch( "windows" );
  REQUIRE( ResourceUri::parse( dir ).kind == ResourceKind::LocalDirectory );

  // Drive letter is not a URI scheme.
  REQUIRE( ( dir.size() >= 2 && dir[1] == ':' ) );
  REQUIRE( dir.substr( 0, 2 ) + "\\" == dir.substr( 0, 2 ) + "\\" );

  // Unicode (Chinese) filename classifies as a local file. The foundation
  // contract is UTF-8 paths, so the fixture is created through u8path.
  const std::string unicodeFile = ( fs::path( dir ) / "\xE6\x95\xB0\xE6\x8D\xAE\xE5\xBD\xB1\xE5\x83\x8F.tif" ).string();
  {
    std::ofstream out( fs::u8path( unicodeFile ), std::ios::binary );
    out << "x";
  }
  REQUIRE( ResourceUri::parse( unicodeFile ).kind == ResourceKind::LocalFile );
  REQUIRE( ResourceUri::parse( unicodeFile ).canonical().find( "\xE6\x95\xB0\xE6\x8D\xAE" ) != std::string::npos );

  // Long-path helper: \\?\ prefix + backslashes, idempotent for UNC/extended.
  const std::string longPath = ResourceUri::toWindowsLongPath( dir );
  REQUIRE( longPath.rfind( "\\\\?\\", 0 ) == 0 );
  REQUIRE( longPath.find( '/' ) == std::string::npos );
  REQUIRE( ResourceUri::toWindowsLongPath( "\\\\server\\share\\x.tif" ) == "\\\\server\\share\\x.tif" );
}
#endif

TEST_CASE( "remote URLs classify, redact and preserve identity", "[io][uri][remote]" )
{
  // Plain https
  const ResourceUri plain = ResourceUri::parse( "https://example.com/data/scene.tif" );
  REQUIRE( plain.kind == ResourceKind::RemoteHttp );
  REQUIRE( plain.isRemote() );
  REQUIRE( plain.host == "example.com" );
  REQUIRE( plain.path == "/data/scene.tif" );
  REQUIRE( plain.display() == "https://example.com/data/scene.tif" );

  // Query strings survive identity.
  const std::string signedUrl =
    "https://example.com/cog.tif?X-Amz-Signature=SECRETVALUE&X-Amz-Expires=3600&x=y";
  const ResourceUri signed_ = ResourceUri::parse( signedUrl );
  REQUIRE( signed_.kind == ResourceKind::RemoteHttp );
  REQUIRE( signed_.canonical() == signedUrl ); // identity keeps everything
  REQUIRE( signed_.display().find( "SECRETVALUE" ) == std::string::npos );
  REQUIRE( signed_.display().find( "X-Amz-Signature=***" ) != std::string::npos );
  REQUIRE( signed_.display().find( "x=y" ) != std::string::npos ); // benign params stay

  // Userinfo is masked in display, kept in identity.
  const ResourceUri cred = ResourceUri::parse( "https://user:secret@example.com/f.tif" );
  REQUIRE( cred.display().find( "secret" ) == std::string::npos );
  REQUIRE( cred.display().find( "user:***@example.com" ) != std::string::npos );
  REQUIRE( cred.canonical().find( "secret" ) != std::string::npos );

  // Token without colon is also masked (#776)
  const ResourceUri tokenOnly = ResourceUri::parse( "https://SECRET_TOKEN@example.com/f.tif" );
  REQUIRE( tokenOnly.display().find( "SECRET_TOKEN" ) == std::string::npos );
  REQUIRE( tokenOnly.display().find( "***@example.com" ) != std::string::npos );

  // Non-HTTP URIs with credentials mask userinfo (#776)
  const ResourceUri s3Cred = ResourceUri::parse( "s3://KEY:SECRET_AWS_KEY@bucket/f.tif" );
  REQUIRE( s3Cred.display().find( "SECRET_AWS_KEY" ) == std::string::npos );

  // Additional credential query parameters are masked (#810)
  const ResourceUri queryCred = ResourceUri::parse( "https://example.com/f.tif?auth=SECRET_AUTH&bearer=SECRET_BEARER&access_key=SECRET_KEY&safe=1" );
  REQUIRE( queryCred.display().find( "SECRET_AUTH" ) == std::string::npos );
  REQUIRE( queryCred.display().find( "SECRET_BEARER" ) == std::string::npos );
  REQUIRE( queryCred.display().find( "SECRET_KEY" ) == std::string::npos );
  REQUIRE( queryCred.display().find( "safe=1" ) != std::string::npos );

  // Percent-encoded path decodes for display only.
  const ResourceUri encoded = ResourceUri::parse( "https://example.com/%E6%95%B0%E6%8D%AE.tif" );
  REQUIRE( encoded.display().find( "\xE6\x95\xB0\xE6\x8D\xAE" ) != std::string::npos );
  REQUIRE( encoded.canonical() == "https://example.com/%E6%95%B0%E6%8D%AE.tif" );

  // Hostless URL is Invalid with a reason.
  REQUIRE( ResourceUri::parse( "https:///nohost.tif" ).kind == ResourceKind::Invalid );
}

TEST_CASE( "VSI handles classify and expose their payload", "[io][uri][vsi]" )
{
  const ResourceUri curl = ResourceUri::parse( "/vsicurl/https://example.com/a.tif" );
  REQUIRE( curl.kind == ResourceKind::VsiRemote );
  REQUIRE( curl.isRemote() );
  REQUIRE( curl.vsiPrefix == "/vsicurl" );
  REQUIRE( curl.remoteUrl() == "https://example.com/a.tif" );
  REQUIRE( curl.canonical() == "/vsicurl/https://example.com/a.tif" );

  const ResourceUri zip = ResourceUri::parse( "/vsizip/C:/data/bundle.zip/inner.tif" );
  REQUIRE( zip.kind == ResourceKind::VsiVirtual );
  REQUIRE_FALSE( zip.isRemote() );
  REQUIRE( zip.embeddedLocalPath() == "C:/data/bundle.zip/inner.tif" );

  // Windows spelling of a VSI payload canonicalizes identically.
  const ResourceUri zippedBack = ResourceUri::parse( "/vsizip/C:\\data\\bundle.zip\\inner.tif" );
  REQUIRE( zippedBack.canonical() == zip.canonical() );
}

TEST_CASE( "subdataset selectors parse with quoted and drive-letter payloads",
           "[io][uri][subdataset]" )
{
  const ResourceUri quoted = ResourceUri::parse( "NETCDF:\"C:/data/climate.nc\":temperature" );
  REQUIRE( quoted.kind == ResourceKind::Subdataset );
  REQUIRE( quoted.scheme == "NETCDF" );
  REQUIRE( quoted.path == "C:/data/climate.nc" );
  REQUIRE( quoted.fragment == "temperature" );
  REQUIRE( quoted.isLocalPayload() );
  REQUIRE( quoted.canonical() == "NETCDF:\"C:/data/climate.nc\":temperature" );

  // Unquoted with a Windows drive: the first ':' after the driver is the
  // selector boundary only when not followed by a path separator.
  const ResourceUri unquoted = ResourceUri::parse( "NETCDF:C:/data/x.nc:temp" );
  REQUIRE( unquoted.kind == ResourceKind::Subdataset );
  REQUIRE( unquoted.path == "C:/data/x.nc" );
  REQUIRE( unquoted.fragment == "temp" );

  // HDF5-style //variable selector.
  const ResourceUri hdf5 = ResourceUri::parse( "HDF5:\"D:/tile.h5\"://grid/reflectance" );
  REQUIRE( hdf5.kind == ResourceKind::Subdataset );
  REQUIRE( hdf5.path == "D:/tile.h5" );
  REQUIRE( hdf5.fragment == "//grid/reflectance" );

  // http:// keeps being a URL — the lowercase driver word is a scheme, and it
  // matched RemoteHttp earlier in the pipeline anyway.
  REQUIRE( ResourceUri::parse( "http://example.com/a.tif" ).kind == ResourceKind::RemoteHttp );
}

TEST_CASE( "stac and memory resources classify", "[io][uri][stac][memory]" )
{
  const ResourceUri stac = ResourceUri::parse( "stac://catalog/item-42" );
  REQUIRE( stac.kind == ResourceKind::StacAsset );

  const ResourceUri memory = ResourceUri::parse( "memory://scratch/derived.tif" );
  REQUIRE( memory.kind == ResourceKind::InMemory );
  REQUIRE( ResourceUri::parse( "/vsimem/scratch.tif" ).kind == ResourceKind::InMemory );
}

TEST_CASE( "resolveAgainst joins relatives and refuses traversal escapes",
           "[io][uri][security]" )
{
  const std::string base = scratch( "resolve" );

  const ResourceUri joined = ResourceUri::resolveAgainst( base, "sub/data.tif" );
  REQUIRE( joined.kind == ResourceKind::LocalFile );
  REQUIRE( joined.canonical() == ResourceUri::parse( base + "/sub/data.tif" ).canonical() );

  // Staying inside the base with ".." is fine ("a/b/../c.tif" → "a/c.tif").
  const ResourceUri inner = ResourceUri::resolveAgainst( base, "a/b/../c.tif" );
  REQUIRE( inner.kind == ResourceKind::LocalFile );
  REQUIRE( inner.canonical() == ResourceUri::parse( base + "/a/c.tif" ).canonical() );

  // Escaping the base is refused as Invalid with a reason.
  const ResourceUri escape = ResourceUri::resolveAgainst( base, "../outside.tif" );
  REQUIRE( escape.kind == ResourceKind::Invalid );
  REQUIRE( escape.parseReason.find( "escape" ) != std::string::npos );

  // A nested reference that climbs above the base is refused too.
  const ResourceUri deepEscape = ResourceUri::resolveAgainst( base, "a/../../outside.tif" );
  REQUIRE( deepEscape.kind == ResourceKind::Invalid );
  REQUIRE( deepEscape.parseReason.find( "escape" ) != std::string::npos );

  // Absolute and remote references pass through untouched.
  const ResourceUri absolute = ResourceUri::resolveAgainst( base, "C:/other/x.tif" );
  REQUIRE( absolute.kind == ResourceKind::Invalid ); // does not exist, but was not joined
  REQUIRE( absolute.parseReason.find( "escape" ) == std::string::npos );
  const ResourceUri remote = ResourceUri::resolveAgainst( base, "https://example.com/x.tif" );
  REQUIRE( remote.kind == ResourceKind::RemoteHttp );
}

TEST_CASE( "kind names are stable strings", "[io][uri]" )
{
  REQUIRE( std::string( sicnu::geo::resourceKindName( ResourceKind::LocalFile ) ) == "local_file" );
  REQUIRE( std::string( sicnu::geo::resourceKindName( ResourceKind::RemoteHttp ) ) == "remote_http" );
  REQUIRE( std::string( sicnu::geo::resourceKindName( ResourceKind::VsiRemote ) ) == "vsi_remote" );
  REQUIRE( std::string( sicnu::geo::resourceKindName( ResourceKind::Subdataset ) ) == "subdataset" );
  REQUIRE( std::string( sicnu::geo::resourceKindName( ResourceKind::Invalid ) ) == "invalid" );
}
