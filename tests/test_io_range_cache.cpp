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
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "support/http_range_server.h"

#include <cpl_conv.h>
#include <cpl_vsi.h>
#include <cstring>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace sicnu::geo;
using sicnu::geo::testsupport::HttpRangeServer;

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

} // namespace

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
