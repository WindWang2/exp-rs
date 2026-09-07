/***************************************************************************
  tests/test_io_probe.cpp — Foundation 5.0: probe contract (ADR 0136).
  Content outranks the file name; COG detection is structural; product
  adapters claim paths; capabilities answer per-dataset; failures are typed.
 ***************************************************************************/

#include "geospatial/cog/cog_validator.h"
#include "geospatial/crs/crs_policy.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/formats/format_profiles.h"
#include "geospatial/probe/probe.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace sicnu::geo;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_probe" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

std::string buildRaster( const std::string &path, int width = 64, int height = 64 )
{
  RasterWriter writer = RasterWriter::create( path, width, height, { RasterBandSpec {} } );
  writer.setGeotransform( { 460000.0, 10.0, 0.0, 5450000.0, 0.0, -10.0 } );
  writer.setCrs( Crs::fromAuthid( "EPSG:32633" ) );
  std::vector<double> values( static_cast<std::size_t>( width ) * height, 3.0 );
  writer.writeWindow( 1, { 0, 0, width, height }, values.data() );
  writer.finalize();
  return path;
}
} // namespace

TEST_CASE( "probe classifies a raster by content even with a wrong extension",
           "[io][probe][signature]" )
{
  const std::string dir = scratch( "wrongext" );
  // 1024² stripped (untiled) TIFF: too big for the COG tiny-image allowance,
  // so the structural COG verdict is genuinely false for this fixture.
  buildRaster( dir + "/real.tif", 1024, 1024 );

  // Copy the GeoTIFF bytes to a ".json" name: the extension is a hint, the
  // TIFF magic is the truth.
  fs::copy_file( fs::u8path( dir + "/real.tif" ), fs::u8path( dir + "/fake.json" ) );

  const ProbeResult result = probeResource( dir + "/fake.json" );
  CHECK( ( result.decidedBy == ProbeStage::GdalDriver || result.decidedBy == ProbeStage::Signature ) );
  CHECK( result.format.driverName == "GTiff" );
  CHECK( result.signature == FileSignature::Tiff );
  CHECK( result.isCog == false ); // structural, not name-based
}

TEST_CASE( "probe detects a produced COG structurally", "[io][probe][cog]" )
{
  const std::string dir = scratch( "cog" );
  buildRaster( dir + "/plain.tif" );
  TranslateResult cog = makeCog( dir + "/plain.tif", dir + "/cog.tif", CogPreset::LosslessScientific );
  REQUIRE( !cog.output.empty() );

  const CogValidationReport report = validateCog( dir + "/cog.tif" );
  REQUIRE( report.isCog );

  const ProbeResult result = probeResource( dir + "/cog.tif" );
  CHECK( result.isCog );
  CHECK( result.format.driverName == "GTiff" ); // identify says GTiff; isCog refines
}

TEST_CASE( "probe reports product families ahead of plain formats",
           "[io][probe][products]" )
{
  const std::string dir = scratch( "product" );
  const std::string safe =
    dir + "/S2A_MSIL1C_20260610T100031_N0400_R122_T33UUU_20260610T120000.SAFE";
  fs::create_directories( fs::u8path( safe + "/GRANULE" ) );
  { std::ofstream out( fs::u8path( safe + "/manifest.safe" ) ); out << "<x/>"; }

  const ProbeResult result = probeResource( safe );
  CHECK( result.product.kind == ProductKind::Sentinel2Safe );
  CHECK( result.decidedBy == ProbeStage::ProductAdapter );

  // A plain raster with no product sidecars stays GenericRaster (reported,
  // but the format stage decides).
  buildRaster( dir + "/plain.tif" );
  const ProbeResult plain = probeResource( dir + "/plain.tif" );
  CHECK( plain.product.kind == ProductKind::GenericRaster );
  CHECK( plain.decidedBy != ProbeStage::ProductAdapter );
}

TEST_CASE( "probe failures are typed: NotFound, CorruptData",
           "[io][probe][failure]" )
{
  SECTION( "missing path" )
  {
    bool threw = false;
    try
    {
      probeResource( "C:/no/such/thing.tif" );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      CHECK( error.code() == ErrorCode::NotFound );
    }
    CHECK( threw );
  }

  SECTION( "TIFF magic but truncated structure → CorruptData" )
  {
    const std::string dir = scratch( "corrupt" );
    buildRaster( dir + "/scene.tif" );
    {
      std::ifstream in( fs::u8path( dir + "/scene.tif" ), std::ios::binary );
      const std::string head{ std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
      std::ofstream out( fs::u8path( dir + "/truncated.tif" ), std::ios::binary );
      out.write( head.data(), 64 ); // TIFF magic + a broken IFD
    }
    bool threw = false;
    try
    {
      probeResource( dir + "/truncated.tif" );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      CHECK( error.code() == ErrorCode::CorruptData );
    }
    CHECK( threw );
  }
}

TEST_CASE( "dataset capabilities answer per dataset, not per format",
           "[io][probe][capabilities]" )
{
  const std::string dir = scratch( "caps" );

  SECTION( "a plain single-band GeoTIFF" )
  {
    buildRaster( dir + "/plain.tif" );
    const ResolvedCapabilities caps = resolveDatasetCapabilities( dir + "/plain.tif" );
    CHECK( hasCapability( caps.caps, DataCapability::WindowRead ) );
    CHECK( hasCapability( caps.caps, DataCapability::BlockRead ) );
    CHECK( hasCapability( caps.caps, DataCapability::Georeferencing ) );
    CHECK( hasCapability( caps.caps, DataCapability::Crs ) );
    CHECK( hasCapability( caps.caps, DataCapability::Streaming ) );
    // Absence answers are as important as presence:
    CHECK( !hasCapability( caps.caps, DataCapability::Overviews ) );
    CHECK( !hasCapability( caps.caps, DataCapability::Multiband ) );
    CHECK( !hasCapability( caps.caps, DataCapability::Vector ) );
    CHECK( caps.driver == "GTiff" );
    CHECK_FALSE( caps.remote );
  }

  SECTION( "multiband + declared nodata + overviews" )
  {
    const std::string path = dir + "/rich.tif";
    {
      RasterWriter writer = RasterWriter::create(
        path, 32, 32, { RasterBandSpec {}, RasterBandSpec {} },
        { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
      writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
      std::vector<double> values( 32 * 32, 1.0 );
      writer.writeWindow( 1, { 0, 0, 32, 32 }, values.data() );
      writer.writeWindow( 2, { 0, 0, 32, 32 }, values.data() );
      writer.finalize();
    }
    const ResolvedCapabilities caps = resolveDatasetCapabilities( path );
    CHECK( hasCapability( caps.caps, DataCapability::Multiband ) );
    CHECK( hasCapability( caps.caps, DataCapability::Overviews ) == false ); // none built yet
    // Overviews built in place are the one sanctioned in-place op:
    buildOverviews( path, { 2 }, "NEAREST" );
    const ResolvedCapabilities after = resolveDatasetCapabilities( path );
    CHECK( hasCapability( after.caps, DataCapability::Overviews ) );
  }
}

TEST_CASE( "capability names form the stable JSON surface", "[io][probe][capabilities]" )
{
  const std::vector<std::string> names = capabilityNames(
    DataCapability::WindowRead | DataCapability::RemoteRange | DataCapability::Subdataset );
  REQUIRE( names.size() == 3 );
  const bool has = []( const std::vector<std::string> &list, const std::string &name ) {
    return std::find( list.begin(), list.end(), name ) != list.end();
  }( names, "window_read" )
    && []( const std::vector<std::string> &list, const std::string &name ) {
    return std::find( list.begin(), list.end(), name ) != list.end();
  }( names, "remote_range" )
    && []( const std::vector<std::string> &list, const std::string &name ) {
    return std::find( list.begin(), list.end(), name ) != list.end();
  }( names, "subdataset" );
  CHECK( has );
}
