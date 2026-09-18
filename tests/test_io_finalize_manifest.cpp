/***************************************************************************
  tests/test_io_finalize_manifest.cpp — finalize manifest known-answer suite.
  Digest oracle: FIPS 180-4 known-answer vector + byte-flip negative; the
  manifest schema is checked against its declared version, never against the
  code that produced a fixture (fixtures are built here, the verifier
  re-derives everything from disk).
 ***************************************************************************/

#include "geospatial/io/finalize_manifest.h"
#include "geospatial/common.h"
#include "geospatial/util/sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

using namespace sicnu::geo;
using namespace sicnu::geo::io;

namespace
{

void ensureGdal()
{
  static std::once_flag once;
  std::call_once( once, [] { GDALAllRegister(); } );
}

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_finalize_manifest" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

/// Minimal 6x7 Byte raster written directly with GDAL (independent of the
/// writer under test's transaction machinery).
std::string makeTinyRaster( const std::string &dir, const std::string &name )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / name ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 6, 7, 2, GDT_Byte, nullptr );
  REQUIRE( dataset );
  GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
  unsigned char pixels[42];
  for ( int i = 0; i < 42; ++i )
    pixels[i] = static_cast<unsigned char>( i * 3 % 251 );
  REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 6, 7, pixels, 6, 7, GDT_Byte, 0, 0 ) == CE_None );
  GDALClose( dataset );
  return path;
}

void flipOneByte( const std::string &path )
{
  std::fstream file( path, std::ios::in | std::ios::out | std::ios::binary );
  REQUIRE( file.is_open() );
  file.seekg( 0, std::ios::end );
  const std::streamoff size = file.tellg();
  file.seekp( size / 2 );
  char byte = '\0';
  file.read( &byte, 1 );
  file.seekp( size / 2 );
  file.put( static_cast<char>( ~byte ) );
  REQUIRE( file.good() );
}

} // namespace

TEST_CASE( "datasetSha256Hex matches the FIPS 180-4 known-answer vector", "[io][manifest][digest]" )
{
  const std::string dir = scratch( "known" );
  const std::string path = dir + "/abc.bin";
  { std::ofstream out( path, std::ios::binary ); out << "abc"; }
  // Independent oracle: the FIPS 180-4 "abc" vector.
  CHECK( datasetSha256Hex( path ) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
  CHECK_THROWS_AS( datasetSha256Hex( dir + "/missing.bin" ), GeoError );
}

TEST_CASE( "finalize manifest roundtrips and verifies a healthy dataset", "[io][manifest]" )
{
  const std::string dir = scratch( "roundtrip" );
  const std::string tif = makeTinyRaster( dir, "scene.tif" );

  FinalizeManifestFields fields;
  fields.producer = "test-harness";
  fields.driver = "GTiff";
  fields.width = 6;
  fields.height = 7;
  fields.bandCount = 2;
  fields.dtype = "Byte";
  fields.crsAuthid = "";
  fields.creationOptions = { "COMPRESS=DEFLATE" };

  const Json::Value manifest = buildFinalizeManifest( tif, fields );
  CHECK( manifest["schema_version"].asInt() == 1 );
  CHECK( manifest["dataset_sha256"].asString().size() == 64 );
  CHECK( manifest["dataset_bytes"].asUInt64() == static_cast<Json::UInt64>( fs::file_size( fs::u8path( tif ) ) ) );
  CHECK( manifest["finalized_utc"].asString().find( 'Z' ) != std::string::npos );
  writeFinalizeManifest( tif, manifest );

  const Json::Value readBack = readFinalizeManifest( tif );
  REQUIRE( readBack["dataset_sha256"].asString() == manifest["dataset_sha256"].asString() );

  const ManifestVerifyReport report = verifyDataset( tif );
  INFO( report.toJson().toStyledString() );
  CHECK( report.verified );
  CHECK( report.digestMatched );
  CHECK( report.manifestPresent );
  CHECK( report.issues.empty() );
}

TEST_CASE( "verifyDataset fails closed on tampering and missing manifests", "[io][manifest][negative]" )
{
  const std::string dir = scratch( "tamper" );
  const std::string tif = makeTinyRaster( dir, "scene.tif" );

  SECTION( "missing manifest is unverifiable, allowed-or-not is visible in the issue code" )
  {
    ManifestVerifyReport report = verifyDataset( tif );
    CHECK( !report.verified );
    REQUIRE( report.issues.size() == 1 );
    CHECK( report.issues.front().code == "manifest_missing" );

    report = verifyDataset( tif, /*allowMissingManifest=*/true );
    CHECK( !report.verified ); // absence is reported, never green-washed
    REQUIRE( report.issues.size() == 1 );
    CHECK( report.issues.front().code == "manifest_missing_allowed" );
  }

  SECTION( "flipped payload byte must break the digest" )
  {
    FinalizeManifestFields fields;
    fields.producer = "test-harness";
    fields.driver = "GTiff";
    fields.width = 6;
    fields.height = 7;
    fields.bandCount = 2;
    writeFinalizeManifest( tif, buildFinalizeManifest( tif, fields ) );

    flipOneByte( tif );
    const ManifestVerifyReport report = verifyDataset( tif );
    INFO( report.toJson().toStyledString() );
    CHECK( !report.verified );
    CHECK( !report.digestMatched );
    bool digestIssue = false;
    for ( const ManifestIssue &issue : report.issues )
      digestIssue |= issue.code == "digest_mismatch";
    CHECK( digestIssue );
  }

  SECTION( "shape drift must surface" )
  {
    FinalizeManifestFields fields;
    fields.producer = "test-harness";
    fields.driver = "GTiff";
    fields.width = 999;
    fields.height = 7;
    fields.bandCount = 2;
    writeFinalizeManifest( tif, buildFinalizeManifest( tif, fields ) );
    const ManifestVerifyReport report = verifyDataset( tif );
    CHECK( report.digestMatched ); // bytes untouched
    CHECK( !report.verified );
    bool shapeIssue = false;
    for ( const ManifestIssue &issue : report.issues )
      shapeIssue |= issue.code == "shape_mismatch";
    CHECK( shapeIssue );
  }

  SECTION( "corrupt manifest JSON is InvalidMetadata, not a crash" )
  {
    { std::ofstream out( tif + ".sicnu-manifest.json", std::ios::binary ); out << "{ not json"; }
    const ManifestVerifyReport report = verifyDataset( tif );
    CHECK( !report.verified );
    bool invalidIssue = false;
    for ( const ManifestIssue &issue : report.issues )
      invalidIssue |= issue.code == "manifest_invalid";
    CHECK( invalidIssue );
  }

  SECTION( "foreign schema version is refused" )
  {
    { std::ofstream out( tif + ".sicnu-manifest.json", std::ios::binary ); out << "{\"schema_version\": 99}"; }
    const ManifestVerifyReport report = verifyDataset( tif );
    CHECK( !report.verified );
    bool invalidIssue = false;
    for ( const ManifestIssue &issue : report.issues )
      invalidIssue |= issue.code == "manifest_invalid";
    CHECK( invalidIssue );
  }

  // #1038: foreign-typed manifest values must land in the report as
  // manifest_invalid issues, never as an escaping Json::LogicError (the
  // conversions happen OUTSIDE the read guard's try).
  SECTION( "foreign-typed digest field is a typed issue" )
  {
    { std::ofstream out( tif + ".sicnu-manifest.json", std::ios::binary );
      out << "{\"schema_version\": 1, \"dataset_sha256\": []}"; }
    const ManifestVerifyReport report = verifyDataset( tif );
    CHECK( !report.verified );
    bool invalidIssue = false;
    for ( const ManifestIssue &issue : report.issues )
      invalidIssue |= issue.code == "manifest_invalid" &&
                      issue.message.find( "foreign type" ) != std::string::npos;
    CHECK( invalidIssue );
  }

  SECTION( "foreign-typed shape field is a typed issue" )
  {
    { std::ofstream out( tif + ".sicnu-manifest.json", std::ios::binary );
      out << "{\"schema_version\": 1, \"dataset_sha256\": \"abc\", \"shape\": {\"width\": \"w\"}}"; }
    const ManifestVerifyReport report = verifyDataset( tif );
    CHECK( !report.verified );
    bool invalidIssue = false;
    for ( const ManifestIssue &issue : report.issues )
      invalidIssue |= issue.code == "manifest_invalid" &&
                      issue.message.find( "declared_shape" ) != std::string::npos;
    CHECK( invalidIssue );
  }
}

TEST_CASE( "manifests survive Unicode directory names", "[io][manifest][unicode]" )
{
  const std::string dir = scratch( "都市_é" );
  const std::string tif = makeTinyRaster( dir, "影像.tif" );
  FinalizeManifestFields fields;
  fields.producer = "test-harness";
  fields.driver = "GTiff";
  fields.width = 6;
  fields.height = 7;
  fields.bandCount = 2;
  writeFinalizeManifest( tif, buildFinalizeManifest( tif, fields ) );
  const ManifestVerifyReport report = verifyDataset( tif );
  CHECK( report.verified );
  CHECK( report.displayPath.find( "影像" ) != std::string::npos );
}
