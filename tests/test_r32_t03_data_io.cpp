/***************************************************************************
  tests/test_r32_t03_data_io.cpp — R32-T03 data/remote-IO/VRT reliability
  regressions over real fixtures (no mocks):
    * #2117 EO spatial slices keep their full-grid origin through
      execute/prefetch/mirror chunk extents (row/col-encoded oracle)
    * #2118 the range cache classifies object-store VSI payloads like
      Stat/Read under the private-network policy (loopback S3 fixture)
    * #2119 an oversized HTTP error page is its STATUS, never a success
      (strict/status double entry contract on the guarded transport)
    * #2120 VectorWriter takes ownership from dataset creation — typed
      field refusal leaves no staged/locked residue — and round-trips
      Date/Time/DateTime values through real GPKG
    * #2121 remote nested subdatasets classify remote, keep working GDAL
      spellings, never leak credentials into inventory/identity JSON
 ***************************************************************************/

#include "geospatial/fabric/chunk_plan.h"
#include "geospatial/fabric/object_store.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/fabric/virtual_cube.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/identity/asset_identity.h"
#include "geospatial/io/subdataset_inventory.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/remote/http_fetch.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"
#include "support/http_range_server.h"
#include "support/http_s3_server.h"
#include "support/http_stac_server.h"

#include <catch2/catch_test_macros.hpp>

#include <cpl_vsi.h>
#include <gdal.h>
#include <ogr_core.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <netcdf.h>

using namespace sicnu::geo;
using namespace sicnu::geo::io;   // inventorySubdatasets / SubdatasetInventory
using sicnu::geo::testsupport::HttpRangeServer;
using sicnu::geo::testsupport::HttpS3Server;
using sicnu::geo::testsupport::HttpStacServer;
using sicnu::geo::testsupport::StacRoute;

namespace
{

std::string scratchDir( const char *name )
{
  const std::string path = ( std::filesystem::temp_directory_path() / "sicnu_r32_t03" /
                             std::string( name ) )
                             .string();
  std::filesystem::remove_all( path );
  std::filesystem::create_directories( path );
  return path;
}

AssetRecord sceneRecord( const std::string &id, const std::string &path, const std::string &instant )
{
  AssetRecord record;
  record.id = id;
  record.path = path;
  record.datetimeUtc = instant;
  record.hasBbox = true;
  record.minX = 0.0;
  record.minY = 0.0;
  record.maxX = 64.0;
  record.maxY = 64.0;
  record.mediaType = "image/tiff";
  record.roles = { "data" };
  return record;
}

/// Row/col-encoded raster: value(row, col) = row * 64 + col — every pixel
/// read back can prove WHICH source pixel it came from (#2117 oracle).
void writeEncodedScene( const std::string &path )
{
  RasterWriter writer = RasterWriter::create(
    path, 64, 64, { RasterBandSpec {} },
    { "GTiff", { "TILED=YES", "BLOCKXSIZE=16", "BLOCKYSIZE=16" }, true } );
  writer.setCrs( Crs::fromAuthid( "EPSG:4326" ) );
  writer.setGeotransform( { 0.0, 1.0, 0.0, 64.0, 0.0, -1.0 } );
  std::vector<double> raster( 64ull * 64 );
  for ( int row = 0; row < 64; ++row )
    for ( int col = 0; col < 64; ++col )
      raster[static_cast<std::size_t>( row ) * 64 + col] = row * 64.0 + col;
  writer.writeWindow( 1, { 0, 0, 64, 64 }, raster.data() );
  writer.finalize();
}

bool netCdfAuthoringAvailable()
{
  static bool available = [] {
    GDALAllRegister();
    return GDALGetDriverByName( "netCDF" ) != nullptr;
  }();
  return available;
}

/// Parses a GDAL date/time string back into Y/M/D h:m:s (+tz minutes).
bool parseDateTimeFields( const std::string &text, int &year, int &month, int &day, int &hour,
                          int &minute, int &second, int &tzFlag )
{
  OGRField field;
  std::memset( &field, 0, sizeof( field ) );
  if ( OGRParseDate( text.c_str(), &field, 0 ) == 0 )
    return false;
  year = field.Date.Year;
  month = field.Date.Month;
  day = field.Date.Day;
  hour = field.Date.Hour;
  minute = field.Date.Minute;
  second = field.Date.Second;
  tzFlag = field.Date.TZFlag;
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// #2117 — EO spatial slice origin fidelity
// ---------------------------------------------------------------------------

TEST_CASE( "EO spatial slice chunks carry the full-grid origin (#2117)",
           "[r32t03][eo_slice]" )
{
  const std::string dir = scratchDir( "eo_slice" );
  const std::string scene = dir + "/encoded.tif";
  writeEncodedScene( scene );

  VirtualCubeGrid grid;
  grid.explicitGrid = true;
  grid.crs.valid = true;
  grid.crs.authid = "EPSG:4326";
  grid.scaleX = 1.0;
  grid.scaleY = 1.0;
  grid.minX = 0.0;
  grid.minY = 0.0;
  grid.maxX = 64.0;
  grid.maxY = 64.0;

  std::vector<AssetRecord> assets = { sceneRecord( "s0", scene, "2024-01-01T00:00:00Z" ) };
  const VirtualCube cube = VirtualCube::build( assets, grid, OverlapPolicy::FirstWins, {} );

  CubeChunkShape shape;
  shape.time = 1;
  shape.y = 16;
  shape.x = 16;
  shape.band = 1;

  // Slice [16,48)² → x0=y0=16, size 32 → 2×2 chunks of 16 px.
  CubeSlice slice;
  slice.hasSpatialSlice = true;
  slice.minX = 16.0;
  slice.minY = 16.0;
  slice.maxX = 48.0;
  slice.maxY = 48.0;
  const CubeChunkPlan plan = CubeChunkPlan::forVirtualCube( cube, shape, slice );
  CHECK( plan.chunkCountTotal() == 4 );
  CHECK( plan.spatialSliced() );

  // The plan's chunk extents are FULL-GRID facts: the first chunk covers
  // source pixels x 16..31 / y 16..31, not the origin corner.
  const std::vector<CubeChunkRequest> chunks = plan.materializeChunks( 0, 4 );
  REQUIRE( chunks.size() == 4 );
  REQUIRE( chunks[0].hasExtent );
  CHECK( chunks[0].minX == 16.0 );
  CHECK( chunks[0].maxX == 32.0 );
  CHECK( chunks[0].minY == 32.0 );   // grid.maxY - (16 + 16)
  CHECK( chunks[0].maxY == 48.0 );   // grid.maxY - 16
  REQUIRE( chunks[3].hasExtent );
  CHECK( chunks[3].maxX == 48.0 );
  CHECK( chunks[3].minY == 16.0 );

  // Un-sliced plans keep the historical origin-anchored extents.
  const CubeChunkPlan whole = CubeChunkPlan::forVirtualCube( cube, shape );
  const std::vector<CubeChunkRequest> wholeChunks = whole.materializeChunks( 0, 1 );
  REQUIRE( wholeChunks.size() == 1 );
  CHECK( wholeChunks[0].minX == 0.0 );
  CHECK( wholeChunks[0].maxY == 64.0 );
}

TEST_CASE( "EO slice execution reads the sliced corner's true pixels (#2117)",
           "[r32t03][eo_slice][execute]" )
{
  const std::string dir = scratchDir( "eo_slice_exec" );
  const std::string scene = dir + "/encoded.tif";
  writeEncodedScene( scene );

  FabricIntent intent;
  intent.records = { sceneRecord( "s0", scene, "2024-01-01T00:00:00Z" ) };
  intent.sceneBudget = 4;
  intent.grid.explicitGrid = true;
  intent.grid.crs.valid = true;
  intent.grid.crs.authid = "EPSG:4326";
  intent.grid.scaleX = 1.0;
  intent.grid.scaleY = 1.0;
  intent.grid.minX = 0.0;
  intent.grid.minY = 0.0;
  intent.grid.maxX = 64.0;
  intent.grid.maxY = 64.0;
  intent.chunkShape = { 1, 16, 16, 1 };
  intent.slice.hasSpatialSlice = true;
  intent.slice.minX = 16.0;
  intent.slice.minY = 16.0;
  intent.slice.maxX = 48.0;
  intent.slice.maxY = 48.0;

  const FabricPlan plan = planFabric( intent );
  REQUIRE( plan.chunkPlan().chunkCountTotal() == 4 );

  FabricExecutionReport report;
  std::size_t chunksSeen = 0;
  const std::vector<FabricChunkOutcome> outcomes = executeChunks(
    plan, {}, 8,
    [ & ] ( const CubeChunkRequest &request, const VirtualCubeWindowResult &window ) {
      ++chunksSeen;
      REQUIRE( request.hasExtent );
      REQUIRE( window.width == 16 );
      REQUIRE( window.height == 16 );
      // Every window pixel must decode to the SOURCE row/col its extent
      // claims — first and last pixels of the window pin both corners.
      const int col0 = static_cast<int>( request.minX - plan.grid().minX );
      const int row0 =
        static_cast<int>( ( plan.grid().maxY - request.maxY ) / plan.grid().scaleY );
      CHECK( window.values.front() == row0 * 64.0 + col0 );
      CHECK( window.values.back() == ( row0 + 15 ) * 64.0 + col0 + 15 );
    },
    report );
  CHECK( outcomes.size() == 4 );
  CHECK( report.chunksExecuted == 4 );
  CHECK( report.assetsFailed == 0 );
  REQUIRE( chunksSeen == 4 );
}

// ---------------------------------------------------------------------------
// #2118 — object-store VSI payloads classify like Stat/Read in Open
// ---------------------------------------------------------------------------

TEST_CASE( "object-store opens flow under the private-network policy (#2118)",
           "[r32t03][range_cache][object_store][integration]" )
{
  const std::string dir = scratchDir( "object_policy" );
  const std::vector<unsigned char> payload( 16384, 0x2E );
  HttpS3Server server( "eo-bucket", "scene.bin", payload, "\"etag-policy-1\"" );
  REQUIRE( server.valid() );
  REQUIRE( server.port() > 0 );

  ObjectStoreCredentials credentials;
  credentials.accessKeyId = "loopback";
  credentials.secretAccessKey = "loopback-secret";
  credentials.endpoint = server.endpoint();

  RangeCacheConfig config;
  config.blockPrivateNetworks = true;   // the production default posture
  RemoteRangeCache::install( config );
  struct CacheGuard
  {
    ~CacheGuard() { RemoteRangeCache::uninstall(); }
  } cacheGuard;

  // Open + Read + Stat through the wrapped object spelling: the policy
  // must classify the payload (vsiObject), not resolve the bucket name as
  // an http host.
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    const std::string cached = fabricCachedPath( "/vsis3/eo-bucket/scene.bin" );
    CHECK( cached == "/vsirangecache//vsis3/eo-bucket/scene.bin" );

    VSILFILE *handle = VSIFOpenL( cached.c_str(), "rb" );
    REQUIRE( handle != nullptr );
    std::vector<unsigned char> buffer( 4096 );
    const std::size_t got = VSIFReadL( buffer.data(), 1, buffer.size(), handle );
    VSIFCloseL( handle );
    CHECK( got == 4096 );
    CHECK( buffer == std::vector<unsigned char>( 4096, 0x2E ) );

    VSIStatBufL statBuf;
    CHECK( VSIStatL( cached.c_str(), &statBuf ) == 0 );
    CHECK( statBuf.st_size == static_cast<decltype( statBuf.st_size )>( payload.size() ) );
  }

  // The same policy still refuses a REAL private http host — the guard is
  // narrowed to object payloads, never switched off.
  HttpRangeServer plainServer( payload );
  REQUIRE( plainServer.port() > 0 );
  {
    ScopedObjectStoreCredentials window( "/vsis3/", credentials );
    const std::string privateHttp =
      "/vsirangecache/" + plainServer.url();
    CHECK( VSIFOpenL( privateHttp.c_str(), "rb" ) == nullptr );
    VSIStatBufL statBuf;
    CHECK( VSIStatL( privateHttp.c_str(), &statBuf ) != 0 );
  }
}

// ---------------------------------------------------------------------------
// #2119 — oversized error pages classify by status on the guarded transport
// ---------------------------------------------------------------------------

TEST_CASE( "an oversized HTTP error page is its status, never a success (#2119)",
           "[r32t03][http_over_budget]" )
{
  // A route-driven fixture answers every status WITH a huge body — the
  // "oversized error page" shape the budget abort used to misreport.
  const std::string hugePage( 512ull * 1024, 'A' );   // 512 KiB page
  HttpFetchOptions options;
  options.maxResponseBytes = 4096;   // far below the page: the budget must abort
  options.timeoutSeconds = 30;
  // Any guard routes the fetch through the redirect-controlling transport
  // (guardedFetchImpl) — the entry whose budget abort used to bypass the
  // typed error classification.
  options.redirectGuard = []( const std::string & ) { return std::string(); };

  SECTION( "oversized 410 is a typed NotFound for strict entries" )
  {
    HttpStacServer server;
    StacRoute route;
    route.status = 410;
    route.contentType = "text/plain";
    route.body = hugePage;
    server.setRoute( "/gone-huge", route );
    bool threw = false;
    try
    {
      ( void )httpFetch( server.url() + "/gone-huge", options );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      CHECK( error.code() == ErrorCode::NotFound );
    }
    CHECK( threw );
  }

  SECTION( "oversized 500 is a typed failure carrying the status" )
  {
    HttpStacServer server;
    StacRoute route;
    route.status = 500;
    route.contentType = "text/plain";
    route.body = hugePage;
    server.setRoute( "/error-huge", route );
    bool threw = false;
    try
    {
      ( void )httpFetch( server.url() + "/error-huge", options );
    }
    catch ( const GeoError &error )
    {
      threw = true;
      CHECK( error.code() == ErrorCode::NetworkError );
      CHECK( error.details()["status"].asInt() == 500 );
    }
    CHECK( threw );
  }

  SECTION( "httpFetchStatus keeps the original status with a bounded, truncated body" )
  {
    HttpStacServer server;
    StacRoute route;
    route.status = 410;
    route.contentType = "text/plain";
    route.body = hugePage;
    server.setRoute( "/gone-huge", route );
    const HttpFetchResult result = httpFetchStatus( server.url() + "/gone-huge", options );
    CHECK( result.httpStatus == 410 );
    CHECK( result.reachedServer );
    CHECK( result.truncated );
    CHECK( result.sizeGuardHit );
    CHECK( result.body.size() <= 4096 );
  }

  SECTION( "a truncated 2xx answer stays a truthful truncated success" )
  {
    HttpStacServer server;
    StacRoute route;
    route.status = 200;
    route.contentType = "text/plain";
    route.body = hugePage;
    server.setRoute( "/big-200", route );
    const HttpFetchResult result = httpFetchStatus( server.url() + "/big-200", options );
    CHECK( result.httpStatus == 200 );
    CHECK( result.truncated );
    CHECK( result.sizeGuardHit );
    CHECK( result.body.size() <= 4096 );
  }
}

// ---------------------------------------------------------------------------
// #2120 — VectorWriter ownership + Date/Time/DateTime round trip
// ---------------------------------------------------------------------------

TEST_CASE( "a typed field refusal leaves no staged residue (#2120)",
           "[r32t03][vector_writer]" )
{
  const std::string dir = scratchDir( "writer_refusal" );
  const std::string target = dir + "/out.gpkg";

  VectorFieldSpec bad;
  bad.name = "payload";
  bad.typeName = "Binary";   // reader-realistic type the writer must refuse

  bool threw = false;
  try
  {
    ( void )VectorWriter::create( target, "layer", "Point", { bad }, Crs(),
                                  VectorWriteOptions{ "GPKG", {}, true } );
  }
  catch ( const GeoError &error )
  {
    threw = true;
    CHECK( error.code() == ErrorCode::Unsupported );
    CHECK( error.details()["field"].asString() == "payload" );
  }
  REQUIRE( threw );

  // Neither the published target nor the staged group may survive the throw.
  CHECK( !std::filesystem::exists( std::filesystem::u8path( target ) ) );
  const std::string staged = atomic_fs::reservedStagedPathFor( target );
  CHECK( !staged.empty() );
  CHECK( !std::filesystem::exists( std::filesystem::u8path( staged ) ) );
}

TEST_CASE( "Date/Time/DateTime fields round trip through real GPKG (#2120)",
           "[r32t03][vector_writer][gpkg]" )
{
  const std::string dir = scratchDir( "writer_datetime" );
  const std::string target = dir + "/dated.gpkg";

  const std::vector<VectorFieldSpec> fields = {
    { "name", "String", 0, 0 },
    { "acquired", "DateTime", 0, 0 },
    { "at", "Time", 0, 0 },
    { "day", "Date", 0, 0 },
  };
  {
    VectorWriter writer = VectorWriter::create( target, "events", "Point", fields,
                                                Crs::fromAuthid( "EPSG:4326" ),
                                                VectorWriteOptions{ "GPKG", {}, true } );
    Json::Value attributes;
    attributes["name"] = "站点点位";   // Unicode label must survive the trip
    attributes["acquired"] = "2026-11-16T08:30:05+08:00";
    attributes["at"] = "12:34:56";
    attributes["day"] = "2026-11-16";
    writer.writeFeature( attributes, "POINT (120 30)" );

    Json::Value nulls;
    nulls["name"] = "unset";
    nulls["acquired"] = Json::Value();   // null datetime stays null
    nulls["at"] = Json::Value();
    nulls["day"] = Json::Value();
    writer.writeFeature( nulls, "POINT (121 31)" );
    writer.finalize();
  }

  {
    VectorReader reader = VectorReader::open( target );
    REQUIRE( reader.isOpen() );
    const std::vector<FieldInfo> &schema = reader.layerInfo().fields;
    REQUIRE( schema.size() == 4 );
    CHECK( schema[1].typeName == "DateTime" );
    CHECK( schema[3].typeName == "Date" );
    // GDAL 3.13's GPKG driver maps an OFTTime column back to OFTString on
    // reopen (driver capability, not a writer defect) — the VALUE round
    // trip below is the fidelity oracle.
    CHECK( ( schema[2].typeName == "Time" || schema[2].typeName == "String" ) );

    std::vector<VectorFeature> batch;
    REQUIRE( reader.nextBatch( batch, 16 ) );
    REQUIRE( batch.size() == 2 );

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, tzFlag = 0;
    REQUIRE( parseDateTimeFields( batch[0].attributes["acquired"].asString(), year, month, day,
                                  hour, minute, second, tzFlag ) );
    CHECK( year == 2026 );
    CHECK( month == 11 );
    CHECK( day == 16 );
    CHECK( hour == 8 );
    CHECK( minute == 30 );
    CHECK( second == 5 );
    CHECK( tzFlag != 0 );   // the +08:00 offset survived, not flattened to UTC-unknown

    REQUIRE( parseDateTimeFields( batch[0].attributes["at"].asString(), year, month, day, hour,
                                  minute, second, tzFlag ) );
    CHECK( hour == 12 );
    CHECK( minute == 34 );
    CHECK( second == 56 );

    CHECK( batch[0].attributes["name"].asString() == "站点点位" );
    CHECK( batch[1].attributes["acquired"].isNull() );
    CHECK( batch[1].attributes["at"].isNull() );
  }

  // The conversion trigger chain survives DateTime schemas end to end.
  const std::string converted = dir + "/converted.gpkg";
  const Json::Value result =
    vectorConvert( target, converted, "GPKG", "events", "", "", {}, nullptr );
  CHECK( result["feature_count"].asInt64() == 2 );

  // An unparseable datetime string is a typed refusal, not a silent zero.
  const std::string bad = dir + "/bad.gpkg";
  bool refused = false;
  {
    VectorWriter writer = VectorWriter::create( bad, "events", "Point", fields, Crs(),
                                                VectorWriteOptions{ "GPKG", {}, true } );
    Json::Value attributes;
    attributes["name"] = "broken";
    attributes["acquired"] = "not-a-date";
    attributes["at"] = "00:00:00";
    attributes["day"] = "2026-11-16";
    try
    {
      writer.writeFeature( attributes, "POINT (1 1)" );
    }
    catch ( const GeoError &error )
    {
      refused = true;
      CHECK( error.code() == ErrorCode::InvalidArgument );
      CHECK( error.details()["field"].asString() == "acquired" );
    }
    CHECK( refused );
    writer.cancel();
  }
  CHECK( !std::filesystem::exists( std::filesystem::u8path( bad ) ) );
}

// ---------------------------------------------------------------------------
// #2121 — remote nested subdatasets: classification, identity, no leakage
// ---------------------------------------------------------------------------

TEST_CASE( "remote nested subdatasets classify remote and keep local ones local (#2121)",
           "[r32t03][subdataset_uri]" )
{
  const std::string dir = scratchDir( "sds_local" );
  const ResourceUri remote = ResourceUri::parse(
    R"(NETCDF:"/vsicurl/https://user:sekrit@data.example.org/a.nc":temp)" );
  CHECK( remote.kind == ResourceKind::Subdataset );
  CHECK( remote.embeddedLocalPath().empty() );
  CHECK( !remote.isLocalPayload() );

  const ResourceUri nestedObject = ResourceUri::parse(
    R"(NETCDF:"/vsis3/bucket/a.nc":temp)" );
  CHECK( nestedObject.embeddedLocalPath().empty() );
  CHECK( !nestedObject.isLocalPayload() );

  const ResourceUri plainUrl = ResourceUri::parse( R"(NETCDF:"https://h.org/a.nc":temp)" );
  CHECK( plainUrl.embeddedLocalPath().empty() );

  // Nested virtual wrappers strip ITERATIVELY: the prefix strip keeps the
  // payload's own '/', so a single parse would see "//vsicurl/…" as a
  // UNC-style local path (the R1 finding).
  const ResourceUri vsizipRemote = ResourceUri::parse(
    "/vsizip//vsicurl/https://user:sekrit@h.org/a.zip" );
  CHECK( vsizipRemote.kind == ResourceKind::VsiVirtual );
  CHECK( vsizipRemote.embeddedLocalPath().empty() );
  CHECK( !vsizipRemote.isLocalPayload() );

  const ResourceUri nestedSubdataset = ResourceUri::parse(
    R"(NETCDF:"/vsizip//vsicurl/https://user:sekrit@h.org/a.zip":temp)" );
  CHECK( nestedSubdataset.kind == ResourceKind::Subdataset );
  CHECK( nestedSubdataset.embeddedLocalPath().empty() );

  // Local virtual containers stay local payloads.
  const std::string localZip = dir + "/local.zip";
  { std::ofstream out( std::filesystem::u8path( localZip ) ); }
  const ResourceUri vsizipLocal = ResourceUri::parse( "/vsizip/" + localZip );
  CHECK( vsizipLocal.kind == ResourceKind::VsiVirtual );
  CHECK( !vsizipLocal.embeddedLocalPath().empty() );

  const std::string localPath = dir + "/a.nc";
  { std::ofstream out( std::filesystem::u8path( localPath ) ); }
  const ResourceUri local = ResourceUri::parse( "NETCDF:\"" + localPath + "\":temp" );
  CHECK( local.kind == ResourceKind::Subdataset );
  CHECK( local.embeddedLocalPath() == localPath );
  CHECK( local.isLocalPayload() );

  // Credentials never reach the display form; the identity-bearing
  // canonical keeps the original spelling (GDAL can still open it).
  const std::string display = remote.display();
  CHECK( display.find( "sekrit" ) == std::string::npos );
  CHECK( remote.gdalOpenTarget().find( "sekrit" ) != std::string::npos );
}

TEST_CASE( "remote subdataset identity is provable, selector-split and drift-aware (#2121)",
           "[r32t03][subdataset_identity][integration]" )
{
  const std::vector<unsigned char> payload( 4096, 0x5A );
  HttpRangeServer server( payload );
  server.setEtag( "\"sds-etag-1\"" );
  server.setLastModified( "Mon, 16 Nov 2026 08:00:00 GMT" );

  // The loopback fixture is the authorized origin for this test
  // (AssetIdentityOptions defaults keep private-network probing off).
  const std::string containerUrl = server.url();   // http://127.0.0.1:<p>/fixture.tif
  const std::string withSecret = "NETCDF:\"/vsicurl/" + containerUrl + "\":temp";

  const AssetIdentity identityA = assetIdentityToken( withSecret );
  CHECK( identityA.provable() );
  CHECK( identityA.resourceKind == "subdataset" );
  CHECK( identityA.token.rfind( "sd1:v1:", 0 ) == 0 );

  // Selector separation: same container, different variable ⇒ different id.
  const AssetIdentity identityB =
    assetIdentityToken( "NETCDF:\"/vsicurl/" + containerUrl + "\":sst" );
  CHECK( identityB.provable() );
  CHECK( identityA.token != identityB.token );

  // Container drift (new ETag) ⇒ the identity token follows.
  server.replacePayload( payload, "\"sds-etag-2\"", "Mon, 16 Nov 2026 09:00:00 GMT" );
  const AssetIdentity identityA2 = assetIdentityToken( withSecret );
  CHECK( identityA2.provable() );
  CHECK( identityA2.token != identityA.token );

  // Identity JSON never carries the URL (hash-only qualifier).
  const std::string identityJson = identityA.toJson().toStyledString();
  CHECK( identityJson.find( "127.0.0.1" ) == std::string::npos );
}

TEST_CASE( "remote subdataset inventory never leaks the raw source (#2121)",
           "[r32t03][subdataset_inventory][netcdf][integration]" )
{
  if ( !netCdfAuthoringAvailable() )
  {
    WARN( "netCDF driver not available; remote inventory proof skipped" );
    return;
  }

  // Author a two-variable netCDF, then serve its bytes over loopback HTTP.
  const std::string dir = scratchDir( "sds_remote" );
  const std::string ncPath = dir + "/cube.nc";
  int ncid = -1;
  REQUIRE( nc_create( ncPath.c_str(), NC_CLOBBER, &ncid ) == NC_NOERR );
  int yId = -1, xId = -1;
  REQUIRE( nc_def_dim( ncid, "y", 3, &yId ) == NC_NOERR );
  REQUIRE( nc_def_dim( ncid, "x", 4, &xId ) == NC_NOERR );
  int dims[2] = { yId, xId };
  int sstId = -1, tempId = -1;
  REQUIRE( nc_def_var( ncid, "sst", NC_FLOAT, 2, dims, &sstId ) == NC_NOERR );
  REQUIRE( nc_def_var( ncid, "temp", NC_FLOAT, 2, dims, &tempId ) == NC_NOERR );
  REQUIRE( nc_enddef( ncid ) == NC_NOERR );
  std::vector<float> cells( 12, 1.0f );
  REQUIRE( nc_put_var_float( ncid, sstId, cells.data() ) == NC_NOERR );
  REQUIRE( nc_put_var_float( ncid, tempId, cells.data() ) == NC_NOERR );
  REQUIRE( nc_close( ncid ) == NC_NOERR );

  std::vector<unsigned char> bytes( std::filesystem::file_size( std::filesystem::u8path( ncPath ) ) );
  {
    std::ifstream in( std::filesystem::u8path( ncPath ), std::ios::binary );
    in.read( reinterpret_cast<char *>( bytes.data() ), static_cast<std::streamsize>( bytes.size() ) );
  }
  HttpRangeServer server( bytes );
  REQUIRE( server.port() > 0 );

  const std::string url = server.url();
  const std::string withUserinfo =
    "NETCDF:\"/vsicurl/" + url.substr( 0, 7 ) + "user:sekrit@" + url.substr( 7 ) + "\"";

  const SubdatasetInventory inventory = inventorySubdatasets( withUserinfo );
  REQUIRE( inventory.entries.size() >= 2 );

  const Json::Value json = inventory.toJson();
  const std::string serialized = json.toStyledString();
  // The raw connection string (credentials included) never crosses the JSON
  // boundary; the redacted display form carries the readable source.
  CHECK( serialized.find( "sekrit" ) == std::string::npos );
  CHECK( !json.isMember( "source" ) );
  CHECK( json.isMember( "source_display" ) );
  for ( const Json::Value &entry : json["entries"] )
    CHECK( !entry.isMember( "name" ) );   // remote SDS names embed the credential URL

  // The GDAL-openable spelling stays intact for the open path.
  CHECK( inventory.source == withUserinfo );
}
