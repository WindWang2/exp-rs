/***************************************************************************
  tests/test_io_range_cache.cpp — Cloud-Native Geospatial I/O 7.0 (M2):
  bounded byte-range cache over remote rasters, proven against the local
  range-capable HTTP fixture:
    * a window read twice costs origin bytes ONCE (hit accounting)
    * coalesced fetching: contiguous missing blocks merge into one GET
    * the LRU byte budget evicts without corrupting reads
    * stale validation: validator mismatch drops cached bytes (policy-driven)
    * failure fallback: a range-ignoring origin degrades to /vsicurl/
    * byte-for-byte equality with local reads
 ***************************************************************************/

#include "geospatial/remote/range_cache.h"
#include "geospatial/remote/range_cache_disk.h"
#include "geospatial/remote/http_fetch.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/convert/raster_convert.h"
#include "support/http_range_server.h"
#include "support/offline_probe.h"

#include <cpl_conv.h>
#include <cpl_vsi.h>
#include <gdal.h>
#include <json/json.h>
#include <cstring>
#include <cstdio>

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

using namespace sicnu::geo;
using sicnu::geo::testsupport::HttpRangeServer;
using sicnu::geo::testsupport::ServerBehavior;

// ADR 0146: never die by timeout when the loopback transport is missing —
// report `sicnu-skip: <reason>` + exit 77 instead.
SICNU_OFFLINE_GUARD()

namespace
{

std::string scratch( const std::string &name )
{
  const std::filesystem::path dir =
    std::filesystem::temp_directory_path() / "sicnu_io_test_range_cache" / name;
  std::error_code ec;
  std::filesystem::remove_all( dir, ec );
  std::filesystem::create_directories( dir );
  return dir.string();
}

/// A 1024×1024 Float32 raster with a distinctive pattern, written as an
/// UNCOMPRESSED tiled GeoTIFF so byte ranges map to tiles deterministically.
std::vector<unsigned char> buildTiff( const std::string &path )
{
  const int size = 1024;
  {
    RasterWriter writer = RasterWriter::create( path, size, size, { RasterBandSpec {} },
                                                { "GTiff", { "TILED=YES", "BLOCKXSIZE=128", "BLOCKYSIZE=128" }, true } );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
    // Write a ramp over the whole top-left quadrant; the rest stays zero.
    std::vector<double> values( 512 * 512 );
    for ( int y = 0; y < 512; ++y )
      for ( int x = 0; x < 512; ++x )
        values[static_cast<std::size_t>( y ) * 512 + x] = static_cast<double>( ( x + y * 3 ) % 977 );
    writer.writeWindow( 1, { 0, 0, 512, 512 }, values.data() );
    writer.finalize();
  }
  std::ifstream in( path, std::ios::binary );
  return std::vector<unsigned char>( ( std::istreambuf_iterator<char>( in ) ),
                                     std::istreambuf_iterator<char>() );
}

/// A small 256x256 Byte raster (fits inside one fetch budget): exercises the
/// full-object-answer serving path of range-ignoring origins.
std::vector<unsigned char> buildSmallTiff( const std::string &path )
{
  const int size = 256;
  {
    RasterBandSpec band;
    band.dtype = "Byte";
    RasterWriter writer = RasterWriter::create( path, size, size, { band },
                                                { "GTiff", { "TILED=YES", "BLOCKXSIZE=128", "BLOCKYSIZE=128" }, true } );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
    std::vector<double> values( static_cast<std::size_t>( size ) * size );
    for ( int y = 0; y < size; ++y )
      for ( int x = 0; x < size; ++x )
        values[static_cast<std::size_t>( y ) * size + x] = static_cast<double>( ( x + y ) % 251 );
    writer.writeWindow( 1, { 0, 0, size, size }, values.data() );
    writer.finalize();
  }
  std::ifstream in( path, std::ios::binary );
  return std::vector<unsigned char>( ( std::istreambuf_iterator<char>( in ) ),
                                     std::istreambuf_iterator<char>() );
}

struct InstalledCache
{
  explicit InstalledCache( const RangeCacheConfig &config ) { RemoteRangeCache::install( config ); }
  ~InstalledCache() { RemoteRangeCache::uninstall(); }
  InstalledCache( const InstalledCache & ) = delete;
  InstalledCache &operator=( const InstalledCache & ) = delete;
};

/// Same RAII contract for the process-wide disk store singleton: a failing
/// REQUIRE in a case that configured it must not leave the store enabled.
struct InstalledDiskStore
{
  InstalledDiskStore( const std::string &directory, std::uint64_t maxBytes )
  {
    RangeDiskBlockStore::configure( directory, maxBytes );
  }
  ~InstalledDiskStore() { RangeDiskBlockStore::configure( std::string(), 0 ); }
  InstalledDiskStore( const InstalledDiskStore & ) = delete;
  InstalledDiskStore &operator=( const InstalledDiskStore & ) = delete;
};

} // namespace

// ---------------------------------------------------------------------------
// 9.0 M2 — bounded concurrent fetch: the global in-flight byte cap is a real
// gate. This case runs FIRST in the binary so the process high-water gauge
// is this test's own peak: 4 concurrent readers whose ungated fetch runs are
// 256 KiB each would peak at ~1 MiB without the gate; the 256 KiB cap must
// hold (asserted as the ABSOLUTE gauge, no delta masking).
// ---------------------------------------------------------------------------

TEST_CASE( "the global in-flight fetch bound holds across concurrent resources",
           "[io][remote][range_cache][fabric9][admission]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "admission" );

  // Two independent origins; four readers so the UNGATED concurrent sum
  // (~4 x 256 KiB fetch runs) clearly exceeds the cap.
  const std::vector<unsigned char> payloadA = buildTiff( dir + "/a.tif" );
  const std::vector<unsigned char> payloadB = buildTiff( dir + "/b.tif" );
  HttpRangeServer serverA( payloadA );
  HttpRangeServer serverB( payloadB );
  serverA.setConcurrency( 8 );
  serverB.setConcurrency( 8 );

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 16ull * 1024 * 1024;
  // A fetch run is clamped to maxSingleFetchBytes: each in-flight request
  // is <= 256 KiB, so head-of-line admission stays inside the cap.
  config.maxSingleFetchBytes = 256ull * 1024;
  config.maxConcurrentFetchBytes = 256ull * 1024;
  InstalledCache guard( config );

  std::vector<double> expectedA;
  std::vector<double> expectedB;
  {
    RasterReader localA( RasterReader::open( dir + "/a.tif" ) );
    expectedA = localA.readWindow( { 1 }, { 0, 0, 256, 256 } );
    RasterReader localB( RasterReader::open( dir + "/b.tif" ) );
    expectedB = localB.readWindow( { 1 }, { 0, 0, 256, 256 } );
  }

  std::atomic<int> okCount{ 0 };
  {
    std::vector<std::thread> readers;
    for ( int t = 0; t < 4; ++t )
    {
      readers.emplace_back( [ &, t ] {
        try
        {
          const bool fromA = ( t % 2 ) == 0;
          RasterReader reader = RasterReader::open(
            RemoteRangeCache::cachedPath( fromA ? serverA.url() : serverB.url() ) );
          const std::vector<double> got = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
          if ( got == ( fromA ? expectedA : expectedB ) )
            okCount.fetch_add( 1 );
        }
        catch ( ... )
        {
        }
      } );
    }
    for ( std::thread &reader : readers )
      reader.join();
  }
  CHECK( okCount.load() == 4 );

  // First-position absolute gauge: this binary has run no other cache
  // traffic, so the high-water IS this test's peak and must not exceed the
  // declared bound (deleting the admission gate pushes it to ~1 MiB).
  const Json::Value telemetry = RemoteRangeCache::telemetryJson();
  const std::uint64_t peak = telemetry["max_in_flight_fetch_bytes"].asUInt64();
  INFO( "peak in-flight bytes: " << peak );
  CHECK( peak <= config.maxConcurrentFetchBytes );
  // Real amplification evidence: origin bytes were fetched for the misses.
  CHECK( telemetry["bytes_fetched"].asUInt64() > 0 );
  CHECK( telemetry["bytes_served"].asUInt64() > 0 );
  CHECK( telemetry["read_amplification"].asDouble() > 0.0 );
}

TEST_CASE( "range cache serves window reads with byte-once origin cost",
           "[io][remote][range_cache]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "hit" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"cache-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.maxCacheBytes = 1024ull * 1024;
  InstalledCache guard( config );

  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );
  REQUIRE( RemoteRangeCache::isCachePath( cachedPath ) );

  std::vector<double> first;
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    CHECK( reader.metadata().width == 1024 );
    first = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
    REQUIRE( first.size() == 256ull * 256 );
    }

  // Second open+read of the same window: zero additional origin bytes.
  Json::Value before = RemoteRangeCache::telemetryJson();
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    const std::vector<double> second = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
    CHECK( second == first );
  }
  Json::Value after = RemoteRangeCache::telemetryJson();
  CHECK( after["bytes_fetched"].asUInt64() == before["bytes_fetched"].asUInt64() );
  CHECK( after["hits"].asUInt64() > before["hits"].asUInt64() );
  CHECK( after["bytes_served"].asUInt64() > before["bytes_served"].asUInt64() );

  // The cached path must agree byte-for-byte with the local file.
  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  const std::vector<double> expected = local.readWindow( { 1 }, { 0, 0, 256, 256 } );
    CHECK( first == expected );
}

TEST_CASE( "range cache coalesces contiguous missing blocks into one fetch",
           "[io][remote][range_cache][coalescing]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "coalesce" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );

  RangeCacheConfig config;
  config.blockSize = 16 * 1024;
  config.maxCacheBytes = 8ull * 1024 * 1024;
  InstalledCache guard( config );

  RemoteRangeCache::clearEntries();
  Json::Value before = RemoteRangeCache::telemetryJson();

  RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
  REQUIRE( reader.isOpen() );
  // A 256KB window spans 16 blocks of 16KB; the runs must merge into a
  // handful of fetches, not one-per-block.
  const std::vector<double> window = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
  REQUIRE( window.size() == 256ull * 256 );

  Json::Value after = RemoteRangeCache::telemetryJson();
  const std::uint64_t fetches = after["coalesced_fetches"].asUInt64() -
                                before["coalesced_fetches"].asUInt64();
  CHECK( fetches >= 1 );
  CHECK( fetches < 16 );

  // And the merged fetch actually served correct bytes.
  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  CHECK( window == local.readWindow( { 1 }, { 0, 0, 256, 256 } ) );
}

TEST_CASE( "range cache LRU budget evicts without corrupting reads",
           "[io][remote][range_cache][budget]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "budget" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.maxCacheBytes = config.blockSize * 2; // 2 blocks only
  InstalledCache guard( config );

  RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
  REQUIRE( reader.isOpen() );
  // Far more data than the budget, interleaved far/near offsets.
  const std::vector<double> a = reader.readWindow( { 1 }, { 0, 0, 128, 128 } );
  const std::vector<double> b = reader.readWindow( { 1 }, { 512, 512, 128, 128 } );
  const std::vector<double> c = reader.readWindow( { 1 }, { 256, 0, 128, 128 } );
  CHECK( RemoteRangeCache::telemetryJson()["evictions"].asUInt64() > 0 );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  CHECK( a == local.readWindow( { 1 }, { 0, 0, 128, 128 } ) );
  CHECK( b == local.readWindow( { 1 }, { 512, 512, 128, 128 } ) );
  CHECK( c == local.readWindow( { 1 }, { 256, 0, 128, 128 } ) );
}

TEST_CASE( "stale policy RevalidateOnOpen drops cached bytes on validator mismatch",
           "[io][remote][range_cache][stale]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "stale" );
  std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"version-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.maxCacheBytes = 8ull * 1024 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::RevalidateOnOpen;
  InstalledCache guard( config );

  RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
  REQUIRE( reader.isOpen() );
  const std::vector<double> v1 = reader.readWindow( { 1 }, { 0, 0, 64, 64 } );
  reader.close();

  // The origin object is replaced: new PIXEL bytes (structure untouched),
  // new ETag.
  std::vector<unsigned char> replacement = payload;
  for ( std::size_t i = 65536; i < 69632; ++i )
    replacement[i] = static_cast<unsigned char>( replacement[i] ^ 0x5A );
  server.replacePayload( replacement, "\"version-2\"", "" );

  RasterReader reader2 = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
  REQUIRE( reader2.isOpen() );
  const std::vector<double> v2 = reader2.readWindow( { 1 }, { 0, 0, 64, 64 } );
  CHECK( RemoteRangeCache::telemetryJson()["invalidations"].asUInt64() >= 1 );

  // v2 comes from the replacement payload: verify through a fresh local copy.
  reader2.close();
  {
    std::filesystem::rename( dir + "/scene.tif", dir + "/scene_old.tif" );
    std::ofstream out( dir + "/scene.tif", std::ios::binary );
    out.write( reinterpret_cast<const char *>( replacement.data() ),
               static_cast<std::streamsize>( replacement.size() ) );
    out.close();
    RasterReader localNew = RasterReader::open( dir + "/scene.tif" );
    const std::vector<double> expected = localNew.readWindow( { 1 }, { 0, 0, 64, 64 } );
    localNew.close();
    CHECK( v2 == expected );
    std::filesystem::remove( dir + "/scene.tif" );
    std::filesystem::rename( dir + "/scene_old.tif", dir + "/scene.tif" );
  }
}

TEST_CASE( "a range-ignoring origin is served honestly through full-object answers",
           "[io][remote][range_cache][fallback]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "norange" );
  const std::vector<unsigned char> payload = buildSmallTiff( dir + "/scene.tif" );
  // NoRange: every GET answers the whole object from byte 0.
  HttpRangeServer server( payload, testsupport::ServerBehavior::NoRange );

  RangeCacheConfig config;
  config.blockSize = 16 * 1024;
  config.maxCacheBytes = 8ull * 1024 * 1024;
  InstalledCache guard( config );

  RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
  REQUIRE( reader.isOpen() );
  const std::vector<double> window = reader.readWindow( { 1 }, { 17, 23, 64, 64 } );
  CHECK( window.size() == 64ull * 64 );
  // The full-object answers covered every request: served from cache,
  // without any fallback and without pretending range support.
  CHECK( RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64() == 0 );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  CHECK( window == local.readWindow( { 1 }, { 17, 23, 64, 64 } ) );
}

TEST_CASE( "an oversized range-ignoring origin fails with typed errors, never wrong bytes",
           "[io][remote][range_cache][fallback]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "oversized" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" ); // ~4 MB
  HttpRangeServer server( payload, testsupport::ServerBehavior::NoRange );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  InstalledCache guard( config );

  // The byte budget aborts every answer of this origin before ranged reads
  // could be satisfied: the layer must fail LOUDLY (typed error), never
  // serve fabricated or shifted bytes.
  try
  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    if ( reader.isOpen() )
      ( void ) reader.readWindow( { 1 }, { 0, 0, 64, 64 } );
    FAIL( "expected a typed failure for an unservable origin" );
  }
  catch ( const GeoError & )
  {
    SUCCEED( "typed failure surfaced" );
  }
}


TEST_CASE( "cache configuration and surface helpers are honest",
           "[io][remote][range_cache][unit]" )
{
  RangeCacheConfig config;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  InstalledCache guard( config );
  CHECK( RemoteRangeCache::installed() );
  CHECK( RemoteRangeCache::currentConfig().stalePolicy == RangeCacheStalePolicy::ValidateOnce );
  CHECK( RemoteRangeCache::currentConfig().blockSize == config.blockSize );
  CHECK( RemoteRangeCache::telemetryJson()["cached_bytes"].asUInt64() == 0 );

  CHECK_THROWS_AS( RemoteRangeCache::cachedPath( "C:/local/file.tif" ), GeoError );
  CHECK( !RemoteRangeCache::isCachePath( "https://example.com/x.tif" ) );
  CHECK( RemoteRangeCache::isCachePath( "/vsirangecache/https://example.com/x.tif" ) );
  CHECK( RemoteRangeCache::cachedPath( "https://example.com/x.tif" ) ==
         "/vsirangecache/https://example.com/x.tif" );
  CHECK( rangeCacheStalePolicyName( RangeCacheStalePolicy::RevalidateOnOpen ) ==
         std::string( "revalidate_on_open" ) );
  CHECK( rangeCacheStalePolicyFromName( "trust_forever" ) == RangeCacheStalePolicy::TrustForever );
  CHECK_THROWS_AS( rangeCacheStalePolicyFromName( "nonsense" ), GeoError );

  RemoteRangeCache::uninstall();
  CHECK( !RemoteRangeCache::installed() );
}

TEST_CASE( "re-install with a changed blockSize drops entries and stays byte-correct",
           "[io][remote][range_cache][config]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "reinstall" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );

  RangeCacheConfig config;
  config.blockSize = 48 * 1024;
  {
    InstalledCache guard( config );
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    REQUIRE( reader.isOpen() );
    const std::vector<double> a = reader.readWindow( { 1 }, { 0, 0, 128, 128 } );
    CHECK( RemoteRangeCache::telemetryJson()["cached_bytes"].asUInt64() > 0 );
  }

  // Re-install with a DIFFERENT blockSize: stale-config blocks would be
  // misinterpreted by the new indexing — they must be dropped, and reads
  // must stay byte-correct afterwards.
  config.blockSize = 16 * 1024;
  {
    InstalledCache guard( config );
    RemoteRangeCache::install( config ); // re-install replaces the config
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    REQUIRE( reader.isOpen() );
    const std::vector<double> b = reader.readWindow( { 1 }, { 64, 64, 128, 128 } );
    RasterReader local = RasterReader::open( dir + "/scene.tif" );
    CHECK( b == local.readWindow( { 1 }, { 64, 64, 128, 128 } ) );
  }
}

// ---------------------------------------------------------------------------
// 8.0 — fault-injection and concurrency evidence (Data Fabric track, pkg C)
// ---------------------------------------------------------------------------

TEST_CASE( "concurrent readers of one resource share the origin fetch",
           "[io][remote][range_cache][concurrency][utc8]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "concurrent" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setConcurrency( 4 ); // thread-per-connection, bounded at 4
  server.setEtag( "\"concurrent-1\"" );

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 4ull * 1024 * 1024;
  InstalledCache guard( config );

  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );
  // Telemetry counters are process-cumulative: diff around this test's reads.
  const std::uint64_t fetchedBefore = RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();

  // The expected window bytes, from the local file.
  std::vector<double> expected;
  {
    RasterReader local( RasterReader::open( dir + "/scene.tif" ) );
    expected = local.readWindow( { 1 }, { 0, 0, 256, 256 } );
    REQUIRE( expected.size() == 256ull * 256 );
  }

  // Four readers race for the same window. The fetch mutex dedups the
  // in-flight fetch: total origin bytes must stay far below 4× one window
  // worth of blocks, and every reader must see identical, correct bytes.
  constexpr int kReaders = 4;
  std::vector<std::vector<double>> results( kReaders );
  std::vector<bool> ok( kReaders, false );
  {
    std::vector<std::thread> readers;
    for ( int i = 0; i < kReaders; ++i )
    {
      readers.emplace_back( [ &, i ] {
        try
        {
          RasterReader reader = RasterReader::open( cachedPath );
          if ( !reader.isOpen() )
            return;
          results[ i ] = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
          ok[ i ] = results[ i ].size() == expected.size();
        }
        catch ( ... )
        {
          ok[ i ] = false;
        }
      } );
    }
    for ( std::thread &reader : readers )
      reader.join();
  }
  for ( int i = 0; i < kReaders; ++i )
  {
    INFO( "reader " << i );
    REQUIRE( ok[ i ] );
    CHECK( results[ i ] == expected );
  }

  const Json::Value telemetry = RemoteRangeCache::telemetryJson();
  // Four racing readers must not multiply the origin cost by the reader
  // count: the fetch mutex dedups the in-flight block fetches, so the origin
  // cost stays at the distinct-block total plus per-open metadata, far below
  // 4× one window's block bytes.
  const std::uint64_t fetchedBytes =
    telemetry["bytes_fetched"].asUInt64() - fetchedBefore;
  INFO( "fetched this test=" << fetchedBytes );
  // Distinct bytes (window tiles + per-open metadata) ≈ 266 KB observed;
  // the bound must reject a no-dedup run (~4× ≈ 1.05 MB) while leaving the
  // dedup run generous headroom.
  const std::uint64_t windowBlockBytes = 4ull * 64 * 1024; // 256×256 Float32 ≤ 4 blocks
  CHECK( fetchedBytes < windowBlockBytes * 2 );
  // The byte bound above is the dedup proof; also count the mechanism
  // directly (waiters that found the concurrent fetch's results cached).
  INFO( "dedup hits this file so far=" << telemetry["dedup_hits"].asUInt64() );
  CHECK( telemetry["dedup_hits"].asUInt64() > 0 );
}

TEST_CASE( "adjacent and overlapping window reads stay byte-correct and pay once",
           "[io][remote][range_cache][adjacency][utc8]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "adjacent" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"adjacent-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );

  std::vector<double> leftHalf, rightHalf, whole;
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    leftHalf = reader.readWindow( { 1 }, { 0, 0, 256, 128 } );
    rightHalf = reader.readWindow( { 1 }, { 256, 0, 256, 128 } );
    const std::uint64_t fetchedAfterHalves =
      RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
    // The overlapping full-width re-read must come out of the cache.
    whole = reader.readWindow( { 1 }, { 0, 0, 512, 128 } );
    CHECK( RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64() ==
           fetchedAfterHalves + 0 );
  }
  CHECK( leftHalf == local.readWindow( { 1 }, { 0, 0, 256, 128 } ) );
  CHECK( rightHalf == local.readWindow( { 1 }, { 256, 0, 256, 128 } ) );
  CHECK( whole == local.readWindow( { 1 }, { 0, 0, 512, 128 } ) );
}

TEST_CASE( "mid-range connection reset degrades to the fallback without wrong bytes",
           "[io][remote][range_cache][reset][utc8]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "reset" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  // The FIRST ranged read beyond the identity head window (bytes≥1024) is
  // reset, then the origin recovers — the transient-fault shape.
  HttpRangeServer server( payload, testsupport::ServerBehavior::ResetRanged );
  server.setEtag( "\"reset-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );

  // The first blocks (inside the head window) cache normally; deeper blocks
  // hit the reset and must degrade to the direct /vsicurl/ fallback — the
  // bytes the reader sees are still the origin's, never garbage.
  std::vector<double> head, deep;
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    head = reader.readWindow( { 1 }, { 0, 0, 128, 128 } );
    deep = reader.readWindow( { 1 }, { 0, 512, 256, 128 } ); // ~512 KB into the file
  }
  CHECK( head == local.readWindow( { 1 }, { 0, 0, 128, 128 } ) );
  CHECK( deep == local.readWindow( { 1 }, { 0, 512, 256, 128 } ) );
  CHECK( RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64() > 0 );
  // The reset fault itself fired (not merely some unrelated fallback).
  CHECK( server.resetFired() );
}

TEST_CASE( "changed content under the same URL invalidates across generations",
           "[io][remote][range_cache][stale][utc8]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "changed" );
  const std::vector<unsigned char> payloadV1 = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payloadV1 );
  server.setEtag( "\"gen-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::RevalidateOnOpen;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  std::vector<double> v1;
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    v1 = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
    REQUIRE( v1.size() == 256ull * 256 );
  }

  // The origin swaps the object (new bytes, new ETag).
  const std::vector<unsigned char> payloadV2 = buildSmallTiff( dir + "/scene_v2.tif" );
  server.replacePayload( payloadV2, "\"gen-2\"", "Wed, 09 Sep 2026 08:00:00 GMT" );
  {
    RasterReader local = RasterReader::open( dir + "/scene_v2.tif" );
    std::vector<double> v2;
    // A reader still open against the OLD generation finishes with coherent
    // v1 bytes (invalidation restarts are bounded; here the handle is already
    // at EOF for this window, so no blend can occur)…
    RasterReader staleReader = RasterReader::open( cachedPath ); // revalidated: drops v1 blocks
    REQUIRE( staleReader.isOpen() );
    // …and a fresh read after the mismatch returns v2, byte-correct.
    v2 = staleReader.readWindow( { 1 }, { 0, 0, 256, 256 } );
    CHECK( v2 == local.readWindow( { 1 }, { 0, 0, 256, 256 } ) );
    CHECK( v2 != v1 );
  }
  CHECK( RemoteRangeCache::telemetryJson()["invalidations"].asUInt64() >= 1 );
}

TEST_CASE( "COG overview and window reads through the cache stay bounded and correct",
           "[io][remote][range_cache][cog][utc8]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "cog" );
  const std::string plain = dir + "/plain.tif";
  {
    RasterWriter writer = RasterWriter::create( plain, 1024, 1024, { RasterBandSpec {} },
                                                { "GTiff", { "TILED=YES", "BLOCKXSIZE=256", "BLOCKYSIZE=256" }, true } );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
    // A deflate-hostile pattern (xorshift) keeps the COG near its raw size
    // so the byte-accounting bound below is meaningful.
    std::vector<double> values( 1024ull * 1024 );
    std::uint32_t state = 0x9E3779B9u;
    for ( std::size_t i = 0; i < values.size(); ++i )
    {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      values[ i ] = static_cast<double>( state % 1000003u );
    }
    RasterWindow full;
    full.width = 1024;
    full.height = 1024;
    writer.writeWindow( 1, full, values.data() );
    writer.finalize();
  }
  const std::string cog = dir + "/cog.tif";
  makeCog( plain, cog, CogPreset::LosslessScientific ); // throws GeoError on failure
  std::ifstream cogFile( cog, std::ios::binary );
  const std::vector<unsigned char> payload( ( std::istreambuf_iterator<char>( cogFile ) ),
                                            std::istreambuf_iterator<char>() );
  REQUIRE( payload.size() > 0 );

  HttpRangeServer server( payload );
  server.setEtag( "\"cog-1\"" );
  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 8ull * 1024 * 1024;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  // Open the cached COG with GDAL directly (full driver stack: overviews,
  // masks, window reads) and read a reduced-resolution overview + a window.
  GDALAllRegister();
  const std::uint64_t fetchedBefore = RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
  GDALDatasetH cached = GDALOpen( cachedPath.c_str(), GA_ReadOnly );
  REQUIRE( cached != nullptr );
  GDALDatasetH local = GDALOpen( cog.c_str(), GA_ReadOnly );
  REQUIRE( local != nullptr );
  CHECK( GDALGetRasterCount( cached ) == GDALGetRasterCount( local ) );

  GDALRasterBandH cachedOverview = GDALGetRasterBand( cached, 1 );
  CHECK( cachedOverview != nullptr );
  cachedOverview = GDALGetOverview( GDALGetRasterBand( cached, 1 ), 0 );
  GDALRasterBandH localOverview = GDALGetOverview( GDALGetRasterBand( local, 1 ), 0 );
  if ( cachedOverview != nullptr && localOverview != nullptr )
  {
    int ow = 0, oh = 0;
    GDALGetBlockSize( cachedOverview, &ow, &oh );
    const int readW = std::min( 64, ow );
    const int readH = std::min( 64, oh );
    std::vector<float> cachedPixels( static_cast<std::size_t>( readW ) * readH );
    std::vector<float> localPixels( static_cast<std::size_t>( readW ) * readH );
    REQUIRE( GDALRasterIO( cachedOverview, GF_Read, 0, 0, readW, readH,
                           cachedPixels.data(), readW, readH, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( localOverview, GF_Read, 0, 0, readW, readH,
                           localPixels.data(), readW, readH, GDT_Float32, 0, 0 ) == CE_None );
    CHECK( cachedPixels == localPixels );
  }
  // Full-resolution window: byte-equal to the local COG.
  {
    std::vector<float> cachedPixels( 128ull * 128 );
    std::vector<float> localPixels( 128ull * 128 );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( cached, 1 ), GF_Read, 0, 0, 128, 128,
                           cachedPixels.data(), 128, 128, GDT_Float32, 0, 0 ) == CE_None );
    REQUIRE( GDALRasterIO( GDALGetRasterBand( local, 1 ), GF_Read, 0, 0, 128, 128,
                           localPixels.data(), 128, 128, GDT_Float32, 0, 0 ) == CE_None );
    CHECK( cachedPixels == localPixels );
  }
  GDALClose( cached );
  GDALClose( local );

  const std::uint64_t fetched = RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64() - fetchedBefore;
  // Byte accounting: opening a COG (IFD walks, overview directories, mask
  // handling) plus one overview tile (a full 256×256 tile is inflated even
  // for a 64×64 window — the driver's granularity) plus a full-res window
  // costs a bounded MINORITY of the object, never the whole file. Measured
  // locally: ~1.7 MB fetched of a ~4.2 MB deflate-hostile COG (~40%).
  INFO( "fetched=" << fetched << " payload=" << payload.size() );
  CHECK( fetched * 2 < static_cast<std::uint64_t>( payload.size() ) );
  CHECK( fetched > 0 ); // the reads did go through the ranged cache
}

// ---------------------------------------------------------------------------
// 9.0 M2 — truncation gate: a well-formed 206 whose body is shorter than its
// echoed window must never be served or cached. The read degrades to the
// /vsicurl/ fallback and stays byte-correct.
// ---------------------------------------------------------------------------

TEST_CASE( "truncated 206 bodies are refused — fallback keeps reads byte-correct",
           "[io][remote][range_cache][fabric9][truncation]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "short_range" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload, ServerBehavior::ShortRange );
  server.setEtag( "\"short-range-1\"" );

  // Refusal evidence: the read must degrade to the /vsicurl/ fallback (the
  // gate threw). Without the gate, the read would be served from the cache
  // and this counter would not move.
  const std::uint64_t fallbackBefore = RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64();

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::TrustForever; // isolate the body gate
  InstalledCache guard( config );

  std::vector<double> expected;
  {
    RasterReader local( RasterReader::open( dir + "/scene.tif" ) );
    expected = local.readWindow( { 1 }, { 0, 0, 256, 256 } );
    REQUIRE( expected.size() == 256ull * 256 );
  }

  // Every ranged GET beyond the identity head answers 4 bytes of body —
  // the cache must throw internally (truncation gate), degrade to the
  // fallback, and STILL deliver the correct bytes to the reader.
  RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
  REQUIRE( reader.isOpen() );
  const std::vector<double> got = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
  REQUIRE( got.size() == expected.size() );
  CHECK( got == expected );
  REQUIRE( server.shortRangeFired() ); // the fault really fired — no vacuous test
  // The truncated answer was REFUSED, not served from the cache path.
  CHECK( RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64() > fallbackBefore );

  // A second read stays correct too (the truncated body was never cached).
  const std::vector<double> again = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
  CHECK( again == expected );
}


// ---------------------------------------------------------------------------
// 9.0 M3 — hierarchical cache: the optional disk block layer under the
// memory cache. Blocks are content-identity keyed, checksummed, atomically
// published and LRU/byte-capped. A resource with NO provable identity is
// never disk-cached (fail-closed).
// ---------------------------------------------------------------------------

TEST_CASE( "the disk layer serves memory-cold reads without origin refetch",
           "[io][remote][range_cache][fabric9][disk]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "disk" );
  const std::string diskDir = dir + "/blocks";
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"disk-identity-1\"" ); // strong ETag ⇒ provable disk identity

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 4ull * 1024 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::TrustForever; // isolate the disk path
  config.diskDirectory = diskDir;
  config.diskMaxBytes = 16ull * 1024 * 1024;
  InstalledCache guard( config );

  std::vector<double> expected;
  {
    RasterReader local( RasterReader::open( dir + "/scene.tif" ) );
    expected = local.readWindow( { 1 }, { 0, 0, 256, 256 } );
    REQUIRE( expected.size() == 256ull * 256 );
  }

  // Cold pass: fetch fills memory AND publishes disk blocks.
  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    CHECK( reader.readWindow( { 1 }, { 0, 0, 256, 256 } ) == expected );
  }
  const Json::Value statsAfterFill = RemoteRangeCache::diskCacheStatsJson();
  CHECK( statsAfterFill["enabled"].asBool() );
  CHECK( statsAfterFill["puts"].asUInt64() > 0 );

  // Cold pass origin cost: the fetch + per-open metadata probes.
  const std::uint64_t fetchedAfterFill = RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
  const std::uint64_t diskHitsBefore = RemoteRangeCache::diskCacheStatsJson()["hits"].asUInt64();

  // Evict EVERYTHING from memory: disk becomes the only warm layer.
  RemoteRangeCache::clearEntries();

  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    const std::vector<double> got = reader.readWindow( { 1 }, { 0, 0, 256, 256 } );
    REQUIRE( got.size() == expected.size() );
    CHECK( got == expected );
  }
  const Json::Value statsAfterDisk = RemoteRangeCache::diskCacheStatsJson();
  CHECK( statsAfterDisk["hits"].asUInt64() > diskHitsBefore );

  // Origin cost unchanged beyond per-open metadata: the window bytes came
  // from the disk layer, not a refetch.
  const std::uint64_t fetchedAfterDiskRead = RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
  CHECK( fetchedAfterDiskRead < fetchedAfterFill + 256ull * 1024 );
}

TEST_CASE( "corrupt disk blocks are refused and re-fetched, never served",
           "[io][remote][range_cache][fabric9][disk][corruption]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "disk_corrupt" );
  const std::string diskDir = dir + "/blocks";
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"disk-corrupt-1\"" );

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 4ull * 1024 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::TrustForever;
  config.diskDirectory = diskDir;
  config.diskMaxBytes = 16ull * 1024 * 1024;
  InstalledCache guard( config );

  std::vector<double> expected;
  {
    RasterReader local( RasterReader::open( dir + "/scene.tif" ) );
    expected = local.readWindow( { 1 }, { 0, 0, 128, 128 } );
    REQUIRE( expected.size() == 128ull * 128 );
  }
  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    CHECK( reader.readWindow( { 1 }, { 0, 0, 128, 128 } ) == expected );
  }

  // Flip bytes in ONE block file — a torn/broken block on disk.
  const std::filesystem::path blocks( diskDir );
  std::filesystem::path victim;
  for ( const auto &entry : std::filesystem::directory_iterator( blocks ) )
  {
    if ( entry.path().extension() == ".blk" )
    {
      victim = entry.path();
      break;
    }
  }
  REQUIRE( !victim.empty() );
  {
    std::ofstream out( victim, std::ios::binary | std::ios::in );
    out.seekp( 60 ); // inside the data region (4+8+8+32 header)
    out << "\xDE\xAD\xBE\xEF";
  }
  const std::uint64_t corruptBefore = RemoteRangeCache::diskCacheStatsJson()["corrupt"].asUInt64();

  RemoteRangeCache::clearEntries(); // cold memory
  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    const std::vector<double> got = reader.readWindow( { 1 }, { 0, 0, 128, 128 } );
    REQUIRE( got.size() == expected.size() );
    CHECK( got == expected ); // corrupt block was a miss: refetch served truth
  }
  CHECK( RemoteRangeCache::diskCacheStatsJson()["corrupt"].asUInt64() > corruptBefore );
}

TEST_CASE( "disk identityBasis requires a strong ETag (LM alone is refused)",
           "[io][remote][range_cache][fabric9][disk][issue1228]" )
{
  // Same-second same-size rewrites make size+Last-Modified unsafe for disk.
  const std::string etagBasis = RangeDiskBlockStore::identityBasis(
    "https://Example.COM:443/a.tif", true, "\"v1\"", true, 100, "Mon, 01 Jan 2024 00:00:00 GMT" );
  CHECK( etagBasis == "etag\nhttps://Example.COM:443/a.tif\n\"v1\"" );

  const std::string lmOnly = RangeDiskBlockStore::identityBasis(
    "https://example.com/a.tif", false, "", true, 100, "Mon, 01 Jan 2024 00:00:00 GMT" );
  CHECK( lmOnly.empty() );

  const std::string weak = RangeDiskBlockStore::identityBasis(
    "https://example.com/a.tif", false, "W/\"v1\"", true, 100, "Mon, 01 Jan 2024 00:00:00 GMT" );
  CHECK( weak.empty() );
}

TEST_CASE( "resources without provable identity are never disk-cached",
           "[io][remote][range_cache][fabric9][disk][fail-closed]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "disk_failclosed" );
  const std::string diskDir = dir + "/blocks";
  const std::vector<unsigned char> payload = buildSmallTiff( dir + "/small.tif" );
  HttpRangeServer server( payload ); // NO ETag, NO Last-Modified: unprovable

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::TrustForever;
  config.diskDirectory = diskDir;
  config.diskMaxBytes = 16ull * 1024 * 1024;
  InstalledCache guard( config );

  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    REQUIRE( reader.isOpen() );
    RasterWindow full;
    full.width = 256;
    full.height = 256;
    CHECK( reader.readWindow( { 1 }, full ).size() == 256ull * 256 );
  }
  const Json::Value stats = RemoteRangeCache::diskCacheStatsJson();
  CHECK( stats["puts"].asUInt64() == 0 ); // unprovable identity ⇒ never masquerades as cacheable
}

TEST_CASE( "the disk layer byte cap evicts LRU blocks without breaking reads",
           "[io][remote][range_cache][fabric9][disk][eviction]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "disk_evict" );
  const std::string diskDir = dir + "/blocks";
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"disk-evict-1\"" );

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 4ull * 1024 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::TrustForever;
  config.diskDirectory = diskDir;
  // Two windows' worth of blocks blow the cap ⇒ eviction must fire.
  config.diskMaxBytes = 128ull * 1024;
  InstalledCache guard( config );

  std::vector<double> expectedTop;
  std::vector<double> expectedFar;
  {
    RasterReader local( RasterReader::open( dir + "/scene.tif" ) );
    expectedTop = local.readWindow( { 1 }, { 0, 0, 256, 256 } );
    expectedFar = local.readWindow( { 1 }, { 512, 512, 256, 256 } );
  }
  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    CHECK( reader.readWindow( { 1 }, { 0, 0, 256, 256 } ) == expectedTop );
    CHECK( reader.readWindow( { 1 }, { 512, 512, 256, 256 } ) == expectedFar );
  }
  CHECK( RemoteRangeCache::diskCacheStatsJson()["evictions"].asUInt64() > 0 );

  // Post-eviction reads stay byte-correct (evicted blocks re-fetch).
  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
    CHECK( reader.readWindow( { 1 }, { 0, 0, 256, 256 } ) == expectedTop );
    CHECK( reader.readWindow( { 1 }, { 512, 512, 256, 256 } ) == expectedFar );
  }
}

// ---------------------------------------------------------------------------
// 9.0 M3 staging: concurrent publishers must all land under their final
// "<block>.blk" names, byte-verifiable, with no ".tmp" residue — the first
// direct concurrent exercise of RangeDiskBlockStore::putBlock. The Windows
// branch of the temp-name PID source is guarded at COMPILE time by the
// `sicnu_range_cache_disk_pid_guard` object target in tests/CMakeLists.txt
// (#1055): it recompiles this TU with UCRT non-STDC names suppressed, so an
// unguarded ::getpid() on the MSVC path fails the test build, not this case.
// ---------------------------------------------------------------------------

TEST_CASE( "concurrent disk publishes all land with no temp residue",
           "[io][remote][range_cache][fabric9][disk][staging]" )
{
  const std::string dir = scratch( "disk_staging" );
  const std::string diskDir = dir + "/blocks";
  const std::string basis = "etag\nhttps://example.com/range-cache-staging\n\"stage-1\"";

  constexpr int kPublishers = 24;
  std::vector<std::vector<unsigned char>> payloads( kPublishers );
  for ( int i = 0; i < kPublishers; ++i )
    payloads[i].assign( 4096, static_cast<unsigned char>( i + 1 ) );

  InstalledDiskStore store( diskDir, 16ull * 1024 * 1024 );
  REQUIRE( RangeDiskBlockStore::enabled() );

  std::vector<std::thread> publishers;
  for ( int i = 0; i < kPublishers; ++i )
  {
    publishers.emplace_back( [ &, i ] {
      RangeDiskBlockStore::putBlock( basis, static_cast<std::uint64_t>( i ),
                                     payloads[i].data(), payloads[i].size() );
    } );
  }
  for ( std::thread &publisher : publishers )
    publisher.join();

  CHECK( RangeDiskBlockStore::stats().puts == static_cast<std::uint64_t>( kPublishers ) );
  for ( int i = 0; i < kPublishers; ++i )
  {
    std::vector<unsigned char> got;
    REQUIRE( RangeDiskBlockStore::readBlock( basis, static_cast<std::uint64_t>( i ), got ) );
    CHECK( got == payloads[i] );
  }

  int blocks = 0;
  int temps = 0;
  for ( const std::filesystem::directory_entry &entry :
        std::filesystem::directory_iterator( diskDir ) )
  {
    const std::string name = entry.path().filename().string();
    if ( name.size() >= 6 && name.substr( name.size() - 4 ) == ".blk" )
      ++blocks;
    if ( name.size() > 4 && name.substr( name.size() - 4 ) == ".tmp" )
      ++temps;
  }
  CHECK( blocks == kPublishers );
  CHECK( temps == 0 );
}

// ---------------------------------------------------------------------------
// 9.0 review — over-long 206 bodies: a well-formed answer that sends its
// window PLUS garbage must never leak past the echoed window into block
// indexes that were never fetched. The poisoned next block would otherwise
// be served cache-clean.
// ---------------------------------------------------------------------------

TEST_CASE( "over-long ranged bodies are sliced to the echoed window",
           "[io][remote][range_cache][fabric9][truncation]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "long_range" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload, ServerBehavior::LongRange );
  server.setEtag( "\"long-range-1\"" );

  RangeCacheConfig config;
  config.blockSize = 1024; // window = block: the fault's extra bytes target the NEXT block
  config.maxCacheBytes = 4ull * 1024 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::TrustForever;
  InstalledCache guard( config );

  // Float32 1024-wide rows: pixel row 256 starts at byte 1 MiB (≥ the head
  // window, so the fault is eligible), row 384 immediately follows the first
  // read's byte range — exactly where the over-long body's garbage lands.
  std::vector<double> firstExpected;
  std::vector<double> nextExpected;
  {
    RasterReader local( RasterReader::open( dir + "/scene.tif" ) );
    firstExpected = local.readWindow( { 1 }, { 0, 256, 128, 128 } );
    nextExpected = local.readWindow( { 1 }, { 0, 384, 128, 128 } ); // the bytes the garbage targets
  }

  RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( server.url() ) );
  const std::vector<double> first = reader.readWindow( { 1 }, { 0, 256, 128, 128 } );
  REQUIRE( first == firstExpected );
  REQUIRE( server.longRangeFired() ); // the fault really fired

  // The following bytes must be fetched fresh (correct values), never
  // served from a checksum-valid garbage block the over-long body leaked
  // into.
  const std::vector<double> next = reader.readWindow( { 1 }, { 0, 384, 128, 128 } );
  CHECK( next == nextExpected );
}

// Regression guard (D10 default #7): the loopback proxy exemption is what
// keeps /vsicurl/ fixture reads away from machine-room proxies; if the
// hygiene pass regresses, this fails in seconds instead of hanging a lab
// host. Wall-clock bound: trivially < 1 s.
// ---------------------------------------------------------------------------
// 12.0 — fault-injection round-up: a 416-answering origin (object shrank
// below the requested offsets) and the deterministic fault script.
// ---------------------------------------------------------------------------

TEST_CASE( "a 416-answering origin fails closed and never publishes wrong bytes",
           "[io][remote][range_cache][fault416][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "range416" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  // Every ranged GET with start > 0 answers 416 ("bytes */<size>") — the
  // shape of an origin whose object shrank below the requested offsets. The
  // head window stays normal so identity probes keep working. The fallback
  // reads the SAME origin, so the refused bytes are unavailable EVERYWHERE:
  // the only honest answer is a typed failure, never stale or garbage bytes.
  HttpRangeServer server( payload, ServerBehavior::RangeNotSatisfiable );
  server.setEtag( "\"range416-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  const std::uint64_t fallbackBefore =
    RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64();

  RasterReader reader = RasterReader::open( cachedPath );
  REQUIRE( reader.isOpen() );
  // The satisfiable head window (its ranged fetch starts at byte 0) reads
  // and caches normally — byte-correct against the local file.
  const std::vector<double> head = reader.readWindow( { 1 }, { 0, 0, 128, 128 } );
  CHECK( head == local.readWindow( { 1 }, { 0, 0, 128, 128 } ) );

  // A deep window (ranged fetch with start > 0) is refused by the origin;
  // the /vsicurl/ fallback gets the same refusal. The read must FAIL TYPED —
  // a shrunken object has no correct bytes to serve — and the refused
  // answer (empty body) must never enter the cache.
  bool failedTyped = false;
  try
  {
    reader.readWindow( { 1 }, { 0, 512, 256, 128 } ); // ~512 KB into the file
  }
  catch ( const GeoError & )
  {
    failedTyped = true;
  }
  CHECK( failedTyped );
  CHECK( RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64() > fallbackBefore );

  // The refusal cached nothing: the head window still serves its cached
  // bytes, and re-asking the refused window still cannot produce bytes.
  CHECK( reader.readWindow( { 1 }, { 0, 0, 128, 128 } ) == head );
  CHECK_THROWS_AS( reader.readWindow( { 1 }, { 0, 512, 256, 128 } ), GeoError );
}

TEST_CASE( "the deterministic fault script stages per-request behaviors",
           "[io][remote][range_cache][faultscript][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "faultscript" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"faultscript-1\"" );
  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  // The identity probe consumes the scripted 500: the resource cannot be
  // proven, and the open fails quietly (a missing-resource miss, not a
  // wrong answer).
  server.setFaultScript( { ServerBehavior::ServerError } );
  bool openFailedTyped = false;
  try
  {
    RasterReader reader( RasterReader::open( cachedPath ) );
  }
  catch ( const GeoError & )
  {
    openFailedTyped = true;
  }
  CHECK( openFailedTyped );

  // Past the script the origin is normal: the same spelling opens and the
  // bytes are correct — the outage was exactly one request long.
  {
    RasterReader local = RasterReader::open( dir + "/scene.tif" );
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    CHECK( reader.readWindow( { 1 }, { 0, 0, 128, 128 } ) ==
           local.readWindow( { 1 }, { 0, 0, 128, 128 } ) );
  }

  // A script can also arm faults for body fetches of an ALREADY-open
  // resource: the next new-window fetch hits the scripted 500 and degrades
  // to the fallback (which reads the now-healthy origin) — correct bytes,
  // honest fallback accounting.
  const std::uint64_t fallbackBefore =
    RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64();
  server.setFaultScript( { ServerBehavior::ServerError } );
  {
    RasterReader local = RasterReader::open( dir + "/scene.tif" );
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    // Window at ~1 MB: a NEW block range (ValidateOnce skips revalidation,
    // so the next request is the body fetch itself).
    CHECK( reader.readWindow( { 1 }, { 0, 768, 128, 128 } ) ==
           local.readWindow( { 1 }, { 0, 768, 128, 128 } ) );
  }
  CHECK( RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64() > fallbackBefore );
}

TEST_CASE( "ranged fetches retry transient failures with bounded backoff",
           "[io][remote][range_cache][backoff][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "backoff" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"backoff-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  config.fetchAttempts = 3;
  config.retryBackoffBaseMs = 20;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  RasterReader reader = RasterReader::open( cachedPath );
  REQUIRE( reader.isOpen() );

  // Two scripted 500s then a healthy answer: the body fetch of a new window
  // must recover INSIDE the cache (no fallback) on the third attempt.
  server.setFaultScript( { ServerBehavior::ServerError, ServerBehavior::ServerError } );
  const std::uint64_t fallbackBefore =
    RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64();
  const std::uint64_t retriesBefore =
    RemoteRangeCache::telemetryJson()["retried_fetches"].asUInt64();
  CHECK( reader.readWindow( { 1 }, { 0, 256, 128, 128 } ) ==
         local.readWindow( { 1 }, { 0, 256, 128, 128 } ) );
  CHECK( RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64() == fallbackBefore );
  CHECK( RemoteRangeCache::telemetryJson()["retried_fetches"].asUInt64() > retriesBefore );

  // The attempt bound is real: a script longer than fetchAttempts exhausts
  // the retries and the read degrades to the fallback (the script's tail is
  // healthy, so the fallback's own read succeeds with correct bytes). A
  // fourth in-cache attempt would have consumed the healthy tail instead.
  server.setFaultScript( { ServerBehavior::ServerError, ServerBehavior::ServerError,
                           ServerBehavior::ServerError, ServerBehavior::Normal } );
  const std::uint64_t fallbackBefore2 =
    RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64();
  CHECK( reader.readWindow( { 1 }, { 0, 512, 128, 128 } ) ==
         local.readWindow( { 1 }, { 0, 512, 128, 128 } ) );
  CHECK( RemoteRangeCache::telemetryJson()["fallback_reads"].asUInt64() > fallbackBefore2 );
}

TEST_CASE( "fetch cancellation discards cancelled bytes and the veto lifts cleanly",
           "[io][remote][range_cache][cancel][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "cancel" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"cancel-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  RasterReader reader = RasterReader::open( cachedPath );
  REQUIRE( reader.isOpen() );
  const std::vector<double> head = reader.readWindow( { 1 }, { 0, 0, 128, 128 } );
  const Json::Value warm = RemoteRangeCache::telemetryJson();

  // Cancel: new fetches for this resource must not start (nor cache).
  RemoteRangeCache::cancelFetches( server.url() );
  const std::vector<double> vetoed = reader.readWindow( { 1 }, { 0, 512, 128, 128 } );
  CHECK( vetoed == local.readWindow( { 1 }, { 0, 512, 128, 128 } ) ); // fallback keeps it correct
  const Json::Value during = RemoteRangeCache::telemetryJson();
  CHECK( during["bytes_fetched"].asUInt64() == warm["bytes_fetched"].asUInt64() );
  CHECK( during["coalesced_fetches"].asUInt64() == warm["coalesced_fetches"].asUInt64() );
  CHECK( during["fallback_reads"].asUInt64() > warm["fallback_reads"].asUInt64() );
  CHECK( during["cached_bytes"].asUInt64() == warm["cached_bytes"].asUInt64() );

  // Resume: the origin is reachable again — the next new window fetches,
  // caches, and a repeat read pays zero origin bytes (warm-cache oracle).
  RemoteRangeCache::resumeFetches( server.url() );
  const std::vector<double> after = reader.readWindow( { 1 }, { 0, 768, 128, 128 } );
  CHECK( after == local.readWindow( { 1 }, { 0, 768, 128, 128 } ) );
  const Json::Value resumed = RemoteRangeCache::telemetryJson();
  CHECK( resumed["bytes_fetched"].asUInt64() > during["bytes_fetched"].asUInt64() );
  const std::uint64_t fetchedAfterResume = resumed["bytes_fetched"].asUInt64();
  CHECK( reader.readWindow( { 1 }, { 0, 768, 128, 128 } ) == after );
  CHECK( RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64() == fetchedAfterResume );
}

TEST_CASE( "a fetch cancelled MID-FLIGHT never publishes its bytes",
           "[io][remote][range_cache][cancel][race][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "cancelmid" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload, ServerBehavior::Slow ); // 3 s per answer
  server.setEtag( "\"cancelmid-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  config.timeoutSeconds = 15;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader reader = RasterReader::open( cachedPath );
  REQUIRE( reader.isOpen() );
  const Json::Value before = RemoteRangeCache::telemetryJson();

  // A slow body fetch of a deep window is in flight when the cancel lands:
  // the fetch's bytes must be discarded (fallback answers the reader), so
  // bytes_fetched/coalesced_fetches stay flat and nothing new is cached.
  std::atomic<bool> readDone{ false };
  std::vector<double> deep;
  const int requestsAfterOpen = server.requestCount();
  std::thread slowReader( [ & ] {
    deep = reader.readWindow( { 1 }, { 0, 512, 128, 128 } );
    readDone.store( true );
  } );
  // Wait until a request BEYOND the open phase arrives — the body fetch is
  // then provably in flight (the Slow origin holds it ~3 s) — and cancel
  // while it runs, so the discard-after-fetch path is what gets tested.
  for ( int i = 0; i < 300 && server.requestCount() <= requestsAfterOpen; ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  REQUIRE( server.requestCount() > requestsAfterOpen );
  RemoteRangeCache::cancelFetches( server.url() );
  slowReader.join();
  REQUIRE( readDone.load() );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  CHECK( deep == local.readWindow( { 1 }, { 0, 512, 128, 128 } ) ); // correct via fallback
  const Json::Value after = RemoteRangeCache::telemetryJson();
  CHECK( after["bytes_fetched"].asUInt64() == before["bytes_fetched"].asUInt64() );
  CHECK( after["cached_bytes"].asUInt64() == before["cached_bytes"].asUInt64() );

  // The veto lifts: a FRESH reader (a fresh GDAL dataset — the shared one's
  // driver-level block cache would answer without reaching the VSI layer)
  // fetches the never-cached window and caches it normally.
  RemoteRangeCache::resumeFetches( server.url() );
  const std::uint64_t fetchedBefore = after["bytes_fetched"].asUInt64();
  RasterReader fresh = RasterReader::open( cachedPath );
  REQUIRE( fresh.isOpen() );
  CHECK( fresh.readWindow( { 1 }, { 0, 512, 128, 128 } ) == deep );
  CHECK( RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64() > fetchedBefore );
}

TEST_CASE( "long resource churn stays inside the byte budgets with a peak gauge",
           "[io][remote][range_cache][churn][budget][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "churn" );
  // 8 distinct origins (distinct identity bases ⇒ distinct disk files too),
  // each demanding a block: total demand outstrips every budget below.
  std::vector<unsigned char> payload;
  std::vector<std::unique_ptr<HttpRangeServer>> servers;
  for ( int i = 0; i < 8; ++i )
  {
    if ( payload.empty() )
      payload = buildTiff( dir + "/scene.tif" );
    servers.push_back( std::make_unique<HttpRangeServer>( payload ) );
    servers.back()->setEtag( "\"churn-" + std::to_string( i ) + "\"" );
  }

  // Memory churn: the global budget is 4 blocks, the demand is 8.
  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 4ull * 64 * 1024;
  config.maxBytesPerResource = 2ull * 64 * 1024; // one resource may hold ≤ 2 blocks
  InstalledCache guard( config );

  std::vector<std::vector<double>> expected;
  std::vector<std::unique_ptr<RasterReader>> locals;
  std::vector<std::string> cachedPaths;
  const RasterWindow deepWindow { 0, 512, 128, 128 }; // ~256-384 KB: blocks 4-5
  for ( int i = 0; i < 8; ++i )
  {
    locals.push_back( std::make_unique<RasterReader>( RasterReader::open( dir + "/scene.tif" ) ) );
    expected.push_back( locals.back()->readWindow( { 1 }, deepWindow ) );
    cachedPaths.push_back( RemoteRangeCache::cachedPath( servers[i]->url() ) );
  }

  const std::uint64_t evictionsBefore =
    RemoteRangeCache::telemetryJson()["evictions"].asUInt64();
  for ( int round = 0; round < 2; ++round )
  {
    for ( int i = 0; i < 8; ++i )
    {
      RasterReader reader = RasterReader::open( cachedPaths[i] );
      REQUIRE( reader.isOpen() );
      // Every read must be byte-correct regardless of eviction pressure.
      CHECK( reader.readWindow( { 1 }, deepWindow ) == expected[i] );
    }
  }
  const Json::Value telemetry = RemoteRangeCache::telemetryJson();
  CHECK( telemetry["evictions"].asUInt64() > evictionsBefore );
  // The budget held across the whole churn: cached bytes never exceed the
  // global budget, and the peak gauge records a bounded high-water too.
  CHECK( telemetry["cached_bytes"].asUInt64() <= config.maxCacheBytes );
  CHECK( telemetry["max_cached_bytes"].asUInt64() <= config.maxCacheBytes );
  CHECK( telemetry["max_cached_bytes"].asUInt64() > 0 );
}

TEST_CASE( "the per-resource budget stops one resource from hogging the cache",
           "[io][remote][range_cache][per-resource][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "perresource" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"perresource-1\"" );

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.maxCacheBytes = 64ull * 1024 * 1024; // effectively unbounded globally
  config.maxBytesPerResource = 2ull * 64 * 1024; // but ≤ 2 blocks for this resource
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader reader = RasterReader::open( cachedPath );
  REQUIRE( reader.isOpen() );
  // Four far-apart windows: 8+ distinct blocks of ONE resource.
  for ( const int row : { 0, 256, 512, 768 } )
    reader.readWindow( { 1 }, { 0, row, 64, 64 } );

  const Json::Value telemetry = RemoteRangeCache::telemetryJson();
  // The resource's footprint was capped: ≤ 2 blocks + the final window's
  // overshoot tolerance (a read spanning block boundaries inserts runs that
  // are immediately trimmed back to the cap).
  CHECK( telemetry["cached_bytes"].asUInt64() <= 3ull * 64 * 1024 );
  CHECK( telemetry["evictions"].asUInt64() > 0 );
}

TEST_CASE( "disk-layer churn across many resources stays inside the byte cap",
           "[io][remote][range_cache][disk][churn][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "diskchurn" );
  const std::string diskDir = dir + "/disk";
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );

  std::vector<std::unique_ptr<HttpRangeServer>> servers;
  std::vector<std::string> urls;
  for ( int i = 0; i < 4; ++i )
  {
    servers.push_back( std::make_unique<HttpRangeServer>( payload ) );
    servers.back()->setEtag( "\"diskchurn-" + std::to_string( i ) + "\"" );
    urls.push_back( servers.back()->url() );
  }

  InstalledCache guard( RangeCacheConfig {} );
  InstalledDiskStore disk( diskDir, 2ull * 64 * 1024 ); // room for 2 blocks
  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.diskDirectory = diskDir;
  config.diskMaxBytes = 2ull * 64 * 1024;
  RemoteRangeCache::install( config );

  const std::uint64_t evictionsBefore =
    RemoteRangeCache::diskCacheStatsJson()["evictions"].asUInt64();
  for ( const std::string &url : urls )
  {
    RasterReader reader = RasterReader::open( RemoteRangeCache::cachedPath( url ) );
    REQUIRE( reader.isOpen() );
    reader.readWindow( { 1 }, { 0, 0, 64, 64 } ); // block 0 of each resource
  }
  const Json::Value stats = RemoteRangeCache::diskCacheStatsJson();
  CHECK( stats["enabled"].asBool() );
  // Four resources demanded four block files against a two-block cap: the
  // store evicted and its own accounting stays at the cap.
  CHECK( stats["evictions"].asUInt64() > evictionsBefore );
  CHECK( stats["bytes_stored"].asUInt64() <= 2ull * 64 * 1024 );
}

TEST_CASE( "an entry past its TTL is dropped and re-proven even under ValidateOnce",
           "[io][remote][range_cache][ttl][utc12]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "ttl" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"ttl-1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce; // trust within the TTL
  config.entryTtlSeconds = 1;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  const RasterWindow window { 0, 0, 128, 128 };

  // First open probes (a ranged identity GET lands on the server) and the
  // read fetches the window's bytes.
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    CHECK( reader.readWindow( { 1 }, window ) == local.readWindow( { 1 }, window ) );
  }
  const int rangedAfterFirst = server.rangedResponses();

  // A second open INSIDE the TTL is trusted (ValidateOnce): no new ranged
  // identity request is issued — the TTL is the only re-proof trigger.
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    CHECK( reader.readWindow( { 1 }, window ) == local.readWindow( { 1 }, window ) );
  }
  CHECK( server.rangedResponses() == rangedAfterFirst );

  // Past the TTL the entry is gone: the next open must re-prove the
  // resource (a fresh ranged identity GET) and re-fetch the dropped bytes —
  // byte-correct, with no stale serving in between.
  std::this_thread::sleep_for( std::chrono::milliseconds( 1200 ) );
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    CHECK( reader.readWindow( { 1 }, window ) == local.readWindow( { 1 }, window ) );
  }
  CHECK( server.rangedResponses() > rangedAfterFirst );
}

TEST_CASE( "a retry's backoff sleep releases the in-flight admission slot",
           "[io][remote][range_cache][admission][backoff][utc13]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "backoffslot" );
  const std::vector<unsigned char> payloadA = buildTiff( dir + "/a.tif" );
  const std::vector<unsigned char> payloadB = buildTiff( dir + "/b.tif" );
  HttpRangeServer serverA( payloadA );
  HttpRangeServer serverB( payloadB );
  serverA.setEtag( "\"slot-a\"" );
  serverB.setEtag( "\"slot-b\"" );

  RangeCacheConfig config;
  config.blockSize = 64 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  config.maxSingleFetchBytes = 256ull * 1024;
  // cap=1 admits at most ONE transfer at a time (an idle gate still admits
  // a larger-than-cap run head-of-line): A's sleeping retry either frees
  // B immediately (13.0) or pins the gate until the retry lands (12.0).
  config.maxConcurrentFetchBytes = 1;
  config.fetchAttempts = 3;
  config.retryBackoffBaseMs = 2500;               // attempt 2 waits ~2.5 s
  config.retryBackoffMaxMs = 5000;
  InstalledCache guard( config );

  // Resource A: one scripted 500 — its body fetch fails once, then sleeps
  // ~2.5 s in backoff. Resource B: healthy. If the sleeping retry kept its
  // admission (the 12.0 defect), B's fetch could not be admitted until
  // A's retry finished — the cap admits only one transfer at a time. The
  // wide window keeps B's whole open+read comfortably inside the backoff
  // even on a loaded shared host.
  const std::string pathA = RemoteRangeCache::cachedPath( serverA.url() );
  RasterReader readerA = RasterReader::open( pathA );
  REQUIRE( readerA.isOpen() );
  serverA.setFaultScript( { ServerBehavior::ServerError } );

  const int requestsAfterOpen = serverA.requestCount();
  std::atomic<bool> aDone{ false };
  std::vector<double> aRead;
  std::thread readerThreadA( [ & ] {
    try
    {
      aRead = readerA.readWindow( { 1 }, { 0, 0, 256, 256 } );
    }
    catch ( ... )
    {
    }
    aDone.store( true );
  } );
  // Wait until the scripted 500 was consumed — requestCount counts at
  // request ARRIVAL, so a short settle lets the (instant, bodiless) 500
  // response finish: A is then inside its ~2.5 s backoff window, holding
  // (13.0) NO admission slot.
  for ( int i = 0; i < 300 && serverA.requestCount() <= requestsAfterOpen; ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  REQUIRE( serverA.requestCount() > requestsAfterOpen );
  std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
  const int requestsAfterFailure = serverA.requestCount();

  // B's open+read completes inside A's backoff window: under the 12.0
  // admission B's fetch waited out the backoff AND the retry (~2.5 s), so
  // A's second attempt would have landed first — requestCount() would
  // already have advanced when B finished. The admission_waits delta is
  // the sharper occupancy oracle: a released slot admits B instantly
  // (delta 0); a held slot queues it (delta 1).
  const std::uint64_t admissionWaitsBefore =
    RemoteRangeCache::telemetryJson()["admission_waits"].asUInt64();
  std::vector<double> bRead;
  {
    RasterReader readerB = RasterReader::open( RemoteRangeCache::cachedPath( serverB.url() ) );
    REQUIRE( readerB.isOpen() );
    bRead = readerB.readWindow( { 1 }, { 0, 0, 256, 256 } );
  }
  CHECK( serverA.requestCount() == requestsAfterFailure );
  CHECK( RemoteRangeCache::telemetryJson()["admission_waits"].asUInt64() ==
         admissionWaitsBefore );

  readerThreadA.join();
  REQUIRE( aDone.load() );

  // A's retry still lands correctly — the backoff is released, not removed.
  RasterReader localA = RasterReader::open( dir + "/a.tif" );
  RasterReader localB = RasterReader::open( dir + "/b.tif" );
  CHECK( aRead == localA.readWindow( { 1 }, { 0, 0, 256, 256 } ) );
  CHECK( bRead == localB.readWindow( { 1 }, { 0, 0, 256, 256 } ) );

  const Json::Value telemetry = RemoteRangeCache::telemetryJson();
  CHECK( telemetry["backoff_waits"].asUInt64() > 0 );
  CHECK( telemetry["retried_fetches"].asUInt64() > 0 );
  CHECK( telemetry["fetch_attempts"].asUInt64() > 0 );
}

TEST_CASE( "a fetch cancelled during retry backoff exits promptly",
           "[io][remote][range_cache][cancel][backoff][utc13]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "cancelbackoff" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"cancel-bo\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce;
  config.fetchAttempts = 3;
  config.retryBackoffBaseMs = 4000; // attempt 2 would wait ~4 s if unbroken
  config.retryBackoffMaxMs = 8000;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  RasterReader reader = RasterReader::open( cachedPath );
  REQUIRE( reader.isOpen() );
  server.setFaultScript( { ServerBehavior::ServerError } );

  const int requestsAfterOpen = server.requestCount();
  std::atomic<bool> readDone{ false };
  std::vector<double> got;
  std::thread slowRead( [ & ] {
    try
    {
      got = reader.readWindow( { 1 }, { 0, 512, 128, 128 } );
    }
    catch ( ... )
    {
    }
    readDone.store( true );
  } );
  // The scripted 500 lands; the retry enters its ~4 s backoff sleep.
  for ( int i = 0; i < 300 && server.requestCount() <= requestsAfterOpen; ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  REQUIRE( server.requestCount() > requestsAfterOpen );

  RemoteRangeCache::cancelFetches( server.url() );
  // The veto must wake the sleeper — finishing well inside the remaining
  // ~4 s backoff (2.5 s bound leaves 1.5 s of scheduling headroom).
  for ( int i = 0; i < 250 && !readDone.load(); ++i )
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
  const bool woke = readDone.load();
  slowRead.join();
  REQUIRE( woke );

  // Cancelled mid-backoff the read still answers correctly — the fallback
  // (the correctness gate) serves the same bytes; the script's tail is
  // healthy so /vsicurl/ succeeds.
  RasterReader local = RasterReader::open( dir + "/scene.tif" );
  CHECK( got == local.readWindow( { 1 }, { 0, 512, 128, 128 } ) );
  const Json::Value telemetry = RemoteRangeCache::telemetryJson();
  CHECK( telemetry["backoff_waits"].asUInt64() > 0 );
  CHECK( telemetry["cancelled_fetches"].asUInt64() > 0 );
}

TEST_CASE( "stat shares the entry TTL trust horizon with open",
           "[io][remote][range_cache][ttl][stat][utc13]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "stattl" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  server.setEtag( "\"stat-ttl\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::ValidateOnce; // trust within TTL
  config.entryTtlSeconds = 1;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server.url() );

  // Warm the entry through the normal open path.
  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    CHECK( reader.readWindow( { 1 }, { 0, 0, 128, 128 } ).size() == 128ull * 128 );
  }
  const std::uint64_t fileBytes =
    std::filesystem::file_size( std::filesystem::path( dir + "/scene.tif" ) );
  const int requestsAfterWarm = server.requestCount();

  // A stat INSIDE the TTL answers from the proven identity — zero origin.
  VSIStatBufL statBuf;
  REQUIRE( VSIStatL( cachedPath.c_str(), &statBuf ) == 0 );
  CHECK( static_cast<std::uint64_t>( statBuf.st_size ) == fileBytes );
  CHECK( server.requestCount() == requestsAfterWarm );

  // Past the TTL the stat must re-prove exactly like an open: a fresh
  // identity request lands on the server and the answer stays correct.
  // (12.0 defect: stat served the cached size forever — requestCount
  // stayed flat and this check failed.)
  std::this_thread::sleep_for( std::chrono::milliseconds( 1200 ) );
  REQUIRE( VSIStatL( cachedPath.c_str(), &statBuf ) == 0 );
  CHECK( static_cast<std::uint64_t>( statBuf.st_size ) == fileBytes );
  CHECK( server.requestCount() > requestsAfterWarm );
  CHECK( RemoteRangeCache::telemetryJson()["ttl_expirations"].asUInt64() > 0 );

  // The re-proven entry is fresh: another stat issues nothing new — the
  // TTL parity adds no origin storm.
  const int requestsAfterReprobe = server.requestCount();
  REQUIRE( VSIStatL( cachedPath.c_str(), &statBuf ) == 0 );
  CHECK( static_cast<std::uint64_t>( statBuf.st_size ) == fileBytes );
  CHECK( server.requestCount() == requestsAfterReprobe );
}

TEST_CASE( "stat revalidates through the shared trust policy",
           "[io][remote][range_cache][stat][revalidate][utc13]" )
{
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  const std::string dir = scratch( "statreval" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  const std::vector<unsigned char> shrunk = buildSmallTiff( dir + "/shrunk.tif" );
  auto server = std::make_unique<HttpRangeServer>( payload );
  server->setEtag( "\"stat-r1\"" );

  RangeCacheConfig config;
  config.blockSize = 32 * 1024;
  config.stalePolicy = RangeCacheStalePolicy::RevalidateOnOpen;
  InstalledCache guard( config );
  const std::string cachedPath = RemoteRangeCache::cachedPath( server->url() );

  {
    RasterReader reader = RasterReader::open( cachedPath );
    REQUIRE( reader.isOpen() );
    CHECK( reader.readWindow( { 1 }, { 0, 0, 128, 128 } ).size() == 128ull * 128 );
  }

  // Stat under RevalidateOnOpen issues the SAME conditional request an
  // open would: the unchanged origin answers 304 and the trust horizon
  // (and the entry's size) refreshes — never a fabricated answer.
  VSIStatBufL statBuf;
  REQUIRE( VSIStatL( cachedPath.c_str(), &statBuf ) == 0 );
  CHECK( static_cast<std::uint64_t>( statBuf.st_size ) ==
         static_cast<std::uint64_t>( payload.size() ) );
  CHECK( server->notModifiedResponses() > 0 );
  const Json::Value after304 = RemoteRangeCache::telemetryJson();
  CHECK( after304["revalidations"].asUInt64() > 0 );
  CHECK( after304["ttl_refreshes"].asUInt64() > 0 );

  // ETag change + object shrink: the conditional answer mismatches, the
  // entry is invalidated and re-proven — stat reports the NEW size, never
  // the stale one (the 12.0 stat path returned the cached size forever).
  server->replacePayload( shrunk, "\"stat-r2\"", "" );
  REQUIRE( VSIStatL( cachedPath.c_str(), &statBuf ) == 0 );
  CHECK( static_cast<std::uint64_t>( statBuf.st_size ) ==
         static_cast<std::uint64_t>( shrunk.size() ) );

  // No storm: under RevalidateOnOpen a stat pays exactly what an open
  // pays — ONE conditional request (answered 304 here) — never a probe
  // loop or a body fetch.
  const int requestsAfterReprobe = server->requestCount();
  REQUIRE( VSIStatL( cachedPath.c_str(), &statBuf ) == 0 );
  CHECK( static_cast<std::uint64_t>( statBuf.st_size ) ==
         static_cast<std::uint64_t>( shrunk.size() ) );
  CHECK( server->requestCount() - requestsAfterReprobe <= 1 );

  // Offline is explicit and conservative: with the origin unreachable the
  // revalidation is INCONCLUSIVE — stat serves the last proven identity
  // (the declared trust level), never a fabricated zero or an error.
  server.reset();
  REQUIRE( VSIStatL( cachedPath.c_str(), &statBuf ) == 0 );
  CHECK( static_cast<std::uint64_t>( statBuf.st_size ) ==
         static_cast<std::uint64_t>( shrunk.size() ) );
}

TEST_CASE( "offline guard keeps loopback exempt from proxies",
           "[io][offline-guard]" )
{
  sicnu::testsupport::offline::ensureLoopbackProxyHygiene();
  CHECK( sicnu::testsupport::offline::loopbackProxyExempted() );
}
