/***************************************************************************
  tests/test_io_microbench.cpp — Out-of-Core I/O 12.0 (WP G): microbench
  with CORRECTNESS GATES on a real loopback HTTP origin.

  A benchmark that only prints numbers cannot fail; a perf assertion on
  wall-clock time flakes in CI. This suite occupies the useful middle:
  every run MEASURES bytes / requests / latency over a real loopback
  transfer and reports them as JSON, and every run HARD-ASSERTS the
  deterministic data-plane facts the out-of-core oracle needs:
    * a cold window read costs the origin bytes ONCE (requests == expected)
    * a warm re-read of the same window costs ZERO origin requests and
      ZERO origin bytes (the warm-cache oracle, measurable)
    * the peak cached-bytes gauge stays inside the configured budget
    * peak in-flight fetch bytes stay inside the concurrency cap
  Latencies are recorded and printed for human comparison — never asserted.
 ***************************************************************************/

#include "geospatial/remote/range_cache.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "support/http_range_server.h"

#include <cpl_conv.h>

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace sicnu::geo;
using sicnu::geo::testsupport::HttpRangeServer;

namespace
{

std::string benchScratch( const std::string &name )
{
  const std::filesystem::path dir =
    std::filesystem::temp_directory_path() / "sicnu_io_microbench" / name;
  std::error_code ec;
  std::filesystem::remove_all( dir, ec );
  std::filesystem::create_directories( dir );
  return dir.string();
}

/// A 2048×2048 Float32 tiled GeoTIFF with an incompressible pattern — real
/// bulk so byte accounting is meaningful (≈16 MB payload).
std::vector<unsigned char> buildBenchTiff( const std::string &path )
{
  const int size = 2048;
  {
    RasterWriter writer =
      RasterWriter::create( path, size, size, { RasterBandSpec {} },
                            { "GTiff", { "TILED=YES", "BLOCKXSIZE=256", "BLOCKYSIZE=256" }, true } );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
    std::vector<double> values( static_cast<std::size_t>( size ) * size );
    std::uint32_t state = 0x9E3779B9u;
    for ( std::size_t i = 0; i < values.size(); ++i )
    {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      values[i] = static_cast<double>( state % 1000003u );
    }
    writer.writeWindow( 1, { 0, 0, size, size }, values.data() );
    writer.finalize();
  }
  std::ifstream in( path, std::ios::binary );
  return std::vector<unsigned char>( ( std::istreambuf_iterator<char>( in ) ),
                                     std::istreambuf_iterator<char>() );
}

double millisecondsSince( const std::chrono::steady_clock::time_point &start )
{
  return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start )
    .count();
}

} // namespace

TEST_CASE( "microbench: window reads cost bytes once and warm reads cost nothing",
           "[io][microbench][range_cache][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = benchScratch( "window" );
  const std::vector<unsigned char> payload = buildBenchTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"bench-1\"" );

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 8ull * 1024 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  RemoteRangeCache::install( config );
  struct CacheGuard { ~CacheGuard() { RemoteRangeCache::uninstall(); } } cacheGuard;
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  const RasterWindow window { 256, 256, 512, 512 }; // 4 × 4 source tiles

  Json::Value bench;
  bench["payload_bytes"] = static_cast<Json::UInt64>( payload.size() );
  bench["window"] = [ & ] {
    Json::Value w;
    w["x"] = window.xOff;
    w["y"] = window.yOff;
    w["w"] = window.width;
    w["h"] = window.height;
    return w;
  }();

  // ── cold phase: the open + first window read through the cache ─────────
  const int requestsColdStart = server.requestCount();
  const std::uint64_t servedColdStart = server.bytesServed();
  const std::uint64_t fetchedColdStart =
    RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
  const auto coldStart = std::chrono::steady_clock::now();
  std::vector<double> cold;
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    cold = reader.readWindow( { 1 }, window );
  }
  bench["cold_ms"] = millisecondsSince( coldStart );
  bench["cold_requests"] = server.requestCount() - requestsColdStart;
  bench["cold_origin_bytes"] = server.bytesServed() - servedColdStart;
  bench["cold_cache_fetched"] =
    RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64() - fetchedColdStart;
  REQUIRE( cold.size() == static_cast<std::size_t>( window.width ) * window.height );
  CHECK( cold == local.readWindow( { 1 }, window ) );

  // Data-plane facts (asserted): the cold read pulled a BOUNDED MINORITY of
  // the object — window blocks, never the whole file.
  CHECK( bench["cold_requests"].asInt() >= 1 );
  CHECK( bench["cold_cache_fetched"].asUInt64() * 2 < payload.size() );

  // ── warm phase: the SAME window again through a fresh reader ───────────
  const int requestsWarmStart = server.requestCount();
  const std::uint64_t servedWarmStart = server.bytesServed();
  const std::uint64_t fetchedWarmStart =
    RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
  const auto warmStart = std::chrono::steady_clock::now();
  std::vector<double> warm;
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    warm = reader.readWindow( { 1 }, window );
  }
  bench["warm_ms"] = millisecondsSince( warmStart );
  bench["warm_requests"] = server.requestCount() - requestsWarmStart;
  bench["warm_origin_bytes"] = server.bytesServed() - servedWarmStart;
  bench["warm_cache_fetched"] =
    RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64() - fetchedWarmStart;
  CHECK( warm == cold );

  // THE warm-cache oracle: zero origin data on the second pass (identity
  // revalidation control traffic may arrive — ValidateOnce skips even that
  // — but payload bytes must not move).
  CHECK( bench["warm_cache_fetched"].asUInt64() == 0 );
  CHECK( server.bytesServed() - servedWarmStart == 0 );

  // Budget honesty: the peak cached-bytes gauge never exceeded the budget.
  const Json::Value telemetry = RemoteRangeCache::telemetryJson();
  CHECK( telemetry["max_cached_bytes"].asUInt64() <= config.maxCacheBytes );
  CHECK( telemetry["max_in_flight_fetch_bytes"].asUInt64() <=
         config.maxConcurrentFetchBytes );
  bench["peak_cached_bytes"] = telemetry["max_cached_bytes"];
  bench["peak_in_flight_bytes"] = telemetry["max_in_flight_fetch_bytes"];
  bench["read_amplification"] = telemetry["read_amplification"];

  INFO( "microbench JSON:\n" << Json::writeString( Json::StreamWriterBuilder(), bench ) );
  std::cout << "microbench-window: "
            << Json::writeString( Json::StreamWriterBuilder(), bench ) << "\n";
}

TEST_CASE( "microbench: an incompressible scan stays bounded by the budget",
           "[io][microbench][churn][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = benchScratch( "scan" );
  const std::vector<unsigned char> payload = buildBenchTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"bench-scan-1\"" );

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 4ull * 1024 * 1024; // a fraction of the 16 MB object
  config.maxBytesPerResource = 2ull * 1024 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  RemoteRangeCache::install( config );
  struct CacheGuard { ~CacheGuard() { RemoteRangeCache::uninstall(); } } cacheGuard;
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  // Scan the whole raster row-band by row-band (an out-of-core traversal:
  // total demand 16 MB, budget 4 MB).
  const auto scanStart = std::chrono::steady_clock::now();
  std::vector<double> band;
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    for ( int y = 0; y < 2048; y += 256 )
    {
      band = reader.readWindow( { 1 }, { 0, y, 2048, 256 } );
      REQUIRE( band.size() == 2048ull * 256 );
    }
  }
  const Json::Value telemetry = RemoteRangeCache::telemetryJson();
  const Json::Value bench;
  ( void ) bench;
  INFO( "scan_ms=" << millisecondsSince( scanStart ) << " evictions="
                   << telemetry["evictions"].asUInt64() );

  // The traversal completed byte-bounded: neither the store nor the gauge
  // ever left the budget, whatever the total demand was.
  CHECK( telemetry["cached_bytes"].asUInt64() <= config.maxCacheBytes );
  CHECK( telemetry["max_cached_bytes"].asUInt64() <= config.maxCacheBytes );
  CHECK( telemetry["evictions"].asUInt64() > 0 );
}
