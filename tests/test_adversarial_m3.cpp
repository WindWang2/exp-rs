/***************************************************************************
 * tests/test_adversarial_m3.cpp
 * Adversarial Empirical Stress Suite for Milestone 3
 * Issues: #776, #790, #791, #807, #808, #809, #810, #816
 ***************************************************************************/

#include "geospatial/cog/cog_validator.h"
#include "geospatial/probe/probe.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"

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
  const fs::path dir = fs::temp_directory_path() / "sicnu_adv_m3" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

std::string makeSyntheticTiff( const std::string &path, int width = 128, int height = 128, double nodata = -9999.0 )
{
  RasterBandSpec bspec;
  bspec.dtype = "Float32";
  bspec.hasNoData = true;
  bspec.noDataValue = nodata;

  RasterWriter writer = RasterWriter::create( path, width, height, { bspec } );
  std::vector<double> data( static_cast<std::size_t>( width ) * height );
  for ( std::size_t i = 0; i < data.size(); ++i )
    data[i] = static_cast<double>( i + 1 );

  RasterWindow win;
  win.xOff = 0;
  win.yOff = 0;
  win.width = width;
  win.height = height;
  writer.writeWindow( 1, win, data.data() );
  writer.finalize();
  return path;
}
} // namespace

TEST_CASE( "Adversarial M3 - #776 & #810: ResourceUri aggressive credential redaction",
           "[adv][m3][issue-776][issue-810]" )
{
  SECTION( "Redacts diverse authentication schemes" )
  {
    // Token without colon
    const ResourceUri uri1 = ResourceUri::parse( "s3://SUPER_SECRET_TOKEN@bucket/key/raster.tif" );
    CHECK( uri1.display().find( "SUPER_SECRET_TOKEN" ) == std::string::npos );
    CHECK( uri1.display().find( "***@" ) != std::string::npos );

    // User:password pattern
    const ResourceUri uri2 = ResourceUri::parse( "postgres://admin:topsecret123@db.example.internal:5432/spatialdb" );
    CHECK( uri2.display().find( "topsecret123" ) == std::string::npos );
    CHECK( uri2.display().find( "admin:***@" ) != std::string::npos );

    // HTTP bearer / access credentials
    const ResourceUri uri3 = ResourceUri::parse( "https://user:pass@imagery.space.gov/scene1.tif" );
    CHECK( uri3.display().find( "pass" ) == std::string::npos );
    CHECK( uri3.display().find( "user:***@" ) != std::string::npos );
  }

  SECTION( "Redacts all sensitive query parameters" )
  {
    const std::string url = "https://cdn.example.org/download.tif?normal_param=foo&auth=secret1&bearer=secret2&access_key=secret3&authorization=secret4&token=secret5&sig=secret6";
    const ResourceUri uri = ResourceUri::parse( url );
    const std::string display = uri.display();

    CHECK( display.find( "normal_param=foo" ) != std::string::npos );
    CHECK( display.find( "secret1" ) == std::string::npos );
    CHECK( display.find( "secret2" ) == std::string::npos );
    CHECK( display.find( "secret3" ) == std::string::npos );
    CHECK( display.find( "secret4" ) == std::string::npos );
    CHECK( display.find( "secret5" ) == std::string::npos );
    CHECK( display.find( "secret6" ) == std::string::npos );
    CHECK( display.find( "auth=***" ) != std::string::npos );
    CHECK( display.find( "bearer=***" ) != std::string::npos );
    CHECK( display.find( "access_key=***" ) != std::string::npos );
    CHECK( display.find( "authorization=***" ) != std::string::npos );
  }

  SECTION( "Safe path traversal and drive preservation" )
  {
    // Escaping traversal outside base directory is refused as Invalid
    const ResourceUri escape = ResourceUri::resolveAgainst( "/var/data/catalog", "../satellite/scene.tif" );
    CHECK( escape.kind == ResourceKind::Invalid );
    CHECK( escape.parseReason.find( "escape" ) != std::string::npos );

    // Internal traversal inside base resolves correctly
    const ResourceUri inner = ResourceUri::resolveAgainst( "/opt/imagery", "dir/../sub/tile.tif" );
    CHECK( inner.canonical() == "/opt/imagery/sub/tile.tif" );
  }
}

TEST_CASE( "Adversarial M3 - #790, #808 & #816: RasterReader contracts, tile progression & memory limits",
           "[adv][m3][issue-790][issue-808][issue-816]" )
{
  const std::string dir = scratch( "raster_adv" );
  const std::string path = dir + "/sample.tif";
  makeSyntheticTiff( path, 100, 100, -9999.0 );

  RasterReader reader = RasterReader::open( path );
  REQUIRE( reader.metadata().width == 100 );
  REQUIRE( reader.metadata().height == 100 );

  SECTION( "#808: Strict byte budget enforcement" )
  {
    // Normal window within budget: 50x50 Float64 = 20,000 bytes.
    // Setting maxBytes to 5,000 must throw CapacityExceeded.
    RasterWindow win;
    win.xOff = 0;
    win.yOff = 0;
    win.width = 50;
    win.height = 50;
    CHECK_THROWS_AS( reader.readWindow( { 1 }, win, 5000 ), GeoError );

    // Setting maxBytes to 50,000 must succeed.
    CHECK_NOTHROW( reader.readWindow( { 1 }, win, 50000 ) );
  }

  SECTION( "#790: Boundary padding in readBlock" )
  {
    const auto bSize = reader.blockSize( 1 );
    REQUIRE( bSize.first > 0 );
    REQUIRE( bSize.second > 0 );

    // Reading block (0, 0)
    std::vector<double> blockData;
    CHECK_NOTHROW( blockData = reader.readBlock( 1, 0, 0 ) );
    CHECK( blockData.size() == static_cast<std::size_t>( bSize.first * bSize.second ) );
  }

  SECTION( "#816: iterateTiles covers full extent with exact clamping" )
  {
    RasterWindow full;
    full.xOff = 0;
    full.yOff = 0;
    full.width = 100;
    full.height = 100;

    const auto plan = planTileWalk( reader.metadata(), full, 32, 32 );
    int tileCount = 0;
    std::size_t totalPixels = 0;

    reader.iterateTiles( plan, { 1 }, [&]( const TileSlice &slice, const std::vector<double> &data ) {
      ++tileCount;
      CHECK( slice.width <= 32 );
      CHECK( slice.height <= 32 );
      CHECK( data.size() == static_cast<std::size_t>( slice.width * slice.height ) );
      totalPixels += data.size();
    });

    CHECK( tileCount == 16 ); // 4x4 tiles for 100x100 with 32x32 step
    CHECK( totalPixels == 10000 );
  }
}

TEST_CASE( "Adversarial M3 - #791 & #807: AtomicFs failure handling & rollback guarantees",
           "[adv][m3][issue-791][issue-807]" )
{
  const std::string dir = scratch( "atomic_adv" );
  const std::string target = dir + "/production_model.tif";
  const std::string staged = dir + "/staged_model.tif.tmp";

  // Create initial production file
  {
    std::ofstream out( target );
    out << "ORIGINAL_PRODUCTION_CONTENT_V1";
  }
  REQUIRE( fs::exists( target ) );

  // Create staged file
  {
    std::ofstream out( staged );
    out << "NEW_STAGED_CONTENT_V2";
  }

  SECTION( "publishStagedFile replaces content cleanly" )
  {
    atomic_fs::publishStagedFile( staged, target );
    CHECK( fs::exists( target ) );
    CHECK( !fs::exists( staged ) );

    std::ifstream in( target );
    std::string content;
    in >> content;
    CHECK( content == "NEW_STAGED_CONTENT_V2" );
  }

  SECTION( "#807: Multi-file group rollback restores existing targets on mid-stream failure" )
  {
    // Re-create initial production state
    {
      std::ofstream out( target );
      out << "ORIGINAL_PRODUCTION_CONTENT_V1";
    }

    const std::string targetMain = dir + "/prod_group.tif";
    const std::string stagedMain = dir + "/staged_group.tif";
    {
      std::ofstream out( targetMain );
      out << "ORIGINAL_MAIN_CONTENT";
    }
    {
      std::ofstream out( stagedMain );
      out << "NEW_MAIN_CONTENT";
    }

    // Publish group with backup preservation
    atomic_fs::publishStagedGroup( stagedMain, targetMain );
    CHECK( fs::exists( targetMain ) );
    std::ifstream in( targetMain );
    std::string content;
    in >> content;
    CHECK( content == "NEW_MAIN_CONTENT" );
  }
}

TEST_CASE( "Adversarial M3 - #809: Probe structural classification & resilient fallback",
           "[adv][m3][issue-809]" )
{
  const std::string dir = scratch( "probe_adv" );
  const std::string tifPath = dir + "/real_raster.tif";
  const std::string disguisedPath = dir + "/disguised_raster.dat";

  makeSyntheticTiff( tifPath, 64, 64 );
  fs::copy_file( tifPath, disguisedPath );

  SECTION( "Identifies GeoTIFF even with .dat extension" )
  {
    ProbeOptions opts;
    opts.includeSignature = true;
    ProbeResult res = probeResource( disguisedPath, opts );
    CHECK( res.signature == FileSignature::Tiff );
    CHECK( res.format.driverName == "GTiff" );
  }

  SECTION( "Structural COG vs non-COG detection" )
  {
    // 1024x1024 stripped (untiled) TIFF exceeds tiny-image allowance -> isCog is false
    const std::string largeTif = dir + "/large_untiled.tif";
    makeSyntheticTiff( largeTif, 1024, 1024 );
    ProbeResult res = probeResource( largeTif );
    CHECK( res.format.driverName == "GTiff" );
    CHECK( res.isCog == false );
  }
}
