/***************************************************************************
  tests/test_io_hints.cpp — 9.0 M8: data locality & execution hints.
  Hints are pure facts over existing inspection surfaces: layout-derived
  seek cost, chunk shapes, estimated bytes, identity-backed cacheability —
  and honest absence everywhere a fact is unknown.
 ***************************************************************************/

#include "geospatial/hints/data_locality.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace sicnu::geo;

namespace
{

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_hints9" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

} // namespace

TEST_CASE( "tiled rasters report low seek cost and their tile shape",
           "[io][hints][fabric9]" )
{
  const std::string dir = scratch( "tiled" );
  const std::string target = ( fs::path( dir ) / "tiled.tif" ).string();
  RasterWriter writer = RasterWriter::create(
    target, 512, 512, { RasterBandSpec{} },
    { "GTiff", { "TILED=YES", "BLOCKXSIZE=256", "BLOCKYSIZE=256", "COMPRESS=LZW" }, true } );
  RasterWindow full;
  full.width = 512;
  full.height = 512;
  std::vector<double> values( 512ull * 512, 1.0 );
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  const DataLocalityHints hints = dataLocalityHintsFor( target );
  CHECK( hints.kind == "raster" );
  CHECK( hints.locality == "local" );
  CHECK( hints.seekCostLow );
  CHECK( hints.compression == "LZW" );
  REQUIRE( hints.preferredChunkShape.size() == 2 );
  CHECK( hints.preferredChunkShape[0] == 256 ); // slowest (rows) first
  CHECK( hints.preferredChunkShape[1] == 256 );
  CHECK( hints.hasEstimatedBytes );
  CHECK( hints.estimatedBytes > 0 );
  // A readable local file carries a content-strength identity token: the
  // cacheability fact ("" would mean NOT cacheable).
  CHECK( hints.identityStrength == "content" );
  CHECK( !hints.identityToken.empty() );

  // JSON projection carries the same facts.
  const Json::Value json = hints.toJson();
  CHECK( json["preferred_chunk_shape"][0].asInt64() == 256 );
  CHECK( json["identity_strength"].asString() == "content" );
}

TEST_CASE( "strip rasters honestly report high seek cost",
           "[io][hints][fabric9]" )
{
  const std::string dir = scratch( "strip" );
  const std::string target = ( fs::path( dir ) / "strip.tif" ).string();
  RasterWriter writer = RasterWriter::create( target, 64, 64, { RasterBandSpec{} }, {} );
  RasterWindow full;
  full.width = 64;
  full.height = 64;
  std::vector<double> values( 64ull * 64, 2.0 );
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  const DataLocalityHints hints = dataLocalityHintsFor( target );
  CHECK( hints.kind == "raster" );
  CHECK_FALSE( hints.seekCostLow );
  CHECK_FALSE( hints.cogOptimized );
  CHECK( hints.preferredChunkShape.empty() );
}

TEST_CASE( "vector hints reflect the spatial-filter capability",
           "[io][hints][fabric9]" )
{
  const std::string dir = scratch( "vector" );
  const std::string target = ( fs::path( dir ) / "pts.gpkg" ).string();
  VectorWriter writer = VectorWriter::create( target, "pts", "Point",
                                              { { "v", "Integer" } },
                                              Crs::fromAuthid( "EPSG:4326" ), {} );
  Json::Value attrs( Json::objectValue );
  attrs["v"] = 1;
  writer.writeFeature( attrs, "POINT (1 2)" );
  writer.finalize();

  const DataLocalityHints hints = dataLocalityHintsFor( target );
  CHECK( hints.kind == "vector" );
  CHECK( hints.locality == "local" );
  // GPKG advertises fast spatial filtering.
  CHECK( hints.seekCostLow );
}

TEST_CASE( "unreadable inputs yield unknown-kind hints with no invented facts",
           "[io][hints][fabric9]" )
{
  const std::string dir = scratch( "unknown" );
  const DataLocalityHints hints = dataLocalityHintsFor( ( fs::path( dir ) / "missing.tif" ).string() );
  CHECK( hints.kind == "unknown" );
  CHECK( hints.locality == "local" );
  // Fail-closed identity: no token is invented for an unreadable input.
  CHECK( hints.identityToken.empty() );
  CHECK_FALSE( hints.hasEstimatedBytes );

  const Json::Value json = hints.toJson();
  CHECK( json["kind"].asString() == "unknown" );
  CHECK( json.isMember( "estimated_bytes" ) == false );
  CHECK( json.isMember( "identity_token" ) == false );
}
