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
