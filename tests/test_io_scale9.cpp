/***************************************************************************
  tests/test_io_scale9.cpp — 9.0 M9: scale & resource-bound evidence.
    * concurrent window reads over a synthetic tiled raster stay byte-correct
    * reader open/close cycles keep the file-descriptor count bounded
    * the GDAL capability matrix answers with this build's driver truth
  Logical-scale fixtures only: a 2048² tiled GeoTIFF written once, no
  multi-GB payloads anywhere.
 ***************************************************************************/

#include "geospatial/doctor/data_doctor.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"

#include <gdal.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <dirent.h>
#endif

namespace fs = std::filesystem;

using namespace sicnu::geo;

namespace
{

std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_scale9" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

/// 2048×2048 Float32, 256² tiles: 64 tiles with distinct deterministic
/// ramps, so every concurrent window read can verify its own bytes.
std::string writeBigTiled( const std::string &path )
{
  RasterWriter writer = RasterWriter::create(
    path, 2048, 2048, { RasterBandSpec{} },
    { "GTiff", { "TILED=YES", "BLOCKXSIZE=256", "BLOCKYSIZE=256" }, true } );
  writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
  for ( int ty = 0; ty < 8; ++ty )
  {
    for ( int tx = 0; tx < 8; ++tx )
    {
      std::vector<double> tile( 256ull * 256 );
      const double seed = ty * 8 + tx;
      for ( std::size_t i = 0; i < tile.size(); ++i )
        tile[i] = seed + static_cast<double>( i % 97 ) * 0.25;
      writer.writeWindow( 1, { tx * 256, ty * 256, 256, 256 }, tile.data() );
    }
  }
  writer.finalize();
  return path;
}

#ifdef __linux__
std::size_t openFdCount()
{
  std::size_t count = 0;
  DIR *dir = opendir( "/proc/self/fd" );
  if ( dir == nullptr )
    return 0;
  while ( readdir( dir ) != nullptr )
    ++count;
  closedir( dir );
  return count;
}
#endif

} // namespace

TEST_CASE( "concurrent window reads stay byte-correct (M9 scale)",
           "[io][scale][fabric9][concurrency]" )
{
  const std::string dir = scratch( "concurrent" );
  const std::string target = writeBigTiled( dir + "/cube.tif" );

  // Expected tiles, computed from the same seed formula.
  auto tileAt = [ & ]( int tx, int ty ) {
    std::vector<double> tile( 256ull * 256 );
    const double seed = ty * 8 + tx;
    for ( std::size_t i = 0; i < tile.size(); ++i )
      tile[i] = seed + static_cast<double>( i % 97 ) * 0.25;
    return tile;
  };

  constexpr int kReaders = 4;
  std::vector<std::atomic<bool>> ok( kReaders );
  for ( auto &flag : ok )
    flag = false;
  {
    std::vector<std::thread> readers;
    for ( int t = 0; t < kReaders; ++t )
    {
      readers.emplace_back( [ &, t ] {
        try
        {
          RasterReader reader = RasterReader::open( target );
          for ( int ty = t; ty < 8; ty += kReaders )
          {
            for ( int tx = 0; tx < 8; ++tx )
            {
              const std::vector<double> got = reader.readWindow( { 1 }, { tx * 256, ty * 256, 256, 256 } );
              if ( got != tileAt( tx, ty ) )
                return;
            }
          }
          ok[t] = true;
        }
        catch ( ... )
        {
        }
      } );
    }
    for ( std::thread &reader : readers )
      reader.join();
  }
  for ( int t = 0; t < kReaders; ++t )
  {
    INFO( "reader " << t );
    CHECK( ok[t].load() );
  }
}

TEST_CASE( "reader lifecycles keep the descriptor count bounded (M9)",
           "[io][scale][fabric9][fd]" )
{
#ifdef __linux__
  const std::string dir = scratch( "fd" );
  const std::string target = writeBigTiled( dir + "/cube.tif" );

  // Warm any lazily-allocated process resources first.
  {
    RasterReader warm = RasterReader::open( target );
    ( void )warm;
  }
  const std::size_t before = openFdCount();
  REQUIRE( before > 0 );

  for ( int cycle = 0; cycle < 200; ++cycle )
  {
    RasterReader reader = RasterReader::open( target );
    const std::vector<double> probe = reader.readWindow( { 1 }, { 0, 0, 8, 8 } );
    CHECK( probe.size() == 64 );
  }
  const std::size_t after = openFdCount();
  // Destructors close what constructors opened: no drift beyond noise.
  CHECK( after <= before + 4 );
#else
  WARN( "descriptor accounting is POSIX-only — skipped" );
#endif
}

TEST_CASE( "the GDAL capability matrix reports this build's driver truth",
           "[io][scale][fabric9][doctor]" )
{
  const Json::Value matrix = gdalCapabilityMatrix();
  CHECK( matrix["gdal_version"].isString() );
  CHECK( !matrix["gdal_version"].asString().empty() );
  REQUIRE( matrix["formats"].isArray() );
  CHECK( matrix["formats"].size() > 0 );

  bool sawGeoTiff = false;
  for ( const Json::Value &entry : matrix["formats"] )
  {
    if ( entry["id"].asString() == "GeoTIFF" )
    {
      sawGeoTiff = true;
      CHECK( entry["driver_available"].asBool() );
      CHECK( entry["can_create"].asBool() );
      CHECK( entry["can_open"].asBool() );
    }
  }
  CHECK( sawGeoTiff );
}
