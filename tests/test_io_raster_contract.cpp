/***************************************************************************
  tests/test_io_raster_contract.cpp
  Geospatial I/O Foundation 4.0 — raster read/write contract suite.
 ***************************************************************************/

#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

std::string scratchDir( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_raster" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}

} // namespace

TEST_CASE( "window reads return stored values exactly", "[io][raster][contract]" )
{
  const std::string dir = scratchDir( "window" );
  const std::string target = ( fs::path( dir ) / "src.tif" ).string();

  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
    target, 16, 12, { sicnu::geo::RasterBandSpec{} }, {} );
  writer.setGeotransform( { 500000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 } );
  writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:32648" ) );

  // Distinct stored values per row/column.
  std::vector<double> values( 16 * 12 );
  for ( int y = 0; y < 12; ++y )
    for ( int x = 0; x < 16; ++x )
      values[y * 16 + x] = static_cast<double>( y * 100 + x );
  sicnu::geo::RasterWindow full;
  full.width = 16;
  full.height = 12;
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  CHECK( reader.metadata().width == 16 );
  CHECK( reader.metadata().height == 12 );
  CHECK( reader.metadata().crs.authid == "EPSG:32648" );
  REQUIRE( reader.metadata().hasExtent );
  CHECK( reader.metadata().minX == Approx( 500000.0 ) );
  CHECK( reader.metadata().maxY == Approx( 4000000.0 ) );

  // A non-aligned interior window keeps value placement.
  sicnu::geo::RasterWindow window;
  window.xOff = 3;
  window.yOff = 4;
  window.width = 5;
  window.height = 4;
  const std::vector<double> chunk = reader.readWindow( { 1 }, window );
  REQUIRE( chunk.size() == 20 );
  for ( int y = 0; y < 4; ++y )
  {
    for ( int x = 0; x < 5; ++x )
    {
      const double expected = static_cast<double>( ( 4 + y ) * 100 + ( 3 + x ) );
      CHECK( chunk[y * 5 + x] == Approx( expected ) );
    }
  }

  // Multi-band request: band list selects layout band-sequentially.
  sicnu::geo::RasterWriter twoBands = sicnu::geo::RasterWriter::create(
    ( fs::path( dir ) / "twobands.tif" ).string(), 4, 4,
    { sicnu::geo::RasterBandSpec{}, sicnu::geo::RasterBandSpec{} }, {} );
  sicnu::geo::RasterWindow small;
  small.width = 4;
  small.height = 4;
  std::vector<double> b1( 16, 1.0 );
  std::vector<double> b2( 16, 2.0 );
  twoBands.writeWindow( 1, small, b1.data() );
  twoBands.writeWindow( 2, small, b2.data() );
  twoBands.finalize();

  sicnu::geo::RasterReader reader2 = sicnu::geo::RasterReader::open( ( fs::path( dir ) / "twobands.tif" ).string() );
  const std::vector<double> both = reader2.readWindow( { 2, 1 }, small );
  REQUIRE( both.size() == 32 );
  CHECK( both[0] == Approx( 2.0 ) );   // band 2 first
  CHECK( both[16] == Approx( 1.0 ) );  // band 1 second
}

TEST_CASE( "window validation is strict; clamping is an explicit helper", "[io][raster][contract]" )
{
  sicnu::geo::RasterMetadata meta;
  meta.width = 10;
  meta.height = 10;

  sicnu::geo::RasterWindow inside;
  inside.width = 5;
  inside.height = 5;
  std::string error;
  CHECK( sicnu::geo::RasterReader::validateWindow( meta, inside, &error ) );

  sicnu::geo::RasterWindow outside;
  outside.xOff = 8;
  outside.width = 5;
  outside.height = 5;
  CHECK_FALSE( sicnu::geo::RasterReader::validateWindow( meta, outside, &error ) );
  CHECK( error.find( "extent" ) != std::string::npos );

  sicnu::geo::RasterWindow clamped = outside;
  CHECK( sicnu::geo::clampWindowToRaster( meta, clamped ) );
  CHECK( clamped.xOff == 8 );
  CHECK( clamped.width == 2 );

  sicnu::geo::RasterWindow disjoint;
  disjoint.xOff = 50;
  disjoint.yOff = 50;
  disjoint.width = 5;
  disjoint.height = 5;
  CHECK_FALSE( sicnu::geo::clampWindowToRaster( meta, disjoint ) );
}

TEST_CASE( "readFull enforces the declared byte budget", "[io][raster][contract]" )
{
  const std::string target = ( fs::path( scratchDir( "budget" ) ) / "big.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
    target, 64, 64, { sicnu::geo::RasterBandSpec{} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 64;
  full.height = 64;
  std::vector<double> values( 64 * 64, 7.0 );
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  CHECK( reader.readFull( { 1 }, 64 * 64 * sizeof( double ) + 8 ).size() == 64 * 64 );

  try
  {
    reader.readFull( { 1 }, 1024 );
    FAIL( "expected budget refusal" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::Unsupported );
    CHECK( error.details()["required_bytes"].asUInt64() > 1024 );
    CHECK( std::string( error.what() ).find( "budget" ) != std::string::npos );
  }
}

TEST_CASE( "masks mark declared nodata only", "[io][raster][contract]" )
{
  const std::string target = ( fs::path( scratchDir( "mask" ) ) / "masked.tif" ).string();
  sicnu::geo::RasterBandSpec spec;
  spec.hasNoData = true;
  spec.noDataValue = -1.0;
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 4, 4, { spec }, {} );
  std::vector<double> values = { 1, 2, 3, 4, 5, -1, 7, 8, 9, 10, 11, 12, 13, 14, -1, 16 };
  sicnu::geo::RasterWindow full;
  full.width = 4;
  full.height = 4;
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const std::vector<std::uint8_t> mask = reader.readMask( full, { 1 } );
  REQUIRE( mask.size() == 16 );
  CHECK( mask[5] == 0 );
  CHECK( mask[14] == 0 );
  CHECK( mask[0] == 255 );
  CHECK( mask[15] == 255 );

  // NaN nodata declarations mask NaNs, not ordinary values.
  sicnu::geo::RasterBandSpec nanSpec;
  nanSpec.hasNoData = true;
  nanSpec.noDataIsNaN = true;
  const std::string nanTarget = ( fs::path( scratchDir( "mask" ) ) / "nan.tif" ).string();
  sicnu::geo::RasterWriter nanWriter = sicnu::geo::RasterWriter::create( nanTarget, 4, 4, { nanSpec }, {} );
  std::vector<double> nanValues( 16, 3.0 );
  nanValues[1] = std::numeric_limits<double>::quiet_NaN();
  nanWriter.writeWindow( 1, full, nanValues.data() );
  nanWriter.finalize();
  sicnu::geo::RasterReader nanReader = sicnu::geo::RasterReader::open( nanTarget );
  const std::vector<std::uint8_t> nanMask = nanReader.readMask( full, { 1 } );
  CHECK( nanMask[1] == 0 );
  CHECK( nanMask[0] == 255 );
}

TEST_CASE( "writer publishes atomically with metadata fidelity", "[io][raster][contract]" )
{
  const std::string dir = scratchDir( "atomic" );
  const std::string target = ( fs::path( dir ) / "out.tif" ).string();

  sicnu::geo::RasterBandSpec spec;
  spec.dtype = "UInt16";
  spec.description = "Red";
  spec.hasNoData = true;
  spec.noDataValue = 0;
  spec.hasScale = true;
  spec.scale = 0.01;
  spec.hasOffset = true;
  spec.offset = -100.0;
  spec.unit = "reflectance";
  spec.role = "Red";
  spec.hasWavelength = true;
  spec.wavelengthNm = 665.0;
  spec.colorInterpretation = "Red";

  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 8, 8, { spec }, {} );
  writer.setGeotransform( { 1.0, 2.0, 0.0, 3.0, 0.0, -2.0 } );
  writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
  writer.setDatasetMetadataItem( "SICNU_SENSOR", "UNIT_TEST" );
  sicnu::geo::RasterWindow full;
  full.width = 8;
  full.height = 8;
  std::vector<double> values( 64, 100 );
  values[0] = 0; // nodata
  writer.writeWindow( 1, full, values.data() );

  // No stray staging before finalize.
  const std::string stagedDuringWrite = writer.stagedPath();
  CHECK( stagedDuringWrite != target );
  writer.finalize();
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( stagedDuringWrite ) );

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const sicnu::geo::BandInfo &band = reader.metadata().bands.at( 0 );
  CHECK( band.dtype == "UInt16" );
  CHECK( band.description == "Red" );
  CHECK( band.hasNoData );
  CHECK( band.noDataValue == Approx( 0.0 ) );
  CHECK( band.hasScale );
  CHECK( band.scale == Approx( 0.01 ) );
  CHECK( band.hasOffset );
  CHECK( band.offset == Approx( -100.0 ) );
  CHECK( band.unit == "reflectance" );
  CHECK( band.role == "Red" );
  CHECK( band.wavelengthNm == Approx( 665.0 ) );
  CHECK( band.colorInterpretation == "Red" );
  CHECK( reader.metadata().sensor == "UNIT_TEST" );

  const std::vector<double> stored = reader.readWindow( { 1 }, full );
  CHECK( stored[0] == Approx( 0.0 ) );
  CHECK( stored[1] == Approx( 100.0 ) );
  // Scale/offset are NEVER applied implicitly.
  CHECK( sicnu::geo::RasterReader::applyScaleOffset( band, stored[1] ) == Approx( 100.0 * 0.01 - 100.0 ) );
}

TEST_CASE( "cancel discards staging and leaves any existing target untouched", "[io][raster][contract]" )
{
  const std::string dir = scratchDir( "cancel" );
  const std::string target = ( fs::path( dir ) / "keepme.tif" ).string();

  // First, a good output.
  sicnu::geo::RasterWriter good = sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 4;
  full.height = 4;
  std::vector<double> values( 16, 9.0 );
  good.writeWindow( 1, full, values.data() );
  good.finalize();
  const auto originalSize = sicnu::geo::atomic_fs::fileSize( target );

  // A second, cancelled attempt must not touch the target.
  {
    sicnu::geo::RasterWriter doomed = sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, sicnu::geo::RasterWriteOptions{ "GTiff", {}, true } );
    std::vector<double> junk( 16, 0.0 );
    doomed.writeWindow( 1, full, junk.data() );
    doomed.cancel();
  }
  CHECK( sicnu::geo::atomic_fs::fileSize( target ) == originalSize );

  // A destroyed (never finalized) writer also cleans up after itself.
  {
    sicnu::geo::RasterWriter abandoned = sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, sicnu::geo::RasterWriteOptions{ "GTiff", {}, true } );
    abandoned.writeWindow( 1, full, values.data() );
    const std::string staged = abandoned.stagedPath();
    CHECK( sicnu::geo::atomic_fs::fileExists( staged ) );
  }
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( ( fs::path( dir ) / "keepme.tif.0.00000.tmp" ).string() ) );
  CHECK( sicnu::geo::atomic_fs::fileSize( target ) == originalSize );
}

TEST_CASE( "create refuses existing targets unless overwrite is declared", "[io][raster][contract]" )
{
  const std::string target = ( fs::path( scratchDir( "overwrite" ) ) / "exists.tif" ).string();
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, {} );
    writer.finalize();
  }
  CHECK_THROWS_AS( sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, {} ), sicnu::geo::GeoError );
  // overwrite=true is the explicit opt-in.
  sicnu::geo::RasterWriter overwriter = sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, sicnu::geo::RasterWriteOptions{ "GTiff", {}, true } );
  overwriter.cancel();
}

TEST_CASE( "create leaves no staging behind when creation fails", "[io][raster][contract]" )
{
  const std::string dir = scratchDir( "badcreate" );
  // Degenerate size throws before staging.
  CHECK_THROWS_AS( sicnu::geo::RasterWriter::create( ( fs::path( dir ) / "zero.tif" ).string(), 0, 4, { {} }, {} ),
                   sicnu::geo::GeoError );
  // Unknown dtype throws before staging.
  sicnu::geo::RasterBandSpec badType;
  badType.dtype = "NotARealType";
  CHECK_THROWS_AS( sicnu::geo::RasterWriter::create( ( fs::path( dir ) / "bad.tif" ).string(), 4, 4, { badType }, {} ),
                   sicnu::geo::GeoError );
  // No tmp files leaked.
  int strays = 0;
  for ( const fs::directory_entry &entry : fs::directory_iterator( dir ) )
  {
    (void)entry;
    ++strays;
  }
  CHECK( strays == 0 );
}

TEST_CASE( "readBlock pads edge blocks to the uniform block geometry",
           "[io][raster][contract]" )
{
  // #790: edge blocks used to return the truncated stored window while the
  // header contract promised blockSize() elements — callers indexing by the
  // block geometry read out of bounds. Padding uses the band's declared
  // NoData (0.0 when none).
  const std::string dir = scratchDir( "blocks" );
  const std::string target = ( fs::path( dir ) / "blocks.tif" ).string();

  sicnu::geo::RasterWriteOptions options;
  options.creationOptions = { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" };
  sicnu::geo::RasterBandSpec spec;
  spec.hasNoData = true;
  spec.noDataValue = -7.5;
  sicnu::geo::RasterWriter writer =
    sicnu::geo::RasterWriter::create( target, 37, 23, { spec }, options );
  std::vector<double> values( 37 * 23 );
  for ( int y = 0; y < 23; ++y )
    for ( int x = 0; x < 37; ++x )
      values[y * 37 + x] = static_cast<double>( y * 37 + x );
  sicnu::geo::RasterWindow full;
  full.width = 37;
  full.height = 23;
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const auto blockSize = reader.blockSize( 1 );
  REQUIRE( blockSize == std::make_pair( 16, 16 ) );

  // Interior block: full geometry, stored values.
  const std::vector<double> interior = reader.readBlock( 1, 1, 0 );
  REQUIRE( interior.size() == 16 * 16 );
  CHECK( interior[0] == Approx( values[16] ) );              // block origin (16, 0)
  CHECK( interior[16 + 15] == Approx( values[1 * 37 + 31] ) ); // block row 1, col 15 → global (31, 1)

  // Corner edge block (5×7 stored): full 16×16 with declared NoData pad.
  const std::vector<double> corner = reader.readBlock( 1, 2, 1 );
  REQUIRE( corner.size() == 16 * 16 );
  const int winW = 37 - 32;
  const int winH = 23 - 16;
  CHECK( winW == 5 );
  CHECK( winH == 7 );
  for ( int y = 0; y < 16; ++y )
  {
    for ( int x = 0; x < 16; ++x )
    {
      const double cell = corner[y * 16 + x];
      if ( x < winW && y < winH )
        CHECK( cell == Approx( values[( 16 + y ) * 37 + ( 32 + x )] ) );
      else
        CHECK( cell == Approx( -7.5 ) ); // declared NoData padding
    }
  }

  // Out-of-range block coordinates stay a typed error.
  REQUIRE_THROWS( reader.readBlock( 1, 3, 0 ) );
  REQUIRE_THROWS( reader.readBlock( 1, -1, 0 ) );
}

TEST_CASE( "readBlock and iterateTiles walk the stored grid exactly",
           "[io][raster][contract]" )
{
  // #816: the block/tile streaming entry points had no test coverage at all.
  const std::string dir = scratchDir( "iterate" );
  const std::string target = ( fs::path( dir ) / "grid.tif" ).string();

  sicnu::geo::RasterWriteOptions options;
  options.creationOptions = { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" };
  sicnu::geo::RasterWriter writer =
    sicnu::geo::RasterWriter::create( target, 37, 23, { sicnu::geo::RasterBandSpec{} }, options );
  std::vector<double> values( 37 * 23 );
  for ( int y = 0; y < 23; ++y )
    for ( int x = 0; x < 37; ++x )
      values[y * 37 + x] = static_cast<double>( y * 37 + x );
  sicnu::geo::RasterWindow full;
  full.width = 37;
  full.height = 23;
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );

  // Walking every block and keeping only the stored (unpadded) region
  // reconstructs the raster exactly.
  const auto size = reader.blockSize( 1 );
  std::vector<double> rebuilt( 37 * 23, std::numeric_limits<double>::quiet_NaN() );
  const int blocksX = ( 37 + size.first - 1 ) / size.first;
  const int blocksY = ( 23 + size.second - 1 ) / size.second;
  for ( int by = 0; by < blocksY; ++by )
  {
    for ( int bx = 0; bx < blocksX; ++bx )
    {
      const std::vector<double> block = reader.readBlock( 1, bx, by );
      REQUIRE( block.size() == static_cast<size_t>( size.first ) * size.second );
      const int winW = std::min( size.first, 37 - bx * size.first );
      const int winH = std::min( size.second, 23 - by * size.second );
      for ( int y = 0; y < winH; ++y )
        for ( int x = 0; x < winW; ++x )
          rebuilt[( by * size.second + y ) * 37 + ( bx * size.first + x )] =
            block[y * size.first + x];
    }
  }
  for ( size_t i = 0; i < values.size(); ++i )
    CHECK( rebuilt[i] == Approx( values[i] ) );

  // iterateTiles hands every tile's stored window to the sink, in order,
  // and covers the whole raster exactly once.
  sicnu::geo::RasterWindow whole;
  whole.xOff = 0;
  whole.yOff = 0;
  whole.width = 37;
  whole.height = 23;
  const sicnu::geo::TilePlan plan =
    sicnu::geo::planTileWalk( reader.metadata(), whole, 16, 16 );
  REQUIRE( plan.tilesX * plan.tileWidth >= 37 );
  REQUIRE( plan.tilesY * plan.tileHeight >= 23 );
  std::vector<double> tiled( 37 * 23, std::numeric_limits<double>::quiet_NaN() );
  int tilesSeen = 0;
  reader.iterateTiles( plan, { 1 },
                       [&]( const sicnu::geo::TileSlice &slice,
                            const std::vector<double> &tile ) {
                         REQUIRE( tile.size() ==
                                  static_cast<size_t>( slice.width ) * slice.height );
                         for ( int y = 0; y < slice.height; ++y )
                           for ( int x = 0; x < slice.width; ++x )
                             tiled[( slice.yOff + y ) * 37 + ( slice.xOff + x )] =
                               tile[y * slice.width + x];
                         ++tilesSeen;
                       } );
  CHECK( tilesSeen == plan.tilesX * plan.tilesY );
  for ( size_t i = 0; i < values.size(); ++i )
    CHECK( tiled[i] == Approx( values[i] ) );

  // Cancellation stops the walk with the typed error.
  bool cancelled = false;
  REQUIRE_THROWS( reader.iterateTiles( plan, { 1 },
                                       []( const sicnu::geo::TileSlice &,
                                          const std::vector<double> & ) {},
                                       [&]() {
                                         return cancelled = true;
                                       } ) );
}

TEST_CASE( "window reads enforce the declared byte budget",
           "[io][raster][contract]" )
{
  // #808: an oversized window read used to allocate unbounded and surface
  // as an uncaught bad_alloc; it is now a typed GeoError before allocation.
  const std::string dir = scratchDir( "budget" );
  const std::string target = ( fs::path( dir ) / "src.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
    target, 64, 64, { sicnu::geo::RasterBandSpec{} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 64;
  full.height = 64;
  std::vector<double> values( 64 * 64, 1.0 );
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  // 64×64 doubles + a second band request = 64 KiB; a 1 KiB budget must
  // refuse the read before allocation.
  REQUIRE_THROWS( reader.readWindow( { 1 }, full, 1024 ) );
  // The explicit-budget overload serves reads within budget.
  const std::vector<double> chunk = reader.readWindow( { 1 }, full, 64 * 1024 );
  REQUIRE( chunk.size() == 64 * 64 );
  CHECK( chunk[0] == Approx( 1.0 ) );
}

TEST_CASE( "readWindow respects maxBytes budget", "[io][raster][contract][issue808]" )
{
  const std::string dir = scratchDir( "budget" );
  const std::string target = ( fs::path( dir ) / "budget.tif" ).string();

  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
    target, 10, 10, { sicnu::geo::RasterBandSpec{} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 10;
  full.height = 10;
  std::vector<double> vals( 100, 1.0 );
  writer.writeWindow( 1, full, vals.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  // Budget smaller than 100 * sizeof(double) (800 bytes) should throw GeoError
  CHECK_THROWS_AS( reader.readWindow( { 1 }, full, 100 ), sicnu::geo::GeoError );
  // Budget larger than or equal to 800 bytes succeeds
  CHECK_NOTHROW( reader.readWindow( { 1 }, full, 1000 ) );
}

TEST_CASE( "readBlock and iterateTiles streaming contracts", "[io][raster][contract][issue790][issue816]" )
{
  const std::string dir = scratchDir( "blocks" );
  const std::string target = ( fs::path( dir ) / "blocks.tif" ).string();

  // Create a 5x5 raster
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
    target, 5, 5, { sicnu::geo::RasterBandSpec{} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 5;
  full.height = 5;
  std::vector<double> vals( 25, 42.0 );
  writer.writeWindow( 1, full, vals.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const auto bSize = reader.blockSize( 1 );
  REQUIRE( bSize.first > 0 );
  REQUIRE( bSize.second > 0 );

  // readBlock returns exact uniform vector of size blockSize.first * blockSize.second
  const std::vector<double> blockData = reader.readBlock( 1, 0, 0 );
  CHECK( blockData.size() == static_cast<std::size_t>( bSize.first * bSize.second ) );
  CHECK( blockData[0] == Approx( 42.0 ) );

  // Out of bounds block coords throw
  CHECK_THROWS_AS( reader.readBlock( 1, -1, 0 ), sicnu::geo::GeoError );
  CHECK_THROWS_AS( reader.readBlock( 1, 0, 9999 ), sicnu::geo::GeoError );

  // iterateTiles contract test
  const auto plan = sicnu::geo::planTileWalk( reader.metadata(), full, 2, 2 );
  int tileCount = 0;
  reader.iterateTiles( plan, { 1 }, [&]( const sicnu::geo::TileSlice &slice, const std::vector<double> &data ) {
    ++tileCount;
    CHECK( data.size() == static_cast<std::size_t>( slice.width * slice.height ) );
  } );
  CHECK( tileCount == 9 );

  // iterateTiles cancellation test
  int cancelCount = 0;
  CHECK_THROWS_AS( reader.iterateTiles( plan, { 1 },
    [&]( const sicnu::geo::TileSlice &, const std::vector<double> & ) {
      ++cancelCount;
    },
    [&]() {
      return cancelCount >= 2;
    }
  ), sicnu::geo::GeoError );
  CHECK( cancelCount == 2 );
}

// ---------------------------------------------------------------------------
// 9.0 M0 — #874: Float32 sentinel matching in the band's storage precision.
// A declared NoData that is not float-exact (-9999.9) never equals the
// float-quantized stored pixels in double space; the mask must still mark
// those pixels invalid. The old strict-double compare failed this test.
// ---------------------------------------------------------------------------

TEST_CASE( "masks match Float32 sentinels in storage precision (issue874)",
           "[io][raster][contract][issue874]" )
{
  const std::string target = ( fs::path( scratchDir( "mask874" ) ) / "sentinel.tif" ).string();
  sicnu::geo::RasterBandSpec spec;
  spec.dtype = "Float32";
  spec.hasNoData = true;
  spec.noDataValue = -9999.9; // NOT representable in float32
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 4, 4, { spec }, {} );
  std::vector<double> values( 16, 1.5 );
  values[5] = -9999.9;  // narrowed to float on write; widens back exactly
  values[14] = -9999.9;
  sicnu::geo::RasterWindow full;
  full.width = 4;
  full.height = 4;
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const std::vector<std::uint8_t> mask = reader.readMask( full, { 1 } );
  REQUIRE( mask.size() == 16 );
  CHECK( mask[5] == 0 );  // old code: 255 (strict double equality missed it)
  CHECK( mask[14] == 0 );
  CHECK( mask[0] == 255 );
  CHECK( mask[1] == 255 );
}

TEST_CASE( "Float64 sentinel matching stays exact; NaN nodata unchanged (issue874)",
           "[io][raster][contract][issue874]" )
{
  const std::string target = ( fs::path( scratchDir( "mask874" ) ) / "f64.tif" ).string();
  sicnu::geo::RasterBandSpec spec;
  spec.dtype = "Float64";
  spec.hasNoData = true;
  spec.noDataValue = -9999.9; // exactly representable in Float64
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 2, 2, { spec }, {} );
  std::vector<double> values = { 1.0, -9999.9, 3.0, 4.0 };
  sicnu::geo::RasterWindow full;
  full.width = 2;
  full.height = 2;
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const std::vector<std::uint8_t> mask = reader.readMask( full, { 1 } );
  REQUIRE( mask.size() == 4 );
  CHECK( mask[1] == 0 );
  CHECK( mask[0] == 255 );

  // NaN NoData: masked where NaN, valid elsewhere (the noDataIsNaN branch of
  // the storage-precision comparison).
  const std::string nanTarget = ( fs::path( scratchDir( "mask874" ) ) / "f32nan.tif" ).string();
  sicnu::geo::RasterBandSpec nanSpec;
  nanSpec.dtype = "Float32";
  nanSpec.hasNoData = true;
  nanSpec.noDataIsNaN = true;
  sicnu::geo::RasterWriter nanWriter = sicnu::geo::RasterWriter::create( nanTarget, 2, 2, { nanSpec }, {} );
  std::vector<double> nanValues = { 1.0f, std::numeric_limits<double>::quiet_NaN(), 3.0, 4.0 };
  nanWriter.writeWindow( 1, full, nanValues.data() );
  nanWriter.finalize();
  sicnu::geo::RasterReader nanReader = sicnu::geo::RasterReader::open( nanTarget );
  const std::vector<std::uint8_t> nanMask = nanReader.readMask( full, { 1 } );
  CHECK( nanMask[1] == 0 );
  CHECK( nanMask[0] == 255 );
}

// ---------------------------------------------------------------------------
// 9.0 M0 — Float32 write overflow gate: double→Float32 narrowing of an
// out-of-range value silently produced ±inf. Now a typed fidelity failure.
// ---------------------------------------------------------------------------

TEST_CASE( "Float32 write overflow is a typed fidelity failure",
           "[io][raster][contract][fidelity]" )
{
  const std::string target = ( fs::path( scratchDir( "f32overflow" ) ) / "overflow.tif" ).string();
  sicnu::geo::RasterBandSpec spec;
  spec.dtype = "Float32";
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 2, 2, { spec }, {} );
  std::vector<double> values( 4, 1.0 );
  sicnu::geo::RasterWindow full;
  full.width = 2;
  full.height = 2;

  values[2] = 1e300; // far beyond FLT_MAX → GDAL would store +inf silently
  bool threw = false;
  try
  {
    writer.writeWindow( 1, full, values.data() );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    threw = true;
    CHECK( error.code() == sicnu::geo::ErrorCode::FidelityLoss );
  }
  CHECK( threw );

  // In-range Float32 values (including precision loss) remain writable.
  std::vector<double> okValues = { 0.1, -0.1, 1e30, -1e30 };
  CHECK_NOTHROW( writer.writeWindow( 1, full, okValues.data() ) );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( target );
  const std::vector<double> stored = reader.readWindow( { 1 }, full );
  CHECK( std::isfinite( stored[2] ) );
}
