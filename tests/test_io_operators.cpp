/***************************************************************************
  tests/test_io_operators.cpp — io:* operator family suite (Phase 11).
  Registry/schema/execution through RSOperatorContext on synthetic datasets.
  Links Catch2 + Qt6::Core + sicnu_operators + jsoncpp (test_rs_operator
  pattern); requires the full operator chain build (integration milestone).
 ***************************************************************************/

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <gdal.h>
#include <gdal_priv.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{

void ensureGdal()
{
  static std::once_flag once;
  std::call_once( once, [] { GDALAllRegister(); } );
}

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_operators" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}

/// Minimal 8x8 georeferenced Byte raster with nodata.
std::string makeTinyRaster( const std::string &dir, const std::string &name )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / name ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 8, 8, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  double gt[6] = { 116.0, 0.01, 0.0, 31.0, 0.0, -0.01 };
  REQUIRE( GDALSetGeoTransform( dataset, gt ) == CE_None );
  OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
  REQUIRE( OSRImportFromEPSG( srs, 4326 ) == OGRERR_NONE );
  char *wkt = nullptr;
  REQUIRE( OSRExportToWkt( srs, &wkt ) == OGRERR_NONE );
  GDALSetProjection( dataset, wkt );
  CPLFree( wkt );
  OSRDestroySpatialReference( srs );
  GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
  GDALSetRasterNoDataValue( band, 255 );
  unsigned char pixels[64];
  for ( int i = 0; i < 64; ++i )
    pixels[i] = static_cast<unsigned char>( i % 200 );
  REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, 8, 8, pixels, 8, 8, GDT_Byte, 0, 0 ) == CE_None );
  GDALClose( dataset );
  return path;
}

std::string makeTinyGeoJson( const std::string &dir, const std::string &name )
{
  const std::string path = ( fs::path( dir ) / name ).string();
  std::ofstream out( path );
  out << R"json({
    "type": "FeatureCollection", "name": "pts",
    "crs": { "type": "name", "properties": { "name": "urn:ogc:def:crs:OGC:1.3:CRS84" } },
    "features": [
      { "type": "Feature", "properties": { "id": 1 }, "geometry": { "type": "Point", "coordinates": [116.5, 30.5] } },
      { "type": "Feature", "properties": { "id": 2 }, "geometry": { "type": "Point", "coordinates": [116.6, 30.6] } }
    ]
  })json";
  return path;
}

} // namespace

TEST_CASE( "io: family registers all ten authoritative operators", "[io][operators][registry]" )
{
  const sicnu::operators::RSOperatorRegistry &registry = sicnu::operators::RSOperatorRegistry::instance();
  for ( const char *id : { "io:translate", "io:warp", "io:reproject", "io:clip", "io:convert_format",
                           "io:build_overviews", "io:make_cog", "io:vector_convert", "io:inspect", "io:doctor" } )
  {
    INFO( "operator: " << id );
    CHECK( registry.find( std::string( id ) ) != nullptr );
  }
}

TEST_CASE( "io:inspect returns canonical metadata through the operator seam", "[io][operators]" )
{
  const std::string raster = makeTinyRaster( scratch( "inspect" ), "tiny.tif" );
  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:inspect" );
  REQUIRE( op );
  REQUIRE( op->name() == "io:inspect" );

  sicnu::operators::RSOperatorContext context;
  const Json::Value result = op->execute( [ & ] {
    Json::Value params;
    params["input"] = raster;
    return params;
  }(), context );

  CHECK( result["kind"].asString() == "raster" );
  CHECK( result["driver"].asString() == "GTiff" );
  CHECK( result["width"].asInt() == 8 );
  CHECK( result["crs"]["authid"].asString() == "EPSG:4326" );
}

TEST_CASE( "io:translate converts raster; io:warp refuses missing targetCrs", "[io][operators]" )
{
  const std::string dir = scratch( "translate" );
  const std::string raster = makeTinyRaster( dir, "src.tif" );
  const std::string target = ( fs::path( dir ) / "out.vrt" ).string();

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:translate" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    params["output"] = target;
    params["driver"] = "VRT";
    const Json::Value result = op->execute( params, context );
    CHECK( result["output"].asString() == target );
    CHECK( sicnu::geo::atomic_fs::fileExists( target ) );
  }

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:warp" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    params["output"] = ( fs::path( dir ) / "warped.tif" ).string();
    // targetCrs missing → CRS policy refusal, not a silent guess.
    CHECK_THROWS_AS( op->execute( params, context ), sicnu::operators::RSOperatorError );
  }

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:warp" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    params["output"] = ( fs::path( dir ) / "warped.tif" ).string();
    params["targetCrs"] = "EPSG:3857";
    params["resampling"] = "bilinear";
    const Json::Value result = op->execute( params, context );
    CHECK( result["width"].asInt() > 0 );
  }
}

TEST_CASE( "io:make_cog validates and io:doctor reports findings", "[io][operators]" )
{
  const std::string dir = scratch( "cog_doctor" );
  const std::string raster = makeTinyRaster( dir, "src.tif" );

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:make_cog" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    params["output"] = ( fs::path( dir ) / "cog.tif" ).string();
    const Json::Value result = op->execute( params, context );
    CHECK( result["output"].asString().find( "cog.tif" ) != std::string::npos );
  }

  {
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:doctor" );
    REQUIRE( op );
    sicnu::operators::RSOperatorContext context;
    Json::Value params;
    params["input"] = raster;
    const Json::Value report = op->execute( params, context );
    CHECK( report["kind"].asString() == "doctor_report" );
    CHECK( report["readable"].asBool() );
    CHECK( report["findings"].isArray() );
  }
}

TEST_CASE( "io:vector_convert converts GeoJSON to GeoPackage", "[io][operators][vector]" )
{
  const std::string dir = scratch( "vector" );
  const std::string geojson = makeTinyGeoJson( dir, "pts.geojson" );
  const std::string target = ( fs::path( dir ) / "pts.gpkg" ).string();

  auto op = sicnu::operators::RSOperatorRegistry::instance().create( "io:vector_convert" );
  REQUIRE( op );
  sicnu::operators::RSOperatorContext context;
  Json::Value params;
  params["input"] = geojson;
  params["output"] = target;
  params["driver"] = "GPKG";
  const Json::Value result = op->execute( params, context );
  CHECK( result["feature_count"].asInt64() == 2 );
  CHECK( sicnu::geo::atomic_fs::fileExists( target ) );
}

TEST_CASE( "io: operators declare memory policy and determinism grades", "[io][operators][contract]" )
{
  auto &registry = sicnu::operators::RSOperatorRegistry::instance();
  for ( const char *id : { "io:translate", "io:warp", "io:reproject", "io:clip", "io:convert_format",
                           "io:build_overviews", "io:make_cog", "io:vector_convert", "io:inspect", "io:doctor" } )
  {
    auto op = registry.create( std::string( id ) );
    REQUIRE( op );
    CHECK_FALSE( op->schema().isNull() );
    CHECK( op->determinismGrade() == "bit-exact" || op->determinismGrade() == "tolerance" );
  }
}
