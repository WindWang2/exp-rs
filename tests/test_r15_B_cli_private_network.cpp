// tests/test_r15_B_cli_private_network.cpp — R15-B #1505 T11: the production
// CLI remote entry points block private-network origins by default.
//
// Before the fix `data cache <url>` and `data cache prefetch` installed the
// range cache with RangeCacheConfig{} (blockPrivateNetworks=false) and
// `data identity <url>` probed with RemoteValidatorOptions{} (same default),
// so a user- or catalog-supplied URL naming loopback / RFC 1918 /
// link-local / metadata hosts was fetched. The fixture listens on
// 127.0.0.1: a refusal is proven by the fixture receiving ZERO requests.
// `--allow-private-network` is the explicit, per-invocation opt-in.
// `data cache prefetch` is driven over a one-item local STAC tree whose
// asset href is the loopback fixture.
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "support/http_range_server.h"
#include "support/offline_probe.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <vector>

#ifndef SICNU_TEST_CLI
#error "SICNU_TEST_CLI must point at sicnu_geo_rs_cli"
#endif

namespace
{
struct RunResult
{
  int exitCode = -1;
  std::string output;
};

RunResult runCli( const std::string &args )
{
  const std::string command = std::string( SICNU_TEST_CLI ) + " " + args + " 2>/dev/null";
  FILE *pipe = ::popen( command.c_str(), "r" );
  REQUIRE( pipe != nullptr );
  char buffer[4096];
  size_t read = 0;
  RunResult result;
  while ( ( read = fread( buffer, 1, sizeof( buffer ), pipe ) ) > 0 )
    result.output.append( buffer, read );
  const int status = ::pclose( pipe );
  result.exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
  return result;
}

Json::Value parseEnvelope( const std::string &text )
{
  Json::Value root;
  Json::Reader reader;
  reader.parse( text, root, false );
  return root;
}
} // namespace

namespace
{
/// A one-item STAC tree (item bbox 0..32 in EPSG:4326) whose only asset is
/// \a href, plus a prefetch intent over it. Returns the spec path.
std::string writePrefetchFixture( const std::string &href )
{
  std::string base = std::string( std::getenv( "TMPDIR" ) ? std::getenv( "TMPDIR" ) : "/tmp" ) +
                     "/r15b_t11_prefetch_XXXXXX";
  std::vector<char> templ( base.begin(), base.end() );
  templ.push_back( '\0' );
  REQUIRE( ::mkdtemp( templ.data() ) != nullptr );
  const std::string dir( templ.data() );
  std::ofstream( dir + "/item.json", std::ios::binary )
    << R"({"type":"Feature","stac_version":"1.0.0","id":"t11","bbox":[0,0,32,32],)"
    << R"("geometry":{"type":"Polygon","coordinates":[[[0,0],[32,0],[32,32],[0,32],[0,0]]]},)"
    << R"("properties":{"datetime":"2024-05-01T00:00:00Z"},)"
    << R"("assets":{"image":{"href":")" << href << R"(","type":"image/tiff","roles":["data"]}}})";
  const std::string spec = dir + "/spec.json";
  std::ofstream( spec, std::ios::binary )
    << R"({"catalog":")" << dir << R"(","grid":{"crs":"EPSG:4326","scaleX":1.0,"scaleY":1.0,"extent":[0,0,32,32]},)"
    << R"("chunkShape":{"time":1,"y":16,"x":16,"band":1}})";
  return spec;
}
} // namespace

SICNU_OFFLINE_GUARD()

TEST_CASE( "1505-T11: data cache <url> refuses a loopback origin by default",
           "[r15b][post1505][cli][ssrf]" )
{
  sicnu::geo::testsupport::HttpRangeServer server( std::vector<unsigned char>( 8192, 0x22 ) );
  server.setEtag( "\"r15b-t11-cache\"" );
  const auto result = runCli( "data cache --json " + server.url() + " --bytes 2048" );
  INFO( result.output );
  CHECK( result.exitCode != 0 );
  CHECK_FALSE( parseEnvelope( result.output ).get( "ok", true ).asBool() );
  CHECK( server.requestCount() == 0 );
}

TEST_CASE( "1505-T11: data identity <url> refuses a loopback origin by default",
           "[r15b][post1505][cli][ssrf]" )
{
  sicnu::geo::testsupport::HttpRangeServer server( std::vector<unsigned char>( 4096, 0x11 ) );
  server.setEtag( "\"r15b-t11-identity\"" );
  const auto result = runCli( "data identity --json " + server.url() );
  INFO( result.output );
  CHECK( result.exitCode != 0 );
  CHECK_FALSE( parseEnvelope( result.output ).get( "ok", true ).asBool() );
  CHECK( server.requestCount() == 0 );
}

TEST_CASE( "1505-T11: data cache prefetch never fetches a loopback asset by default",
           "[r15b][post1505][cli][ssrf]" )
{
  sicnu::geo::testsupport::HttpRangeServer server( std::vector<unsigned char>( 8192, 0x44 ) );
  server.setEtag( "\"r15b-t11-prefetch\"" );
  const std::string spec = writePrefetchFixture( server.url() );
  const auto guarded = runCli( "data cache prefetch " + spec + " --json" );
  INFO( guarded.output );
  CHECK( server.requestCount() == 0 );
  // Positive control (proves the case is not vacuous): the same spec with
  // the explicit opt-in does reach the fixture.
  const auto optIn = runCli( "data cache prefetch " + spec + " --allow-private-network --json" );
  INFO( optIn.output );
  CHECK( server.requestCount() > 0 );
  std::error_code ec;
  std::filesystem::remove_all( std::filesystem::path( spec ).parent_path(), ec );
}

TEST_CASE( "1505-T11: --allow-private-network is the explicit opt-in",
           "[r15b][post1505][cli][ssrf]" )
{
  sicnu::geo::testsupport::HttpRangeServer server( std::vector<unsigned char>( 8192, 0x22 ) );
  server.setEtag( "\"r15b-t11-optin\"" );
  const auto result =
    runCli( "data cache --json " + server.url() + " --bytes 2048 --allow-private-network" );
  INFO( result.output );
  REQUIRE( result.exitCode == 0 );
  const Json::Value envelope = parseEnvelope( result.output );
  REQUIRE( envelope.get( "ok", false ).asBool() );
  CHECK( envelope["data"].get( "bytes_read", Json::Value( 0 ) ).asUInt64() == 2048 );
  CHECK_FALSE( envelope["data"]["config"].get( "block_private_networks", true ).asBool() );
  CHECK( server.requestCount() > 0 );
}

// ---------------------------------------------------------------------------
// R4-03 (round 5): remote paths OUTSIDE the range cache — plain GDAL
// /vsicurl/ and CPLHTTPFetch reads behind data inspect / doctor / probe /
// window / mirror materialize, and the library-default identity / locality
// / doctor probes. The CLI entry engages a process-wide private-network
// policy (default on; --allow-private-network anywhere on the command line
// opts out for that run). Each case proves the loopback fixture is NEVER
// contacted; the positive control proves the oracle is not vacuous.
// ---------------------------------------------------------------------------
namespace
{
int requestsFor( const std::string &argsWithUrlPlaceholder, const std::string &suffix = "" )
{
  sicnu::geo::testsupport::HttpRangeServer server( std::vector<unsigned char>( 8192, 0x47 ) );
  server.setEtag( "\"r15b-r403\"" );
  std::string args = argsWithUrlPlaceholder;
  const std::string marker = "@URL@";
  const std::size_t at = args.find( marker );
  REQUIRE( at != std::string::npos );
  args.replace( at, marker.size(), server.url() );
  const auto result = runCli( args + suffix );
  INFO( args << suffix << " -> exit " << result.exitCode << ": " << result.output.substr( 0, 400 ) );
  return server.requestCount();
}
} // namespace

TEST_CASE( "R4-03: data inspect/doctor/probe never contact a loopback origin by default",
           "[r15b][r403][cli][ssrf]" )
{
  CHECK( requestsFor( "data inspect --json @URL@" ) == 0 );
  CHECK( requestsFor( "data inspect --json /vsicurl/@URL@" ) == 0 );
  CHECK( requestsFor( "data doctor --json @URL@" ) == 0 );
  CHECK( requestsFor( "data probe --json @URL@" ) == 0 );
}

TEST_CASE( "R4-03: data cube window / mirror materialize never contact a loopback asset by default",
           "[r15b][r403][cli][ssrf]" )
{
  for ( const std::string verb : { std::string( "cube window" ), std::string( "mirror materialize" ) } )
  {
    for ( const bool optIn : { false, true } )
    {
      sicnu::geo::testsupport::HttpRangeServer server( std::vector<unsigned char>( 8192, 0x48 ) );
      const std::string spec = writePrefetchFixture( server.url() );
      const std::string out = std::filesystem::path( spec ).parent_path().string() +
                              ( verb == "cube window" ? "/out.tif" : "/mirror" );
      const auto result = runCli( "data " + verb + " " + spec + " -o " + out + " --json" +
                                  ( optIn ? " --allow-private-network" : "" ) );
      INFO( verb << ( optIn ? " (opt-in)" : "" ) << " -> exit " << result.exitCode << ": "
                 << result.output.substr( 0, 400 ) );
      if ( optIn )
        CHECK( server.requestCount() > 0 ); // positive control: the case is not vacuous
      else
        CHECK( server.requestCount() == 0 );
      std::error_code ec;
      std::filesystem::remove_all( std::filesystem::path( spec ).parent_path(), ec );
    }
  }
}

TEST_CASE( "R4-03: --allow-private-network anywhere opts the whole run out (positive control)",
           "[r15b][r403][cli][ssrf]" )
{
  CHECK( requestsFor( "data inspect --json @URL@", " --allow-private-network" ) > 0 );
  CHECK( requestsFor( "--allow-private-network data probe --json @URL@" ) > 0 );
}
