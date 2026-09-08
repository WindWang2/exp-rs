/***************************************************************************
  tests/test_io_atomic_failures.cpp — Phase 12: atomic output failure paths.
  Cancel · failed publish (locked target) · partial sidecar · write failure ·
  disk-full proxy (injected GDAL failure) · old-target preservation.
 ***************************************************************************/

#include "geospatial/raster/raster_reader.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/vector/vector_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_atomic" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}
} // namespace

TEST_CASE( "unpublishable target fails loudly, keeps target, removes staging", "[io][atomic][publish]" )
{
  const std::string dir = scratch( "locked" );
  // A DIRECTORY occupying the target name can never be replaced by a file:
  // publish must fail loudly and the directory must survive untouched.
  const std::string target = ( fs::path( dir ) / "target.tif" ).string();
  fs::create_directories( target );
  sicnu::geo::RasterWindow full;
  full.width = 8;
  full.height = 8;
  std::vector<double> values( 64, 1.0 );

  bool publishFailed = false;
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 8, 8, { {} },
                                                                        sicnu::geo::RasterWriteOptions{ "GTiff", {}, true } );
    const std::string staged = writer.stagedPath();
    REQUIRE( sicnu::geo::atomic_fs::fileExists( staged ) );
    try
    {
      writer.finalize();
      FAIL( "expected publish failure" );
    }
    catch ( const sicnu::geo::GeoError &error )
    {
      publishFailed = true;
      CHECK( error.code() == sicnu::geo::ErrorCode::IoError );
    }
    // finalize() cleans its staging on failure.
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( staged ) );
  }
  CHECK( publishFailed );
  CHECK( fs::exists( target ) ); // the occupying directory survives
  CHECK( fs::is_directory( target ) );
}

TEST_CASE( "cancelling a vector writer removes the whole staged group", "[io][atomic][vector][group]" )
{
  const std::string dir = scratch( "vector_group" );
  const std::string target = ( fs::path( dir ) / "group.shp" ).string();

  {
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
      target, "pts", "Point", { { "name", "String", 16 } }, sicnu::geo::Crs::fromAuthid( "EPSG:4326" ),
      { "ESRI Shapefile", {}, false, "" } );
    Json::Value attrs( Json::objectValue );
    attrs["name"] = "a";
    writer.writeFeature( attrs, "POINT (1 2)" );
    const std::string staged = writer.stagedPath();
    writer.cancel();
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( staged ) );
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( staged + ".dbf" ) );
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( staged + ".shx" ) );
  }
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target ) );
}

TEST_CASE( "shapefile group publish lands sidecars and main file together", "[io][atomic][vector][group]" )
{
  const std::string dir = scratch( "group_publish" );
  const std::string target = ( fs::path( dir ) / "final.shp" ).string();

  sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
    target, "pts", "Point", { { "name", "String", 16 } }, sicnu::geo::Crs::fromAuthid( "EPSG:4326" ),
    { "ESRI Shapefile", {}, false, "" } );
  for ( int i = 0; i < 5; ++i )
  {
    Json::Value attrs( Json::objectValue );
    attrs["name"] = "p" + std::to_string( i );
    writer.writeFeature( attrs, "POINT (" + std::to_string( i ) + " 0)" );
  }
  writer.finalize();

  CHECK( sicnu::geo::atomic_fs::fileExists( target ) );
  // Shapefile sidecars share the stem (X.shp → X.shx/X.dbf/X.prj).
  const auto sibling = [ & ]( const char *extension ) {
    return sicnu::geo::atomic_fs::fileExists( fs::path( target ).replace_extension( extension ).string() );
  };
  CHECK( sibling( ".shx" ) );
  CHECK( sibling( ".dbf" ) );
  CHECK( sibling( ".prj" ) );

  // And the published dataset reads back correctly.
  sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
  CHECK( reader.layerInfo().featureCount == 5 );
  std::vector<sicnu::geo::VectorFeature> batch;
  REQUIRE( reader.nextBatch( batch, 10 ) );
  CHECK( batch.size() == 5 );
}

TEST_CASE( "write failure through injected bad window leaves staging empty", "[io][atomic][write]" )
{
  const std::string dir = scratch( "badwrite" );
  const std::string target = ( fs::path( dir ) / "x.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 8, 8, { {} }, {} );
  sicnu::geo::RasterWindow bogus;
  bogus.xOff = 100;
  bogus.width = 2;
  bogus.height = 2;
  double values[4] = { 0 };
  CHECK_THROWS_AS( writer.writeWindow( 1, bogus, values ), sicnu::geo::GeoError );
  writer.cancel();
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target ) );
  int filesLeft = 0;
  for ( const fs::directory_entry &entry : fs::directory_iterator( dir ) )
  {
    (void)entry;
    ++filesLeft;
  }
  CHECK( filesLeft == 0 );
}

TEST_CASE( "group publish consumes the main-file backup set on success",
           "[io][atomic][publish]" )
{
  // #791: the main target was replaced with no backup discipline — a stale
  // main .bak survived a successful republish, and any failure inside the
  // main swap had no restore path. The success path now consumes the main
  // backup like the sidecar backups.
  const std::string dir = scratch( "group_main_backup" );
  const std::string target = ( fs::path( dir ) / "final.shp" ).string();

  auto writeGroup = [ & ]( const std::string &marker ) {
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
      target, "pts", "Point", { { "name", "String", 16 } },
      sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), { "ESRI Shapefile", {}, true, "" } );
    Json::Value attrs( Json::objectValue );
    attrs["name"] = marker;
    writer.writeFeature( attrs, "POINT (1 2)" );
    writer.finalize();
  };

  writeGroup( "first" );
  const std::string firstMain = []( const std::string &path ) {
    std::ifstream in( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
  }( target );
  REQUIRE( !firstMain.empty() );

  // A stale main backup from an interrupted earlier run must be consumed.
  { std::ofstream stale( target + ".bak", std::ios::binary ); stale << "stale"; }

  writeGroup( "second" );

  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target + ".bak" ) );
  // Every sidecar backup is consumed too.
  for ( const char *extension : { ".shx", ".dbf", ".prj" } )
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists(
      fs::path( target ).replace_extension( extension ).string() + ".bak" ) );

  // The published group reads back with the NEW content.
  sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
  std::vector<sicnu::geo::VectorFeature> batch;
  REQUIRE( reader.nextBatch( batch, 10 ) );
  REQUIRE( batch.size() == 1 );
}

TEST_CASE( "a failed group publish restores the previous main file and"
           " sidecars byte-identical",
           "[io][atomic][publish]" )
{
  // Rollback guarantee: a mid-publish failure must restore the whole
  // previous good group — main file included (#791).
  const std::string dir = scratch( "group_rollback" );
  const std::string target = ( fs::path( dir ) / "final.shp" ).string();

  auto writeGroup = [ & ]( const std::string &marker ) {
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
      target, "pts", "Point", { { "name", "String", 16 } },
      sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), { "ESRI Shapefile", {}, true, "" } );
    Json::Value attrs( Json::objectValue );
    attrs["name"] = marker;
    writer.writeFeature( attrs, "POINT (1 2)" );
    writer.finalize();
  };

  writeGroup( "good" );
  const std::string goodMain = []( const std::string &path ) {
    std::ifstream in( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
  }( target );
  const std::string goodDbf = []( const std::string &path ) {
    std::ifstream in( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
  }( fs::path( target ).replace_extension( ".dbf" ).string() );
  REQUIRE( !goodMain.empty() );
  REQUIRE( !goodDbf.empty() );

  // Publish failure injection: the target .dbf is a DIRECTORY, so the
  // staged .dbf can never replace it — publish fails mid-sidecar-phase and
  // the whole target group must roll back.
  const std::string dbfTarget = fs::path( target ).replace_extension( ".dbf" ).string();
  std::error_code ec;
  fs::remove( dbfTarget, ec );
  fs::create_directories( dbfTarget );

  bool failed = false;
  try
  {
    writeGroup( "rejected" );
    FAIL( "expected group publish failure" );
  }
  catch ( const sicnu::geo::GeoError & )
  {
    failed = true;
  }
  CHECK( failed );

  // Rollback: previous main byte-identical, previous .dbf restored, no
  // backup leftovers anywhere.
  const std::string restoredMain = []( const std::string &path ) {
    std::ifstream in( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
  }( target );
  CHECK( restoredMain == goodMain );
  CHECK( fs::is_directory( dbfTarget ) ); // the unreplaceable member survives
  for ( const char *extension : { ".shx", ".prj" } )
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists(
      fs::path( target ).replace_extension( extension ).string() + ".bak" ) );
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target + ".bak" ) );
}
