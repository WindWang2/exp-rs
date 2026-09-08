/***************************************************************************
  tests/benchmark_io.cpp — Geospatial I/O Foundation 4.0 benchmark harness.
  Measures: open latency · metadata inspect · window/multiband read ·
  sequential scan · write · reproject · COG create · 100k vector scan ·
  simulated remote range access (/vsimem baseline). Emits JSON results.
 ***************************************************************************/

#include "geospatial/convert/raster_convert.h"
#include "geospatial/cog/cog_validator.h"
#include "geospatial/doctor/data_doctor.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/multidim/multidim_view.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"

#include <gdal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace
{

double millisecondsSince( Clock::time_point start )
{
  return std::chrono::duration<double, std::milli>( Clock::now() - start ).count();
}

std::string scratchDir()
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_benchmark";
  fs::create_directories( dir );
  return dir.string();
}

std::string makeBenchmarkRaster( const std::string &dir, int size = 2048 )
{
  const std::string path = ( fs::path( dir ) / "bench.tif" ).string();
  sicnu::geo::RasterBandSpec b1;
  b1.dtype = "Float32";
  b1.hasNoData = true;
  b1.noDataValue = -9999.0;
  b1.role = "Red";
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
    path, size, size, { b1, b1 }, sicnu::geo::RasterWriteOptions{ "GTiff", { "COMPRESS=DEFLATE", "TILED=YES", "BLOCKXSIZE=512", "BLOCKYSIZE=512" }, true } );
  writer.setGeotransform( { 116.0, 0.001, 0.0, 31.0, 0.0, -0.001 } );
  writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
  std::vector<double> row( size, 1.0 );
  for ( int y = 0; y < size; ++y )
  {
    row[0] = static_cast<double>( y );
    sicnu::geo::RasterWindow line;
    line.yOff = y;
    line.width = size;
    line.height = 1;
    writer.writeWindow( 1, line, row.data() );
    writer.writeWindow( 2, line, row.data() );
  }
  writer.finalize();
  return path;
}

Json::Value record( const char *name, double milliseconds, const Json::Value &extra = Json::Value() )
{
  Json::Value entry;
  entry["benchmark"] = name;
  entry["milliseconds"] = milliseconds;
  if ( !extra.isNull() )
    entry["extra"] = extra;
  return entry;
}

} // namespace

int main( int argc, char **argv )
{
  std::string outPath = ( fs::path( scratchDir().substr( 0, 0 ) ) / "benchmarks" / "io-foundation-4.json" ).string();
  for ( int i = 1; i < argc; ++i )
  {
    const std::string arg = argv[i];
    if ( arg == "--out" && i + 1 < argc )
      outPath = argv[++i];
  }

  sicnu::geo::ensureGdalRegistered();
  Json::Value results;
  results["format_version"] = 1;
  results["suite"] = "geospatial-io-foundation-4";
  const std::string dir = scratchDir();
  results["scratch_dir"] = dir;

  const std::string raster = makeBenchmarkRaster( dir );

  // 1. open latency
  {
    double total = 0.0;
    const int runs = 20;
    for ( int i = 0; i < runs; ++i )
    {
      const auto start = Clock::now();
      sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( raster );
      total += millisecondsSince( start );
      reader.close();
    }
    results["results"].append( record( "open_latency_mean", total / runs, Json::Value( runs ) ) );
  }

  // 2. metadata inspect (canonical, incl. JSON serialization)
  {
    const auto start = Clock::now();
    const Json::Value inspected = sicnu::geo::runInspect( raster );
    const double elapsed = millisecondsSince( start );
    Json::Value extra;
    extra["bytes"] = static_cast<Json::UInt64>( Json::writeString( Json::StreamWriterBuilder(), inspected ).size() );
    results["results"].append( record( "metadata_inspect", elapsed, extra ) );
  }

  // 3. window read (512², 100 random windows)
  {
    sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( raster );
    const auto start = Clock::now();
    const int runs = 100;
    std::size_t checksum = 0;
    for ( int i = 0; i < runs; ++i )
    {
      sicnu::geo::RasterWindow window;
      window.xOff = ( i * 97 ) % 1536;
      window.yOff = ( i * 131 ) % 1536;
      window.width = 512;
      window.height = 512;
      checksum += reader.readWindow( { 1 }, window ).size();
    }
    const double elapsed = millisecondsSince( start );
    Json::Value extra;
    extra["windows"] = runs;
    extra["window_px"] = 512 * 512;
    extra["checksum"] = static_cast<Json::UInt64>( checksum );
    results["results"].append( record( "window_read_512_mean", elapsed / runs, extra ) );
    reader.close();
  }

  // 4. multiband read (both bands, 512²)
  {
    sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( raster );
    const auto start = Clock::now();
    sicnu::geo::RasterWindow window;
    window.width = 512;
    window.height = 512;
    const std::vector<double> values = reader.readWindow( { 1, 2 }, window );
    const double elapsed = millisecondsSince( start );
    Json::Value extra;
    extra["doubles"] = static_cast<Json::UInt64>( values.size() );
    results["results"].append( record( "multiband_read_512", elapsed, extra ) );
    reader.close();
  }

  // 5. sequential scan (full raster, 256-px line windows)
  {
    sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( raster );
    const auto start = Clock::now();
    sicnu::geo::RasterWindow line;
    line.width = 2048;
    line.height = 256;
    double sink = 0.0;
    for ( int y = 0; y < 2048; y += 256 )
    {
      line.yOff = y;
      const std::vector<double> values = reader.readWindow( { 1 }, line );
      sink += values[0];
    }
    const double elapsed = millisecondsSince( start );
    Json::Value extra;
    extra["sink"] = sink;
    results["results"].append( record( "sequential_scan_2048", elapsed, extra ) );
    reader.close();
  }

  // 6. write (1024² Float32, LZW tiled)
  {
    const std::string target = ( fs::path( dir ) / "write_out.tif" ).string();
    const auto start = Clock::now();
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
      target, 1024, 1024, { {} }, { "GTiff", { "COMPRESS=LZW", "TILED=YES" }, true } );
    std::vector<double> values( 1024 * 1024, 2.5 );
    sicnu::geo::RasterWindow full;
    full.width = 1024;
    full.height = 1024;
    writer.writeWindow( 1, full, values.data() );
    writer.finalize();
    results["results"].append( record( "write_1024_lzw", millisecondsSince( start ) ) );
  }

  // 7. reproject (512² 4326 → 3857, bilinear)
  {
    const std::string target = ( fs::path( dir ) / "reprojected.tif" ).string();
    sicnu::geo::WarpOptions options;
    options.targetCrs = "EPSG:3857";
    options.resampling = "bilinear";
    const auto start = Clock::now();
    const sicnu::geo::TranslateResult result = sicnu::geo::warpRaster( raster, target, options );
    results["results"].append( record( "reproject_2048_to_3857", millisecondsSince( start ),
                                       Json::Value( result.width ) ) );
  }

  // 8. COG create (lossless)
  {
    const std::string target = ( fs::path( dir ) / "cog_out.tif" ).string();
    const auto start = Clock::now();
    const sicnu::geo::TranslateResult result =
      sicnu::geo::makeCog( raster, target, sicnu::geo::CogPreset::LosslessScientific );
    const double elapsed = millisecondsSince( start );
    const sicnu::geo::CogValidationReport report = sicnu::geo::validateCog( target );
    Json::Value extra;
    extra["is_cog"] = report.isCog;
    extra["bands"] = result.bandCount;
    results["results"].append( record( "cog_create_2048", elapsed, extra ) );
  }

  // 9. vector scan: 100k features written, then streamed in 1000-batches
  {
    const std::string target = ( fs::path( dir ) / "bulk.gpkg" ).string();
    const auto writeStart = Clock::now();
    {
      sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
        target, "bulk", "Point", { { "code", "Integer64" }, { "label", "String", 24 } },
        sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), { "GPKG", {}, true, "" } );
      for ( int i = 0; i < 100000; ++i )
      {
        Json::Value attrs( Json::objectValue );
        attrs["code"] = static_cast<Json::Int64>( i );
        attrs["label"] = "point-" + std::to_string( i % 1000 );
        writer.writeFeature( attrs, "POINT (" + std::to_string( 100 + ( i % 300 ) * 0.01 ) + " "
                                       + std::to_string( 20 + ( i % 200 ) * 0.01 ) + ")" );
      }
      writer.finalize();
    }
    results["results"].append( record( "vector_write_100k", millisecondsSince( writeStart ) ) );

    sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
    const auto scanStart = Clock::now();
    std::vector<sicnu::geo::VectorFeature> batch;
    std::int64_t total = 0;
    while ( reader.nextBatch( batch, 1000 ) || !batch.empty() )
    {
      total += static_cast<std::int64_t>( batch.size() );
      batch.clear();
    }
    results["results"].append( record( "vector_scan_100k", millisecondsSince( scanStart ),
                                       Json::Value( static_cast<Json::Int64>( total ) ) ) );
  }

  // 10. simulated remote range access: /vsimem copy (in-memory block reads)
  {
    const std::string vsimemPath = "/vsimem/sicnu_io_bench.tif";
    {
      sicnu::geo::QuietCplErrors quiet;
      GDALDatasetH source = GDALOpenEx( raster.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER, nullptr, nullptr, nullptr );
      GDALDriverH driver = GDALGetDriverByName( "GTiff" );
      char *options[] = { const_cast<char *>( "COMPRESS=DEFLATE" ), const_cast<char *>( "TILED=YES" ), nullptr };
      GDALDatasetH copy = GDALCreateCopy( driver, vsimemPath.c_str(), source, 0, options, nullptr, nullptr );
      GDALClose( copy );
      GDALClose( source );
    }
    const auto start = Clock::now();
    sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( vsimemPath );
    sicnu::geo::RasterWindow window;
    window.width = 512;
    window.height = 512;
    std::size_t bytes = 0;
    for ( int i = 0; i < 50; ++i )
    {
      window.xOff = ( i * 89 ) % 1536;
      window.yOff = ( i * 127 ) % 1536;
      bytes += reader.readWindow( { 1 }, window ).size() * sizeof( double );
    }
    const double elapsed = millisecondsSince( start );
    reader.close();
    Json::Value extra;
    extra["bytes"] = static_cast<Json::UInt64>( bytes );
    extra["note"] = "vsimem baseline — no network latency; measures block/range read path only";
    results["results"].append( record( "simulated_remote_range_512_mean", elapsed / 50, extra ) );
    GDALDeleteDataset( GDALGetDriverByName( "GTiff" ), vsimemPath.c_str() );
    VSIUnlink( vsimemPath.c_str() );
  }

  results["generated_at"] = std::to_string( std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() ) );

  // Emit.
  const Json::StreamWriterBuilder builder;
  const std::string text = Json::writeString( builder, results );
  if ( !outPath.empty() )
  {
    fs::path out = fs::u8path( outPath );
    if ( out.is_relative() )
      out = fs::u8path( CMAKE_SOURCE_DIR ) / out;
    fs::create_directories( out.parent_path() );
    std::ofstream out_file( out, std::ios::binary | std::ios::trunc );
    out_file << text;
    std::cout << "results written to " << out.string() << std::endl;
  }
  std::cout << text << std::endl;
  return 0;
}
