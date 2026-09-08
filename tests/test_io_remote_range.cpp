/***************************************************************************
  tests/test_io_remote_range.cpp — Foundation 5.0: remote I/O proof harness
  (ADR 0139). A local range-capable HTTP fixture proves that:
    * a windowed /vsicurl/ read downloads a bounded byte count, NOT the file
    * the remote probe reports reachability/size/range support
    * failure modes are typed: server error, truncation, timeout
 ***************************************************************************/

#include "geospatial/remote/probe_remote.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "support/http_range_server.h"

#include <cpl_conv.h>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace sicnu::geo;
using sicnu::geo::testsupport::HttpRangeServer;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_remote" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

/// A 1024×1024 byte raster with a distinctive pattern (4 MiB class payload:
/// large enough that a bounded window read is measurable, small enough for
/// CI). Written as an UNCOMPRESSED tiled GeoTIFF so range reads map to tiles.
std::vector<unsigned char> buildTiff( const std::string &path )
{
  const int size = 1024;
  {
    RasterWriter writer = RasterWriter::create( path, size, size, { RasterBandSpec {} },
                                                { "GTiff", { "TILED=YES", "BLOCKXSIZE=128", "BLOCKYSIZE=128" }, true } );
    writer.setGeotransform( { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 } );
    // Write the top-left 128×128 tile with a known ramp; the rest stays zero.
    std::vector<double> tile( 128 * 128 );
    for ( std::size_t i = 0; i < tile.size(); ++i )
      tile[i] = static_cast<double>( ( i * 7 ) % 251 );
    writer.writeWindow( 1, { 0, 0, 128, 128 }, tile.data() );
    writer.finalize();
  }
  std::ifstream in( path, std::ios::binary );
  return std::vector<unsigned char>( ( std::istreambuf_iterator<char>( in ) ),
                                     std::istreambuf_iterator<char>() );
}
} // namespace

TEST_CASE( "remote probe reports reachability, size and range support",
           "[io][remote][range]" )
{
  const std::string dir = scratch( "probe" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );
  REQUIRE( server.port() > 0 );

  const RemoteProbeResult probe = probeRemote( server.url() );
  CHECK( probe.reachable );
  // Some GDAL builds leave nStatus = 0 on success (no status line is
  // exposed through CPLHTTPResult); reachability is the contract here.
  CHECK( ( probe.httpStatus == 200 || probe.httpStatus == 0 ) );
  CHECK( probe.acceptsRanges );
  CHECK( probe.hasSize );
  CHECK( probe.sizeBytes == payload.size() );
  CHECK( probe.contentType == "image/tiff" );
}

TEST_CASE( "a /vsicurl/ window read stays bounded — never a full download",
           "[io][remote][range][bounded]" )
{
  const std::string dir = scratch( "window" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload );

  // Keep the measurement honest: PAM would fetch a full "<url>.aux.xml"
  // sidecar from the fixture server (which answers 200 for any path) and
  // swamp the byte accounting. Remote PAM is exactly the kind of surprise
  // the bounded-download gate exists to catch.
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  // Bound the vsicurl chunk granularity the same way the production remote
  // defaults do: one window read must not drag multi-megabyte chunks.
  CPLSetConfigOption( "GDAL_HTTP_CHUNK_SIZE", "16" );

  RasterReader reader = RasterReader::open( "/vsicurl/" + server.url() );
  REQUIRE( reader.isOpen() );
  CHECK( reader.metadata().width == 1024 );

  const std::vector<double> window = reader.readWindow( { 1 }, { 0, 0, 64, 64 } );
  REQUIRE( window.size() == 64 * 64 );
  CHECK( window.front() == static_cast<double>( ( 0 * 7 ) % 251 ) );
  CHECK( window[1] == 7.0 );

  // Byte accounting: header/IFD round trips plus one 128×128 tile must stay
  // far below the 1024×1024 payload (the hard gate: no silent full read).
  const std::uint64_t served = server.bytesServed();
  for ( const std::string &entry : server.requestLog() )
    std::fprintf( stderr, "[REQ] %s\n", entry.c_str() );
  CHECK( served < payload.size() / 2 );
}

TEST_CASE( "servers without range support are detected by the probe layer",
           "[io][remote][range][degraded]" )
{
  const std::string dir = scratch( "norange" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );
  HttpRangeServer server( payload, testsupport::ServerBehavior::NoRange );

  // GDAL's own fallback for rangeless origins is a full download — exactly
  // the degenerate transport the 5.0 gate forbids. The probe layer surfaces
  // the situation BEFORE any pixel access so callers can refuse or warn.
  const RemoteProbeResult probe = probeRemote( server.url() );
  CHECK( probe.reachable );
  CHECK( probe.hasSize );
  CHECK( probe.sizeBytes == payload.size() );
  CHECK( probe.acceptsRanges == false );
}

TEST_CASE( "server errors, truncation and timeouts are typed failures",
           "[io][remote][range][failure]" )
{
  const std::string dir = scratch( "failure" );
  const std::vector<unsigned char> payload = buildTiff( dir + "/scene.tif" );

  SECTION( "HTTP 500 → NetworkError with status" )
  {
    HttpRangeServer server( payload, testsupport::ServerBehavior::ServerError );
    bool threw = false;
    try
    {
      probeRemote( server.url() );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      CHECK( error.code() == ErrorCode::NetworkError );
    }
    CHECK( threw );
  }

  SECTION( "truncated body → GDAL fails at open or read, never a silent short read" )
  {
    HttpRangeServer server( payload, testsupport::ServerBehavior::Truncated );
    bool threw = false;
    try
    {
      // The open itself validates TIFF structure; either stage must refuse.
      RasterReader reader = RasterReader::open( "/vsicurl/" + server.url() );
      std::vector<double> window = reader.readWindow( { 1 }, { 0, 0, 64, 64 } );
      ( void ) window;
    }
    catch ( const GeoError & )
    {
      threw = true;
    }
    CHECK( threw );
  }

  SECTION( "a stalled origin surfaces GeoError(Timeout)" )
  {
    HttpRangeServer server( payload, testsupport::ServerBehavior::Slow );
    RemoteProbeOptions options;
    options.timeoutSeconds = 1;
    options.connectTimeoutSeconds = 1;
    bool threw = false;
    try
    {
      probeRemote( server.url(), options );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      CHECK( error.code() == ErrorCode::Timeout );
    }
    CHECK( threw );
  }
}

TEST_CASE( "probeRemote refuses local paths — it is a remote-only contract",
           "[io][remote][range]" )
{
  bool threw = false;
  try
  {
    probeRemote( "C:/definitely/local/file.tif" );
  }
  catch ( const GeoError &error )
  {
    threw = true;
    CHECK( error.code() == ErrorCode::InvalidArgument );
  }
  CHECK( threw );
}
