/***************************************************************************
  tests/test_io_fabric_cube.cpp — fabric 10.0: virtual mosaic / time cube and
  chunk plans. Deterministic overlap, mask-aware filling, grid negotiation,
  provenance, bounded chunk enumeration over synthetic scene assets.
 ***************************************************************************/

#include "geospatial/fabric/chunk_plan.h"
#include "geospatial/fabric/virtual_cube.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace sicnu::geo;

namespace
{

std::string scratchDir( const char *name )
{
  std::string path = ( std::filesystem::temp_directory_path() / "sicnu_fabric_tests" /
                       ( std::string( "cube_" ) + name ) )
                       .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

std::array<double, 6> geotransformFor( double originX, double originY, double scale,
                                       bool southUp = false )
{
  return { originX, scale, 0.0, southUp ? originY : originY + static_cast<double>( 32 ) * scale,
           0.0, southUp ? scale : -scale };
}

struct SceneSpec
{
  std::string id;
  double originX, originY;
  double scale = 1.0;
  std::function<double( int, int )> value;
  bool declareNoData = false;   // band NoData = 0 (values must avoid 0 then)
};

std::string writeScene( const std::string &dir, const SceneSpec &spec, int size = 32,
                        bool southUp = false )
{
  const std::string path = dir + "/" + spec.id + ".tif";
  RasterBandSpec band;
  band.noDataValue = spec.declareNoData ? 0.0 : RasterBandSpec {}.noDataValue;
  band.hasNoData = spec.declareNoData;   // (kept explicit for the reader below)
  RasterWriter writer = RasterWriter::create(
    path, size, size, { band },
    { "GTiff", { "TILED=YES", "BLOCKXSIZE=32", "BLOCKYSIZE=32" }, true } );
  writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
  writer.setGeotransform( geotransformFor( spec.originX, spec.originY, spec.scale, southUp ) );
  std::vector<double> raster( static_cast<std::size_t>( size ) * size );
  for ( int y = 0; y < size; ++y )
    for ( int x = 0; x < size; ++x )
      raster[static_cast<std::size_t>( y ) * size + x] = spec.value( x, y );
  writer.writeWindow( 1, { 0, 0, size, size }, raster.data() );
  writer.finalize();
  return path;
}

AssetRecord recordFor( const std::string &id, const std::string &path, double minX, double minY,
                       double maxX, double maxY, const std::string &instantUtc = "",
                       double cloudCover = 0.0, bool hasCloudCover = true )
{
  AssetRecord record;
  record.id = id;
  record.path = path;
  record.datetimeUtc = instantUtc;
  record.hasCloudCover = hasCloudCover;
  record.cloudCover = cloudCover;
  record.hasBbox = true;
  record.minX = minX;
  record.minY = minY;
  record.maxX = maxX;
  record.maxY = maxY;
  return record;
}

} // namespace

TEST_CASE( "a virtual cube answers windows from scene assets with exact values and provenance",
           "[io][fabric][cube]" )
{
  const std::string dir = scratchDir( "basic" );
  // Two scenes: left half (x 0..31) and right half (x 32..63) of a
  // 64-wide × 32-tall grid. Values follow the SCENE-LOCAL pattern
  // (y*100 + localX); the mapping back to world cells is what we verify.
  const std::string left = writeScene( dir, { "left", 0.0, 0.0, 1.0,
                                               [] ( int x, int y ) {
                                                 return double( y * 100 + x );
                                               } } );
  const std::string right = writeScene( dir, { "right", 32.0, 0.0, 1.0,
                                                [] ( int x, int y ) {
                                                  return double( y * 100 + x );
                                                } } );

  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 64.0;
  grid.maxY = 32.0;

  std::vector<AssetRecord> assets;
  assets.push_back( recordFor( "left", left, 0.0, 0.0, 32.0, 32.0 ) );
  assets.push_back( recordFor( "right", right, 32.0, 0.0, 64.0, 32.0 ) );
  const VirtualCube cube = VirtualCube::build( assets, grid, OverlapPolicy::FirstWins, {} );
  REQUIRE( cube.assetCount() == 2 );
  CHECK( cube.grid().width() == 64 );
  CHECK( cube.grid().height() == 32 );
  CHECK( cube.grid().crs.authid == "EPSG:4326" );   // declared on the scenes

  // Full-window read: cell (x,y) = y*100 + (x mod 32) — the covering
  // scene's local pattern.
  const VirtualCubeWindowResult full = cube.readWindow( 0, 0, 64, 32 );
  REQUIRE( full.values.size() == 64ull * 32 );
  std::size_t mismatches = 0;
  for ( int y = 0; y < 32; ++y )
    for ( int x = 0; x < 64; ++x )
    {
      const int localX = x % 32;
      if ( full.values[static_cast<std::size_t>( y ) * 64 + x] != double( y * 100 + localX ) )
        ++mismatches;
    }
  CHECK( mismatches == 0 );

  // Provenance: both scenes consulted, both contributed, windows mapped
  // (each scene reads its OWN pixel space: {0,0,32,32}).
  REQUIRE( full.provenance.size() == 2 );
  CHECK( full.provenance[0].assetId == "left" );
  CHECK( full.provenance[0].contributed );
  CHECK( full.provenance[1].assetId == "right" );
  CHECK( full.provenance[1].contributed );
  CHECK( full.provenance[0].sourceWindow == ( RasterWindow { 0, 0, 32, 32 } ) );
  CHECK( full.provenance[1].sourceWindow == ( RasterWindow { 0, 0, 32, 32 } ) );

  // Sub-window: the left scene's declared bbox never intersects, so it
  // earns NO provenance (never opened, never read).
  const VirtualCubeWindowResult corner = cube.readWindow( 40, 10, 8, 8 );
  REQUIRE( corner.provenance.size() == 1 );
  CHECK( corner.provenance[0].assetId == "right" );
  CHECK( corner.provenance[0].contributed == true );
  std::size_t holePixels = 0;
  for ( const double value : corner.values )
    if ( value == corner.gridNoData )
      ++holePixels;
  CHECK( holePixels == 0 );   // fully covered by the right scene
  CHECK( corner.values[0] == double( 10 * 100 + 8 ) );   // world x 40 → local x 8
}

TEST_CASE( "overlap resolves deterministically by the selection order (FirstWins)",
           "[io][fabric][cube][overlap]" )
{
  const std::string dir = scratchDir( "overlap" );
  // Both scenes cover the SAME 32×32 area; values differ per scene.
  const std::string clean = writeScene( dir, { "clean", 0.0, 0.0, 1.0,
                                               [] ( int, int ) { return 1.0; } } );
  const std::string cloudy = writeScene( dir, { "cloudy", 0.0, 0.0, 1.0,
                                                [] ( int, int ) { return 9.0; } } );

  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 32.0;
  grid.maxY = 32.0;

  // Quality policy: fewer clouds first — the clean scene must win EVERY cell.
  std::vector<AssetRecord> assets;
  assets.push_back( recordFor( "cloudy", cloudy, 0.0, 0.0, 32.0, 32.0,
                               "2024-01-01T00:00:00Z", 80.0 ) );
  assets.push_back( recordFor( "clean", clean, 0.0, 0.0, 32.0, 32.0,
                               "2024-01-02T00:00:00Z", 5.0 ) );
  const VirtualCube cube = VirtualCube::build( assets, grid, OverlapPolicy::FirstWins, {} );
  const VirtualCubeWindowResult window = cube.readWindow( 0, 0, 32, 32 );
  std::size_t cleanCells = 0, cloudyCells = 0;
  for ( const double value : window.values )
  {
    if ( value == 1.0 )
      ++cleanCells;
    if ( value == 9.0 )
      ++cloudyCells;
  }
  CHECK( cleanCells == 32ull * 32 );
  CHECK( cloudyCells == 0 );
  // Provenance: first (selected) scene contributed, second filled nothing.
  REQUIRE( window.provenance.size() == 2 );
  CHECK( window.provenance[0].assetId == "clean" );
  CHECK( window.provenance[0].contributed );
  CHECK( window.provenance[1].assetId == "cloudy" );
  CHECK( window.provenance[1].contributed == false );
}

TEST_CASE( "grid negotiation derives the highest-resolution grid from a bounded probe",
           "[io][fabric][cube][grid]" )
{
  const std::string dir = scratchDir( "negotiate" );
  // Coarse scene (2 m cells) + fine scene (1 m cells), overlapping coverage.
  const std::string coarse = writeScene( dir, { "coarse", 0.0, 0.0, 2.0,
                                                [] ( int, int ) { return 1.0; } } );
  const std::string fine = writeScene( dir, { "fine", 0.0, 32.0, 1.0,
                                              [] ( int, int ) { return 2.0; } } );

  std::vector<AssetRecord> assets;
  assets.push_back( recordFor( "coarse", coarse, 0.0, 0.0, 64.0, 64.0,
                               "2024-01-01T00:00:00Z" ) );
  assets.push_back( recordFor( "fine", fine, 0.0, 0.0, 32.0, 32.0,
                               "2024-01-02T00:00:00Z" ) );
  const VirtualCube cube = VirtualCube::build( assets, {}, OverlapPolicy::FirstWins, {} );
  CHECK( cube.grid().scaleX == 1.0 );   // the fine scene's resolution wins
  CHECK( cube.grid().scaleY == 1.0 );
  CHECK( cube.grid().crs.authid == "EPSG:4326" );
  // Coverage = union of probed extents.
  CHECK( cube.grid().minX == 0.0 );
  CHECK( cube.grid().maxY == 64.0 );
  // The probe facts are visible in the bounded description.
  const Json::Value described = cube.describeJson();
  CHECK( described["probed"].asUInt64() == 2 );
  CHECK( described["readable"].asUInt64() == 2 );
}

TEST_CASE( "typed refusals: empty sets, invalid grids, undervivable grids, escaping windows",
           "[io][fabric][cube][typed]" )
{
  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 32.0;
  grid.maxY = 32.0;

  REQUIRE_THROWS_AS( VirtualCube::build( {}, grid, OverlapPolicy::FirstWins, {} ), GeoError );

  VirtualCubeGrid invalid = grid;
  invalid.scaleX = -1.0;   // normalized at build, not rejected — use a real invalid shape
  invalid.maxX = 0.0;
  invalid.minX = 32.0;
  REQUIRE_THROWS_AS( VirtualCube::build(
                       { recordFor( "a", "/tmp/whatever.tif", 0.0, 0.0, 32.0, 32.0 ) }, invalid,
                       OverlapPolicy::FirstWins, {} ),
                     GeoError );

  // An asset whose raster carries no geotransform cannot seed a grid.
  const std::string dir = scratchDir( "nogrid" );
  const std::string plain = dir + "/plain.tif";
  {
    RasterWriter writer =
      RasterWriter::create( plain, 8, 8, { RasterBandSpec {} }, { "GTiff", {}, true } );
    std::vector<double> zeros( 64, 0.0 );
    writer.writeWindow( 1, { 0, 0, 8, 8 }, zeros.data() );
    writer.finalize();
  }
  REQUIRE_THROWS_AS( VirtualCube::build( { recordFor( "plain", plain, 0.0, 0.0, 32.0, 32.0 ) }, {},
                                         OverlapPolicy::FirstWins, {} ),
                     GeoError );

  const std::string scene = writeScene( dir, { "s", 0.0, 0.0, 1.0, [] ( int, int ) { return 0.0; } } );
  const VirtualCube cube = VirtualCube::build(
    { recordFor( "s", scene, 0.0, 0.0, 32.0, 32.0 ) }, grid, OverlapPolicy::FirstWins, {} );
  REQUIRE_THROWS_AS( cube.readWindow( 30, 30, 8, 8 ), GeoError );   // escapes the grid
  REQUIRE_THROWS_AS( cube.readWindow( 0, 0, 0, 8 ), GeoError );     // degenerate
}

TEST_CASE( "declared NoData loses the FirstWins contest — later scenes fill the holes",
           "[io][fabric][cube][mask]" )
{
  const std::string dir = scratchDir( "mask" );
  // The primary scene declares NoData 0 and holds a hole rectangle; the
  // secondary scene (worse clouds, later instant) covers the hole with 7s.
  {
    RasterWriter writer = RasterWriter::create(
      dir + "/declared.tif", 32, 32,
      { [] {
        RasterBandSpec band;
        band.noDataValue = 0.0;
        band.hasNoData = true;
        return band;
      }() },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=32", "BLOCKYSIZE=32" }, true } );
    writer.setGeotransform( geotransformFor( 0.0, 0.0, 1.0 ) );
    std::vector<double> raster( 32ull * 32, 1.0 );
    for ( int y = 8; y < 24; ++y )
      for ( int x = 8; x < 24; ++x )
        raster[static_cast<std::size_t>( y ) * 32 + x] = 0.0;
    writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
    writer.finalize();
  }
  const std::string secondary = writeScene( dir, { "secondary", 0.0, 0.0, 1.0,
                                                   [] ( int, int ) { return 7.0; } } );

  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 32.0;
  grid.maxY = 32.0;

  std::vector<AssetRecord> assets;
  assets.push_back( recordFor( "declared", dir + "/declared.tif", 0.0, 0.0, 32.0, 32.0,
                               "2024-01-01T00:00:00Z", 1.0 ) );
  assets.push_back( recordFor( "secondary", secondary, 0.0, 0.0, 32.0, 32.0,
                               "2024-01-02T00:00:00Z", 90.0 ) );
  const VirtualCube cube = VirtualCube::build( assets, grid, OverlapPolicy::FirstWins, {} );
  const VirtualCubeWindowResult window = cube.readWindow( 0, 0, 32, 32 );
  REQUIRE( window.values.size() == 32ull * 32 );
  std::size_t filledBySecondary = 0;
  for ( int y = 8; y < 24; ++y )
    for ( int x = 8; x < 24; ++x )
      if ( window.values[static_cast<std::size_t>( y ) * 32 + x] == 7.0 )
        ++filledBySecondary;
  CHECK( filledBySecondary == 16ull * 16 );   // every hole got the secondary's 7
  std::size_t stillPrimary = 0;
  for ( const double value : window.values )
    if ( value == 1.0 )
      ++stillPrimary;
  CHECK( stillPrimary == 32ull * 32 - 16ull * 16 );
  CHECK( window.provenance[1].contributed );   // the secondary DID contribute
}

TEST_CASE( "NaN-declared NoData loses the FirstWins contest — NaN == NaN never wins",
           "[io][fabric][cube][mask]" )
{
  const std::string dir = scratchDir( "mask_nan" );
  // Same doctrine as the declared-NoData case above, but the sentinel is NaN
  // (Float32): `NaN == NaN` is false, so a raw comparison in the scatter lets
  // NaN pixels WIN the contest and block the later scene. The reader's
  // sentinel authority (bandSentinelMatches) is NaN-exact; the cube must use
  // it or the hole silently bakes NaN in as "data".
  {
    RasterWriter writer = RasterWriter::create(
      dir + "/declared_nan.tif", 32, 32,
      { [] {
        RasterBandSpec band;
        band.dtype = "Float32";
        band.hasNoData = true;
        band.noDataIsNaN = true;
        return band;
      }() },
      { "GTiff", { "TILED=YES", "BLOCKXSIZE=32", "BLOCKYSIZE=32" }, true } );
    writer.setGeotransform( geotransformFor( 0.0, 0.0, 1.0 ) );
    std::vector<double> raster( 32ull * 32, 1.0 );
    for ( int y = 8; y < 24; ++y )
      for ( int x = 8; x < 24; ++x )
        raster[static_cast<std::size_t>( y ) * 32 + x] = std::numeric_limits<double>::quiet_NaN();
    writer.writeWindow( 1, { 0, 0, 32, 32 }, raster.data() );
    writer.finalize();
  }
  const std::string secondary = writeScene( dir, { "secondary", 0.0, 0.0, 1.0,
                                                   [] ( int, int ) { return 7.0; } } );

  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 32.0;
  grid.maxY = 32.0;

  std::vector<AssetRecord> assets;
  assets.push_back( recordFor( "declared_nan", dir + "/declared_nan.tif", 0.0, 0.0, 32.0, 32.0,
                               "2024-01-01T00:00:00Z", 1.0 ) );
  assets.push_back( recordFor( "secondary", secondary, 0.0, 0.0, 32.0, 32.0,
                               "2024-01-02T00:00:00Z", 90.0 ) );
  const VirtualCube cube = VirtualCube::build( assets, grid, OverlapPolicy::FirstWins, {} );
  const VirtualCubeWindowResult window = cube.readWindow( 0, 0, 32, 32 );
  REQUIRE( window.values.size() == 32ull * 32 );
  std::size_t nanPixels = 0;
  for ( const double value : window.values )
    if ( std::isnan( value ) )
      ++nanPixels;
  CHECK( nanPixels == 0 );   // no declared-NaN pixel may survive as data
  std::size_t filledBySecondary = 0;
  for ( int y = 8; y < 24; ++y )
    for ( int x = 8; x < 24; ++x )
      if ( window.values[static_cast<std::size_t>( y ) * 32 + x] == 7.0 )
        ++filledBySecondary;
  CHECK( filledBySecondary == 16ull * 16 );   // every hole got the secondary's 7
  std::size_t stillPrimary = 0;
  for ( const double value : window.values )
    if ( value == 1.0 )
      ++stillPrimary;
  CHECK( stillPrimary == 32ull * 32 - 16ull * 16 );
  CHECK( window.provenance[1].contributed );   // the secondary DID contribute
}

TEST_CASE( "chunk plans count millions without materializing them",
           "[io][fabric][chunk][plan]" )
{
  const std::string dir = scratchDir( "chunks" );
  // A 512×512 grid chunked 16×16 = 32×32 tiles; 4096 time steps (logical,
  // via repeated records the plan only needs instants — the cube here is
  // built from 4 real assets; the time COUNT for million-scale comes from
  // the multiplication below, not from asset materialization).
  const std::string scene = writeScene( dir, { "s", 0.0, 0.0, 1.0,
                                               [] ( int x, int y ) { return double( x + y ); } } );

  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 512.0;
  grid.maxY = 512.0;

  std::vector<AssetRecord> assets;
  for ( int t = 0; t < 4; ++t )
    assets.push_back( recordFor( "t" + std::to_string( t ), scene, 0.0, 0.0, 512.0, 512.0,
                                 "2024-01-0" + std::to_string( t + 1 ) + "T00:00:00Z" ) );
  const VirtualCube cube = VirtualCube::build( assets, grid, OverlapPolicy::FirstWins, {} );

  CubeChunkShape shape;
  shape.time = 1;
  shape.y = 16;
  shape.x = 16;
  shape.band = 1;
  const CubeChunkPlan plan = CubeChunkPlan::forVirtualCube( cube, shape );
  CHECK( plan.dims().size() == 4 );
  CHECK( plan.dims()[0].name == "time" );
  CHECK( plan.dims()[1].name == "y" );
  CHECK( plan.dims()[2].name == "x" );
  CHECK( plan.dims()[3].name == "band" );
  CHECK( plan.chunkCountTotal() == 4ull * 32ull * 32ull * 1ull );

  // Bounded enumeration: window at the front, exact tail, typed overflow.
  const std::vector<CubeChunkRequest> front = plan.materializeChunks( 0, 10 );
  REQUIRE( front.size() == 10 );
  CHECK( front[0].index == 0 );
  // Quality policy default: newest first — t3 owns chunk 0.
  CHECK( front[0].timeUtc == "2024-01-04T00:00:00Z" );
  CHECK( front[0].assetIdHint == "t3" );
  CHECK( front[0].hasExtent );
  CHECK( front[0].minX == 0.0 );
  CHECK( front[0].estimatedBytes == 16ull * 16ull * 4ull );   // Float32 dtype fact

  const std::vector<CubeChunkRequest> tail =
    plan.materializeChunks( plan.chunkCountTotal() - 3, 100 );
  REQUIRE( tail.size() == 3 );
  CHECK( tail.back().index == plan.chunkCountTotal() - 1 );

  REQUIRE_THROWS_AS( plan.materializeChunks( plan.chunkCountTotal(), 1 ), GeoError );

  // Determinism: same index, same chunk — twice.
  const std::vector<CubeChunkRequest> again = plan.materializeChunks( 40, 5 );
  const std::vector<CubeChunkRequest> repeat = plan.materializeChunks( 40, 5 );
  REQUIRE( again.size() == repeat.size() );
  for ( std::size_t i = 0; i < again.size(); ++i )
    CHECK( again[i].toJson() == repeat[i].toJson() );

  // Time slicing narrows the count BEFORE enumeration.
  CubeSlice sliced;
  sliced.timeStartUtc = "2024-01-02T00:00:00Z";
  sliced.timeEndUtc = "2024-01-04T00:00:00Z";   // exclusive: steps 2 and 3 stay
  const CubeChunkPlan slicedPlan = CubeChunkPlan::forVirtualCube( cube, shape, sliced );
  CHECK( slicedPlan.chunkCountTotal() == 2ull * 32ull * 32ull );
  CHECK( slicedPlan.timeSliced() );
  const std::vector<CubeChunkRequest> slicedChunks = slicedPlan.materializeChunks( 0, 1 );
  REQUIRE( slicedChunks.size() == 1 );
  // Within the kept range the newest step comes first.
  CHECK( slicedChunks[0].timeUtc == "2024-01-03T00:00:00Z" );

  // Spatial slicing narrows y/x.
  CubeSlice region;
  region.hasSpatialSlice = true;
  region.minX = 0.0;
  region.minY = 0.0;
  region.maxX = 64.0;   // quarter of the width
  region.maxY = 64.0;
  const CubeChunkPlan regionPlan = CubeChunkPlan::forVirtualCube( cube, shape, region );
  CHECK( regionPlan.chunkCountTotal() == 4ull * 4ull * 4ull );

  // Multidim descriptors plan by their own dims; slicing is a typed refusal.
  MultidimCubeDescriptor descriptor;
  descriptor.variable = "lst";
  descriptor.dtype = "Float32";
  descriptor.dimensionNames = { "time", "y", "x" };
  MultidimCubeAxis timeAxis;
  timeAxis.name = "time";
  timeAxis.type = "TEMPORAL";
  timeAxis.size = 512;
  MultidimCubeAxis yAxis;
  yAxis.name = "y";
  yAxis.size = 1024;
  MultidimCubeAxis xAxis;
  xAxis.name = "x";
  xAxis.size = 1024;
  descriptor.axes = { timeAxis, yAxis, xAxis };
  const CubeChunkPlan multidimPlan = CubeChunkPlan::forMultidimDescriptor( descriptor, shape );
  CHECK( multidimPlan.chunkCountTotal() == 512ull * 64ull * 64ull );
  CubeSlice multidimSlice;
  multidimSlice.hasSpatialSlice = true;
  REQUIRE_THROWS_AS( CubeChunkPlan::forMultidimDescriptor( descriptor, shape, multidimSlice ),
                     GeoError );
}
