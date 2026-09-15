/***************************************************************************
  tests/test_io_gdal_matrix.cpp — GDAL version/capability matrix (package H):
  feature detection against runtime truth, corrupt/truncated negative
  contracts (typed failures, never crashes, never silent passes), and huge
  LOGICAL datasets handled within bounded memory (declared shape + computed
  budgets, no materialization).
 ***************************************************************************/

#include "geospatial/io/gdal_feature_probe.h"
#include "geospatial/common.h"
#include "geospatial/cog/cog_validator.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/io/finalize_manifest.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/util/gdal_compat.h"

#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_vrt.h>
#include <ogr_api.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

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
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_gdal_matrix" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

std::string makeSmallRaster( const std::string &dir, const std::string &name )
{
  ensureGdal();
  const std::string path = ( fs::path( dir ) / name ).string();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver );
  GDALDatasetH dataset = GDALCreate( driver, path.c_str(), 300, 300, 1, GDT_Byte, nullptr );
  REQUIRE( dataset );
  GDALClose( dataset );
  return path;
}

void truncateFile( const std::string &path, std::uintmax_t keepBytes )
{
  std::error_code ec;
  REQUIRE( fs::file_size( fs::u8path( path ) ) > keepBytes );
  fs::resize_file( fs::u8path( path ), keepBytes, ec );
  REQUIRE( !ec );
}

} // namespace

TEST_CASE( "feature report agrees with compile-time macros and runtime drivers", "[io][gdal_matrix]" )
{
  const Json::Value report = gdalFeatureReport();
  INFO( report.toStyledString() );
  CHECK( report["gdal_release"].asString().size() >= 3 );

  // Compile-time truth: the reported macro states must equal the macros the
  // TEST TU was compiled with (same header, same GDAL).
  CHECK( report["version_macros"]["int64_datatypes"].asBool() == ( SICNU_GDAL_INT64_DATATYPES != 0 ) );
  CHECK( report["version_macros"]["vsi_handle_err_api"].asBool() == ( SICNU_GDAL_VSI_HANDLE_ERR_API != 0 ) );
  CHECK( report["version_macros"]["vsi_remove_handler"].asBool() == ( SICNU_GDAL_VSI_REMOVE_HANDLER != 0 ) );
  CHECK( report["version_macros"]["vsi_open_returns_unique_ptr"].asBool()
         == ( SICNU_GDAL_VSI_OPEN_RETURNS_UNIQUE_PTR != 0 ) );
  CHECK( report["version_macros"]["vsi_handle_read_bytes"].asBool() == ( SICNU_GDAL_VSI_HANDLE_READ_BYTES != 0 ) );

  // Runtime truth: GTiff must exist for this suite to make sense at all.
  REQUIRE( GDALGetDriverByName( "GTiff" ) );
  CHECK( report["drivers"]["GTiff"]["present"].asBool() );
  const bool cogPresent = GDALGetDriverByName( "COG" ) != nullptr;
  CHECK( report["drivers"]["COG"]["present"].asBool() == cogPresent );
  const bool parquetPresent = GDALGetDriverByName( "Parquet" ) != nullptr;
  CHECK( report["drivers"]["Parquet"]["present"].asBool() == parquetPresent );
}

TEST_CASE( "truncated COG: validator refuses typed, verifier proves digest drift",
           "[io][gdal_matrix][negative]" )
{
  const std::string dir = scratch( "truncated_cog" );
  const std::string raster = makeSmallRaster( dir, "src.tif" );
  const std::string cog = ( fs::path( dir ) / "cog.tif" ).string();
  const TranslateResult result = makeCog( raster, cog, CogPreset::LosslessScientific, {}, nullptr );
  REQUIRE( result.width == 300 );

  // Manifest binds the healthy bytes.
  FinalizeManifestFields fields;
  fields.producer = "test-harness";
  fields.driver = "GTiff";
  fields.width = 300;
  fields.height = 300;
  fields.bandCount = 1;
  fields.dtype = "Byte";
  writeFinalizeManifest( cog, buildFinalizeManifest( cog, fields ) );
  REQUIRE( verifyDataset( cog ).verified );

  // Truncate to a quarter: a TIFF-shaped file with a broken body.
  truncateFile( cog, fs::file_size( fs::u8path( cog ) ) / 4 );

  // The validator must refuse LOUDLY (typed open failure), not crash and not
  // pass.
  CHECK_THROWS_AS( validateCog( cog ), GeoError );

  // The verifier must fail closed with a digest verdict.
  const ManifestVerifyReport report = verifyDataset( cog );
  INFO( report.toJson().toStyledString() );
  CHECK( report.manifestPresent );
  CHECK_FALSE( report.verified );
  CHECK_FALSE( report.digestMatched );
}

TEST_CASE( "garbage bytes never validate as COG", "[io][gdal_matrix][negative]" )
{
  const std::string dir = scratch( "garbage" );
  const std::string path = ( fs::path( dir ) / "junk.tif" ).string();
  {
    std::ofstream out( path, std::ios::binary );
    out << "not a tiff at all, just text bytes padded to some length 0123456789";
  }
  CHECK_THROWS_AS( validateCog( path ), GeoError );
}

TEST_CASE( "truncated GeoPackage opens as a typed failure, not a crash", "[io][gdal_matrix][negative]" )
{
  ensureGdal();
  GDALDriverH gpkg = GDALGetDriverByName( "GPKG" );
  if ( !gpkg )
  {
    WARN( "GPKG driver not available; truncated-GPKG contract skipped (profile stays honest)" );
    return;
  }
  const std::string dir = scratch( "truncated_gpkg" );
  const std::string path = ( fs::path( dir ) / "pts.gpkg" ).string();
  {
    GDALDatasetH dataset = GDALCreate( gpkg, path.c_str(), 0, 0, 0, GDT_Unknown, nullptr );
    REQUIRE( dataset );
    OGRLayerH layer = GDALDatasetCreateLayer( dataset, "pts", nullptr, wkbPoint, nullptr );
    REQUIRE( layer );
    OGRFeatureH feature = OGR_F_Create( OGR_L_GetLayerDefn( layer ) );
    REQUIRE( feature );
    OGRGeometryH geometry = nullptr;
    char wktBuffer[] = "POINT (1 2)";
    char *wktPtr = wktBuffer; // OGR_G_CreateFromWkt takes char** (mutable)
    REQUIRE( OGR_G_CreateFromWkt( &wktPtr, nullptr, &geometry ) == OGRERR_NONE );
    OGR_F_SetGeometry( feature, geometry );
    REQUIRE( OGR_L_CreateFeature( layer, feature ) == OGRERR_NONE );
    OGR_F_Destroy( feature );
    GDALClose( dataset );
  }
  truncateFile( path, fs::file_size( fs::u8path( path ) ) / 2 );

  try
  {
    inspectVector( path );
    FAIL( "a truncated GeoPackage must not inspect as healthy" );
  }
  catch ( const GeoError &error )
  {
    // Typed failure from the declared vocabulary — any open/corrupt-shaped
    // code is a PASS here; a silent success is the only FAIL.
    CHECK( ( error.code() == ErrorCode::OpenFailed || error.code() == ErrorCode::CorruptData
             || error.code() == ErrorCode::InvalidMetadata || error.code() == ErrorCode::WriteFailed
             || error.code() == ErrorCode::IoError ) );
  }
}

TEST_CASE( "huge logical VRT stays bounded: declared shape, computed budget, no materialization",
           "[io][gdal_matrix][scale]" )
{
  ensureGdal();
  const std::string dir = scratch( "huge_logical" );
  const std::string source = makeSmallRaster( dir, "src.tif" );
  // 40000 x 40000 declared (1.6e9 cells) — logically huge, physically empty.
  const std::string vrt = ( fs::path( dir ) / "huge.vrt" ).string();
  {
    std::ofstream out( vrt );
    out << "<VRTDataset rasterXSize=\"40000\" rasterYSize=\"40000\">\n"
        << "  <VRTRasterBand dataType=\"Byte\" band=\"1\">\n"
        << "    <SimpleSource>\n"
        << "      <SourceFilename relativeToVRT=\"1\">"
        << fs::u8path( source ).filename().string() << "</SourceFilename>\n"
        << "      <SourceBand>1</SourceBand>\n"
        << "      <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"300\" ySize=\"300\"/>\n"
        << "      <DstRect xOff=\"0\" yOff=\"0\" xSize=\"40000\" ySize=\"40000\"/>\n"
        << "    </SimpleSource>\n"
        << "  </VRTRasterBand>\n"
        << "</VRTDataset>\n";
  }

  // Inspection reads headers only: it must succeed within bounded memory and
  // report the DECLARED logical shape.
  const RasterMetadata meta = inspectRaster( vrt );
  CHECK( meta.width == 40000 );
  CHECK( meta.height == 40000 );

  // The read-side budget is COMPUTED, never allocated: readWindow buffers
  // doubles, so a full single-band window is cells x sizeof(double).
  const RasterWindow full{ 0, 0, 40000, 40000 };
  const std::size_t budget = RasterReader::windowByteBudget( meta, full, { 1 } );
  CHECK( budget == static_cast<std::size_t>( 40000 ) * 40000 * sizeof( double ) );

  // Reading the whole logical window is REFUSED by the contract (typed
  // budget error), never attempted: 1.6e9 cells exceed the default budget.
  RasterReader reader = RasterReader::open( vrt );
  std::vector<double> sink;
  CHECK_THROWS_AS( reader.readWindow( { 1 }, full ), GeoError );
  CHECK( sink.empty() );
}
