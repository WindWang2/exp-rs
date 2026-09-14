/***************************************************************************
  tests/test_io_fabric_operators.cpp — fabric 10.0: the io:catalog_search /
  io:cube_plan / io:cube_window / io:cache_prefetch operators through the
  real registry (schema presence, JSON contracts, execution on synthetic
  scene trees).
 ***************************************************************************/

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace sicnu::operators;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "ops_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

void writeFile( const std::string &path, const std::string &content )
{
  std::ofstream out( path, std::ios::binary );
  out << content;
}

/// One 32×32 scene at (originX, originY), constant value.
std::string writeScene( const std::string &dir, const std::string &name, double originX,
                        double originY, double value )
{
  const std::string path = dir + "/" + name;
  sicnu::geo::RasterWriter writer =
    sicnu::geo::RasterWriter::create( path, 32, 32, { sicnu::geo::RasterBandSpec {} },
                                      { "GTiff", { "TILED=YES", "BLOCKXSIZE=32", "BLOCKYSIZE=32" },
                                        true } );
  writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
  writer.setGeotransform( { originX, 1.0, 0.0, originY + 32.0, 0.0, -1.0 } );
  std::vector<double> raster( 32ull * 32, value );
  writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
  writer.finalize();
  return path;
}

std::string itemJsonFor( const std::string &id, const std::string &datetime, double cloud,
                         const std::string &assetPath )
{
  Json::Value item;
  item["type"] = "Feature";
  item["stac_version"] = "1.0.0";
  item["id"] = id;
  item["bbox"] = [] {
    Json::Value b( Json::arrayValue );
    b.append( 0.0 );
    b.append( 0.0 );
    b.append( 64.0 );
    b.append( 32.0 );
    return b;
  }();
  item["properties"]["datetime"] = datetime;
  item["properties"]["eo:cloud_cover"] = cloud;
  Json::Value data;
  data["href"] = assetPath;
  data["type"] = "image/tiff";
  data["roles"] = [] {
    Json::Value r( Json::arrayValue );
    r.append( "data" );
    return r;
  }();
  item["assets"]["image"] = data;
  return Json::writeString( Json::StreamWriterBuilder(), item );
}

Json::Value parseFile( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
  Json::Value parsed;
  Json::CharReaderBuilder builder;
  std::string errors;
  const bool ok =
    builder.newCharReader()->parse( text.data(), text.data() + text.size(), &parsed, &errors );
  REQUIRE( ok );
  return parsed;
}

/// Builds a STAC tree of two scenes and returns the tree root.
std::string sceneTree( const char *name )
{
  const std::string dir = scratchDir( name );
  writeScene( dir, "clean.tif", 0.0, 0.0, 1.0 );
  writeScene( dir, "cloudy.tif", 32.0, 0.0, 2.0 );
  writeFile( dir + "/item-clean.json",
             itemJsonFor( "item-clean", "2024-05-01T00:00:00Z", 3.0, "clean.tif" ) );
  writeFile( dir + "/item-cloudy.json",
             itemJsonFor( "item-cloudy", "2024-05-02T00:00:00Z", 60.0, "cloudy.tif" ) );
  return dir;
}

} // namespace

TEST_CASE( "the fabric operator family registers with schemas in the io group",
           "[io][fabric][operators]" )
{
  const RSOperatorRegistry &registry = RSOperatorRegistry::instance();
  for ( const char *name : { "io:catalog_search", "io:cube_plan", "io:cube_window",
                             "io:cache_prefetch" } )
  {
    auto op = registry.create( name );
    REQUIRE( op != nullptr );
    CHECK( op->group() == "io" );
    const Json::Value schema = op->schema();
    CHECK( schema["properties"].isObject() );
    CHECK( !op->determinismGrade().empty() );
  }
}

TEST_CASE( "io:catalog_search queries a local STAC tree with the filter vocabulary",
           "[io][fabric][operators]" )
{
  const std::string root = sceneTree( "search" );
  auto op = RSOperatorRegistry::instance().create( "io:catalog_search" );
  REQUIRE( op );

  Json::Value params;
  params["catalog"] = root;
  params["cloudCoverMax"] = 20.0;
  RSOperatorContext context;
  const Json::Value result = op->run( params, context );
  const Json::Value records = result["records"];
  REQUIRE( records.size() == 1 );
  CHECK( records[0]["id"] == "item-clean" );
  CHECK( result["truncatedByCap"].asBool() == false );

  Json::Value cloudless = params;
  cloudless["cloudCoverMax"] = 100.0;
  const Json::Value all = op->run( cloudless, context );
  CHECK( all["records"].size() == 2 );

  // Typed failure: an unknown root is an operator error, not a crash.
  Json::Value bad = params;
  bad["catalog"] = "/definitely/not/anywhere";
  REQUIRE_THROWS_AS( op->run( bad, context ), RSOperatorError );
}

TEST_CASE( "io:cube_plan produces inspectable plans from a catalog URI",
           "[io][fabric][operators]" )
{
  const std::string root = sceneTree( "planops" );
  auto op = RSOperatorRegistry::instance().create( "io:cube_plan" );
  REQUIRE( op );

  Json::Value params;
  params["catalog"] = root;
  params["cloudCoverMax"] = 100.0;
  Json::Value grid;
  grid["crs"] = "EPSG:4326";
  grid["scaleX"] = 1.0;
  grid["scaleY"] = 1.0;
  Json::Value extent( Json::arrayValue );
  extent.append( 0.0 );
  extent.append( 0.0 );
  extent.append( 64.0 );
  extent.append( 32.0 );
  grid["extent"] = extent;
  params["grid"] = grid;
  Json::Value chunkShape;
  chunkShape["time"] = 1;
  chunkShape["y"] = 16;
  chunkShape["x"] = 16;
  chunkShape["band"] = 1;
  params["chunkShape"] = chunkShape;
  params["sceneBudget"] = 4;

  RSOperatorContext context;
  const Json::Value plan = op->run( params, context );
  CHECK( plan["cost"]["scenes"].asUInt64() == 2 );
  // 2 (time) × 4 (x) × 2 (y) = 16 chunks over the 64×32 grid.
  CHECK( plan["cost"]["chunks"].asUInt64() == 16 );
  CHECK( plan["grid"]["crs"].asString() == "EPSG:4326" );
  CHECK( plan["stages"].isArray() );
}

TEST_CASE( "io:cube_window executes a window and publishes it atomically",
           "[io][fabric][operators]" )
{
  const std::string dir = scratchDir( "window" );
  const std::string root = sceneTree( "winops" );
  auto op = RSOperatorRegistry::instance().create( "io:cube_window" );
  REQUIRE( op );

  Json::Value params;
  params["catalog"] = root;
  params["cloudCoverMax"] = 20.0;   // only the clean (left) scene qualifies
  Json::Value grid;
  grid["crs"] = "EPSG:4326";
  grid["scaleX"] = 1.0;
  grid["scaleY"] = 1.0;
  Json::Value extent( Json::arrayValue );
  extent.append( 0.0 );
  extent.append( 0.0 );
  extent.append( 32.0 );
  extent.append( 32.0 );
  grid["extent"] = extent;
  params["grid"] = grid;
  Json::Value window;
  window["x"] = 0;
  window["y"] = 0;
  window["w"] = 32;
  window["h"] = 32;
  params["window"] = window;
  params["output"] = dir + "/window-out.tif";
  params["sceneBudget"] = 2;

  RSOperatorContext context;
  const Json::Value result = op->run( params, context );
  CHECK( result["width"].asInt() == 32 );
  CHECK( result["height"].asInt() == 32 );
  REQUIRE( result["provenance"].size() == 1 );
  CHECK( result["provenance"][0]["assetId"] == "item-clean" );
  REQUIRE( std::filesystem::exists( result["output"].asString() ) );

  // The published raster reads back with the scene's constant value.
  auto reader = sicnu::geo::RasterReader::open( result["output"].asString() );
  const std::vector<double> values = reader.readWindow( { 1 }, { 0, 0, 32, 32 } );
  CHECK( values.front() == 1.0 );
  CHECK( values.back() == 1.0 );
}

TEST_CASE( "io:cache_prefetch refuses to masquerade without the range cache",
           "[io][fabric][operators]" )
{
  const std::string root = sceneTree( "prefetchops" );
  sicnu::geo::RemoteRangeCache::uninstall();
  auto op = RSOperatorRegistry::instance().create( "io:cache_prefetch" );
  REQUIRE( op );

  Json::Value params;
  params["catalog"] = root;
  Json::Value grid;
  grid["crs"] = "EPSG:4326";
  grid["scaleX"] = 1.0;
  grid["scaleY"] = 1.0;
  Json::Value extent( Json::arrayValue );
  extent.append( 0.0 );
  extent.append( 0.0 );
  extent.append( 64.0 );
  extent.append( 32.0 );
  grid["extent"] = extent;
  params["grid"] = grid;
  params["sceneBudget"] = 4;

  RSOperatorContext context;
  REQUIRE_THROWS_AS( op->run( params, context ), RSOperatorError );

  // With the cache installed, local assets report as cache-hits (they pull
  // no origin bytes) and the plan rides along for inspection.
  sicnu::geo::RemoteRangeCache::install( {} );
  const Json::Value result = op->run( params, context );
  // No chunkShape declared: defaults (256×256) cover the whole grid once
  // per time step → exactly 2 chunks.
  CHECK( result["chunks"].size() == 2 );
  CHECK( result["cacheHits"].asUInt64() == 2 );
  CHECK( result["plan"]["cost"]["scenes"].asUInt64() == 2 );
  sicnu::geo::RemoteRangeCache::uninstall();
}
