/***************************************************************************
  tests/test_io_cog_options.cpp — explicit COG production options (11.0).
  Known-answer on the plan (which key comes from where), negatives on illegal
  knobs, production checks through the REAL pipeline: deterministic byte
  equality (same GDAL/libtiff build), explicit blocksize visible to the COG
  validator, overview refusal semantics.
 ***************************************************************************/

#include "geospatial/io/cog_options.h"
#include "geospatial/common.h"
#include "geospatial/cog/cog_validator.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/io/finalize_manifest.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>

#include <filesystem>
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
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_cog_options" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

/// 1024x700 Byte raster: big enough that overview/tiling checks bite.
std::string makeBigRaster( const std::string &dir, const std::string &name )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / name ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 1024, 700, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
  // Write in row stripes to keep the fixture cheap but non-trivial.
  std::vector<unsigned char> row( 1024 );
  for ( int y = 0; y < 700; ++y )
  {
    for ( int x = 0; x < 1024; ++x )
      row[static_cast<std::size_t>( x )] = static_cast<unsigned char>( ( x * 7 + y * 13 ) % 256 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, y, 1024, 1, row.data(), 1024, 1, GDT_Byte, 0, 0 ) == CE_None );
  }
  GDALClose( dataset );
  return path;
}

bool hasOption( const std::vector<std::string> &options, const std::string &key, const std::string &value )
{
  for ( const std::string &option : options )
  {
    if ( option == key + "=" + value )
      return true;
  }
  return false;
}

} // namespace

TEST_CASE( "planCogProduction keeps preset values when no overrides are given", "[io][cog_options]" )
{
  CogProductionOptions settings;
  settings.expectedDtypeName = "Byte";
  const CogProductionPlan plan = planCogProduction( settings );
  CHECK( hasOption( plan.creationOptions, "BLOCKSIZE", "512" ) );
  CHECK( hasOption( plan.creationOptions, "OVERVIEWS", "AUTO" ) );
  CHECK( hasOption( plan.creationOptions, "NUM_THREADS", "ALL_CPUS" ) );
  // The only warning is the preset's own standing advisory.
  REQUIRE( plan.warnings.size() == 1 );
  CHECK( plan.warnings.front().find( "lossless preset" ) != std::string::npos );
  CHECK( plan.explanation["preset"].asString() == "lossless_scientific" );
}

TEST_CASE( "planCogProduction replaces preset keys in place for explicit knobs", "[io][cog_options]" )
{
  CogProductionOptions settings;
  settings.expectedDtypeName = "Byte";
  settings.blocksize = 1024;
  settings.deterministic = true;
  settings.deflateLevel = 9;
  const CogProductionPlan plan = planCogProduction( settings );
  CHECK( hasOption( plan.creationOptions, "BLOCKSIZE", "1024" ) );
  CHECK( hasOption( plan.creationOptions, "NUM_THREADS", "1" ) );
  CHECK( hasOption( plan.creationOptions, "LEVEL", "9" ) );
  // Only ONE of each key (the in-place replace, not an append).
  int blockSizeKeys = 0;
  for ( const std::string &option : plan.creationOptions )
  {
    if ( option.rfind( "BLOCKSIZE=", 0 ) == 0 )
      ++blockSizeKeys;
  }
  CHECK( blockSizeKeys == 1 );
  // Explanation records the override provenance.
  bool blocksizeExplained = false;
  for ( const Json::Value &entry : plan.explanation["options"] )
  {
    if ( entry["key"].asString() == "BLOCKSIZE" && entry["source"].asString() == "override" )
      blocksizeExplained = true;
  }
  CHECK( blocksizeExplained );
}

TEST_CASE( "planCogProduction refuses illegal knobs before any GDAL call", "[io][cog_options][negative]" )
{
  CogProductionOptions settings;
  settings.expectedDtypeName = "Byte";

  settings.blocksize = 100; // not a power of two
  CHECK_THROWS_AS( planCogProduction( settings ), GeoError );

  settings.blocksize = 8192; // above the range
  CHECK_THROWS_AS( planCogProduction( settings ), GeoError );

  settings.blocksize = 0; // preset default
  settings.deflateLevel = 0;
  settings.deterministic = true;
  CHECK_THROWS_AS( planCogProduction( settings ), GeoError );
}

TEST_CASE( "planCogProduction warns when caller extras replace preset keys", "[io][cog_options]" )
{
  CogProductionOptions settings;
  settings.expectedDtypeName = "Byte";
  settings.extraCreationOptions = { "BLOCKSIZE=256", "GEOTIFF_VERSION=1.1" };
  const CogProductionPlan plan = planCogProduction( settings );
  CHECK( hasOption( plan.creationOptions, "BLOCKSIZE", "256" ) );
  CHECK( hasOption( plan.creationOptions, "GEOTIFF_VERSION", "1.1" ) );
  bool sawOverrideWarning = false;
  for ( const std::string &warning : plan.warnings )
    sawOverrideWarning |= warning.find( "BLOCKSIZE" ) != std::string::npos;
  CHECK( sawOverrideWarning );
}

TEST_CASE( "deterministic COG production yields byte-identical output in this build",
           "[io][cog_options][determinism]" )
{
  const std::string dir = scratch( "determinism" );
  const std::string raster = makeBigRaster( dir, "src.tif" );

  CogProductionOptions settings;
  settings.expectedDtypeName = "Byte";
  settings.deterministic = true;
  settings.deflateLevel = 6;
  const CogProductionPlan plan = planCogProduction( settings );
  REQUIRE( hasOption( plan.creationOptions, "NUM_THREADS", "1" ) );

  const std::string outA = ( fs::path( dir ) / "a.tif" ).string();
  const std::string outB = ( fs::path( dir ) / "b.tif" ).string();
  const TranslateResult ra = makeCogWithOptions( raster, outA, plan.creationOptions, nullptr );
  const TranslateResult rb = makeCogWithOptions( raster, outB, plan.creationOptions, nullptr );
  CHECK( ra.width == rb.width );

  // Independent oracle: FIPS-validated SHA-256 over the two files.
  const std::string digestA = datasetSha256Hex( outA );
  const std::string digestB = datasetSha256Hex( outB );
  INFO( "digestA=" << digestA << " digestB=" << digestB );
  CHECK( digestA == digestB );

  const CogValidationReport report = validateCog( outA );
  INFO( report.toJson().toStyledString() );
  CHECK( report.isCog );
}

TEST_CASE( "explicit blocksize reaches the produced COG and stays validator-clean",
           "[io][cog_options][production]" )
{
  const std::string dir = scratch( "blocksize" );
  const std::string raster = makeBigRaster( dir, "src.tif" );

  CogProductionOptions settings;
  settings.expectedDtypeName = "Byte";
  settings.blocksize = 256;
  const CogProductionPlan plan = planCogProduction( settings );
  REQUIRE( hasOption( plan.creationOptions, "BLOCKSIZE", "256" ) );

  const std::string out = ( fs::path( dir ) / "cog256.tif" ).string();
  makeCogWithOptions( raster, out, plan.creationOptions, nullptr );

  // Independent oracle: the validator reads the block geometry itself.
  const CogValidationReport report = validateCog( out );
  INFO( report.toJson().toStyledString() );
  CHECK( report.isCog );

  const RasterMetadata meta = inspectRaster( out );
  REQUIRE( !meta.bands.empty() );
  // canonical metadata reports block shape? If absent, use GDAL directly:
  GDALDatasetH handle = GDALOpenEx( out.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER, nullptr, nullptr, nullptr );
  REQUIRE( handle );
  int blockX = 0;
  int blockY = 0;
  GDALGetBlockSize( GDALGetRasterBand( handle, 1 ), &blockX, &blockY );
  GDALClose( handle );
  CHECK( blockX == 256 );
  CHECK( blockY == 256 );
}

TEST_CASE( "overviews=false is refused for images beyond the small-image allowance",
           "[io][cog_options][negative]" )
{
  const std::string dir = scratch( "no_overviews" );
  const std::string raster = makeBigRaster( dir, "src.tif" );

  CogProductionOptions settings;
  settings.expectedDtypeName = "Byte";
  settings.buildOverviews = false;
  const CogProductionPlan plan = planCogProduction( settings );
  REQUIRE( hasOption( plan.creationOptions, "OVERVIEWS", "NONE" ) );

  // A 1024x700 output without overviews can never pass the COG validator,
  // and the pipeline validates BEFORE publish: the transaction must abort
  // and leave nothing behind (fail-closed, no partial artifact).
  const std::string out = ( fs::path( dir ) / "flat.tif" ).string();
  CHECK_THROWS_AS( makeCogWithOptions( raster, out, plan.creationOptions, nullptr ), GeoError );
  CHECK( !fs::exists( fs::u8path( out ) ) );

  // Within the small-image allowance (≤512 px) the same options are legal.
  ensureGdal();
  const std::string tinyPath = ( fs::path( dir ) / "tiny_src.tif" ).string();
  {
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH dataset = GDALCreate( driver, tinyPath.c_str(), 128, 128, 1, GDT_Byte, nullptr );
    REQUIRE( dataset );
    GDALClose( dataset );
  }
  const std::string tinyOut = ( fs::path( dir ) / "tiny_flat.tif" ).string();
  const TranslateResult result = makeCogWithOptions( tinyPath, tinyOut, plan.creationOptions, nullptr );
  CHECK( result.width == 128 );
  const CogValidationReport report = validateCog( tinyOut );
  INFO( report.toJson().toStyledString() );
  CHECK( report.isCog );
}
