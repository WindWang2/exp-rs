/***************************************************************************
 * tests/test_adversarial_m3.cpp
 * Adversarial Empirical Stress Suite for Milestone 3
 * Issues: #776, #790, #791, #807, #808, #809, #810, #816
 ***************************************************************************/

#include "geospatial/cog/cog_validator.h"
#include "geospatial/probe/probe.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/util/resource_uri.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace sicnu::geo;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_adv_m3" / name;
  std::error_code ec;
  fs::remove_all( dir, ec );
  fs::create_directories( dir );
  return dir.string();
}

std::string makeSyntheticTiff( const std::string &path, int width = 128, int height = 128, double nodata = -9999.0 )
{
  RasterBandSpec bspec;
  bspec.dtype = "Float32";
  bspec.hasNoData = true;
  bspec.noDataValue = nodata;

  RasterWriter writer = RasterWriter::create( path, width, height, { bspec } );
  std::vector<double> data( static_cast<std::size_t>( width ) * height );
  for ( std::size_t i = 0; i < data.size(); ++i )
    data[i] = static_cast<double>( i + 1 );

  RasterWindow win;
  win.xOff = 0;
  win.yOff = 0;
  win.width = width;
  win.height = height;
  writer.writeWindow( 1, win, data.data() );
  writer.finalize();
  return path;
}
} // namespace

TEST_CASE( "Adversarial M3 - #776 & #810: ResourceUri aggressive credential redaction",
           "[adv][m3][issue-776][issue-810]" )
{
  SECTION( "Redacts diverse authentication schemes" )
  {
    // Token without colon
    const ResourceUri uri1 = ResourceUri::parse( "s3://SUPER_SECRET_TOKEN@bucket/key/raster.tif" );
    CHECK( uri1.display().find( "SUPER_SECRET_TOKEN" ) == std::string::npos );
    CHECK( uri1.display().find( "***@" ) != std::string::npos );

    // User:password pattern
    const ResourceUri uri2 = ResourceUri::parse( "postgres://admin:topsecret123@db.example.internal:5432/spatialdb" );
    CHECK( uri2.display().find( "topsecret123" ) == std::string::npos );
    CHECK( uri2.display().find( "admin:***@" ) != std::string::npos );

    // HTTP bearer / access credentials
    const ResourceUri uri3 = ResourceUri::parse( "https://user:pass@imagery.space.gov/scene1.tif" );
    CHECK( uri3.display().find( "pass" ) == std::string::npos );
    CHECK( uri3.display().find( "user:***@" ) != std::string::npos );
  }

  SECTION( "Redacts all sensitive query parameters" )
  {
    const std::string url = "https://cdn.example.org/download.tif?normal_param=foo&auth=secret1&bearer=secret2&access_key=secret3&authorization=secret4&token=secret5&sig=secret6";
    const ResourceUri uri = ResourceUri::parse( url );
    const std::string display = uri.display();

    CHECK( display.find( "normal_param=foo" ) != std::string::npos );
    CHECK( display.find( "secret1" ) == std::string::npos );
    CHECK( display.find( "secret2" ) == std::string::npos );
    CHECK( display.find( "secret3" ) == std::string::npos );
    CHECK( display.find( "secret4" ) == std::string::npos );
    CHECK( display.find( "secret5" ) == std::string::npos );
    CHECK( display.find( "secret6" ) == std::string::npos );
    CHECK( display.find( "auth=***" ) != std::string::npos );
    CHECK( display.find( "bearer=***" ) != std::string::npos );
    CHECK( display.find( "access_key=***" ) != std::string::npos );
    CHECK( display.find( "authorization=***" ) != std::string::npos );
  }

  SECTION( "Safe path traversal and drive preservation" )
  {
    // Escaping traversal outside base directory is refused as Invalid
    const ResourceUri escape = ResourceUri::resolveAgainst( "/var/data/catalog", "../satellite/scene.tif" );
    CHECK( escape.kind == ResourceKind::Invalid );
    CHECK( escape.parseReason.find( "escape" ) != std::string::npos );

    // Internal traversal inside base resolves correctly
    const ResourceUri inner = ResourceUri::resolveAgainst( "/opt/imagery", "dir/../sub/tile.tif" );
    CHECK( inner.canonical() == "/opt/imagery/sub/tile.tif" );
  }
}

TEST_CASE( "Adversarial M3 - #790, #808 & #816: RasterReader contracts, tile progression & memory limits",
           "[adv][m3][issue-790][issue-808][issue-816]" )
{
  const std::string dir = scratch( "raster_adv" );
  const std::string path = dir + "/sample.tif";
  makeSyntheticTiff( path, 100, 100, -9999.0 );

  RasterReader reader = RasterReader::open( path );
  REQUIRE( reader.metadata().width == 100 );
  REQUIRE( reader.metadata().height == 100 );

  SECTION( "#808: Strict byte budget enforcement" )
  {
    // Normal window within budget: 50x50 Float64 = 20,000 bytes.
    // Setting maxBytes to 5,000 must throw CapacityExceeded.
    RasterWindow win;
    win.xOff = 0;
    win.yOff = 0;
    win.width = 50;
    win.height = 50;
    CHECK_THROWS_AS( reader.readWindow( { 1 }, win, 5000 ), GeoError );

    // Setting maxBytes to 50,000 must succeed.
    CHECK_NOTHROW( reader.readWindow( { 1 }, win, 50000 ) );
  }

  SECTION( "#790: Boundary padding in readBlock" )
  {
    const auto bSize = reader.blockSize( 1 );
    REQUIRE( bSize.first > 0 );
    REQUIRE( bSize.second > 0 );

    // Reading block (0, 0)
    std::vector<double> blockData;
    CHECK_NOTHROW( blockData = reader.readBlock( 1, 0, 0 ) );
    CHECK( blockData.size() == static_cast<std::size_t>( bSize.first * bSize.second ) );
  }

  SECTION( "#816: iterateTiles covers full extent with exact clamping" )
  {
    RasterWindow full;
    full.xOff = 0;
    full.yOff = 0;
    full.width = 100;
    full.height = 100;

    const auto plan = planTileWalk( reader.metadata(), full, 32, 32 );
    int tileCount = 0;
    std::size_t totalPixels = 0;

    reader.iterateTiles( plan, { 1 }, [&]( const TileSlice &slice, const std::vector<double> &data ) {
      ++tileCount;
      CHECK( slice.width <= 32 );
      CHECK( slice.height <= 32 );
      CHECK( data.size() == static_cast<std::size_t>( slice.width * slice.height ) );
      totalPixels += data.size();
    });

    CHECK( tileCount == 16 ); // 4x4 tiles for 100x100 with 32x32 step
    CHECK( totalPixels == 10000 );
  }
}

TEST_CASE( "Adversarial M3 - #791 & #807: AtomicFs failure handling & rollback guarantees",
           "[adv][m3][issue-791][issue-807]" )
{
  const std::string dir = scratch( "atomic_adv" );
  const std::string target = dir + "/production_model.tif";
  const std::string staged = dir + "/staged_model.tif.tmp";

  // Create initial production file
  {
    std::ofstream out( target );
    out << "ORIGINAL_PRODUCTION_CONTENT_V1";
  }
  REQUIRE( fs::exists( target ) );

  // Create staged file
  {
    std::ofstream out( staged );
    out << "NEW_STAGED_CONTENT_V2";
  }

  SECTION( "publishStagedFile replaces content cleanly" )
  {
    atomic_fs::publishStagedFile( staged, target );
    CHECK( fs::exists( target ) );
    CHECK( !fs::exists( staged ) );

    std::ifstream in( target );
    std::string content;
    in >> content;
    CHECK( content == "NEW_STAGED_CONTENT_V2" );
  }

  SECTION( "#807: Multi-file group rollback restores existing targets on mid-stream failure" )
  {
    // Re-create initial production state
    {
      std::ofstream out( target );
      out << "ORIGINAL_PRODUCTION_CONTENT_V1";
    }

    const std::string targetMain = dir + "/prod_group.tif";
    const std::string stagedMain = dir + "/staged_group.tif";
    {
      std::ofstream out( targetMain );
      out << "ORIGINAL_MAIN_CONTENT";
    }
    {
      std::ofstream out( stagedMain );
      out << "NEW_MAIN_CONTENT";
    }

    // Publish group with backup preservation
    atomic_fs::publishStagedGroup( stagedMain, targetMain );
    CHECK( fs::exists( targetMain ) );
    std::ifstream in( targetMain );
    std::string content;
    in >> content;
    CHECK( content == "NEW_MAIN_CONTENT" );
  }
}

TEST_CASE( "Adversarial M3 - #809: Probe structural classification & resilient fallback",
           "[adv][m3][issue-809]" )
{
  const std::string dir = scratch( "probe_adv" );
  const std::string tifPath = dir + "/real_raster.tif";
  const std::string disguisedPath = dir + "/disguised_raster.dat";

  makeSyntheticTiff( tifPath, 64, 64 );
  fs::copy_file( tifPath, disguisedPath );

  SECTION( "Identifies GeoTIFF even with .dat extension" )
  {
    ProbeOptions opts;
    opts.includeSignature = true;
    ProbeResult res = probeResource( disguisedPath, opts );
    CHECK( res.signature == FileSignature::Tiff );
    CHECK( res.format.driverName == "GTiff" );
  }

  SECTION( "Structural COG vs non-COG detection" )
  {
    // 1024x1024 stripped (untiled) TIFF exceeds tiny-image allowance -> isCog is false
    const std::string largeTif = dir + "/large_untiled.tif";
    makeSyntheticTiff( largeTif, 1024, 1024 );
    ProbeResult res = probeResource( largeTif );
    CHECK( res.format.driverName == "GTiff" );
    CHECK( res.isCog == false );
  }
}

namespace
{
void writeBinaryFile( const std::string &path, const std::string &data )
{
  std::ofstream out( path, std::ios::binary | std::ios::trunc );
  REQUIRE( static_cast<bool>( out ) );
  out.write( data.data(), static_cast<std::streamsize>( data.size() ) );
  out.flush();
  REQUIRE( static_cast<bool>( out ) );
}

std::string readBinaryFile( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  REQUIRE( static_cast<bool>( in ) );
  return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}
} // namespace

TEST_CASE( "Adversarial M3 - B06: Comprehensive empirical stress testing of atomic_fs crash journal & recovery",
           "[adv][m3][b06][atomic_fs][journal]" )
{
  // 1. Interrupted publish: simulated crash after 2 sidecars written, before main file is published.
  SECTION( "Scenario 1: Crash after 2 sidecars written restores all original files byte-for-byte" )
  {
    const std::string dir = scratch( "adv_crash_interrupted_publish" );
    const std::string mainPath = dir + "/roads.shp";
    const std::string shxPath = dir + "/roads.shx";
    const std::string dbfPath = dir + "/roads.dbf";
    const std::string prjPath = dir + "/roads.prj";

    const std::string origMain = std::string( "\x00\x01\x02ORIGINAL_SHP_BYTES_\xff\xfe", 24 );
    const std::string origShx = std::string( "\x03\x04\x05ORIGINAL_SHX_BYTES_\xfd\xfc", 24 );
    const std::string origDbf = std::string( "\x06\x07\x08ORIGINAL_DBF_BYTES_\xfb\xfa", 24 );
    const std::string origPrj = std::string( "\x09\x0a\x0bORIGINAL_PRJ_BYTES_\xf9\xf8", 24 );

    writeBinaryFile( mainPath, origMain );
    writeBinaryFile( shxPath, origShx );
    writeBinaryFile( dbfPath, origDbf );
    writeBinaryFile( prjPath, origPrj );

    // Simulate crash after shx and dbf were published with new data, but before prj and shp were published
    const std::string shxBackup = shxPath + ".bak";
    const std::string dbfBackup = dbfPath + ".bak";
    writeBinaryFile( shxBackup, origShx );
    writeBinaryFile( dbfBackup, origDbf );

    writeBinaryFile( shxPath, "NEW_TORN_SHX_BYTES_WHICH_SHOULD_BE_ROLLED_BACK" );
    writeBinaryFile( dbfPath, "NEW_TORN_DBF_BYTES_WHICH_SHOULD_BE_ROLLED_BACK" );

    // Prepare journal
    const std::string journalPath = mainPath + ".publish-journal";
    const std::string journalJson =
      "{\n"
      "  \"version\": 1,\n"
      "  \"main\": \"" + mainPath + "\",\n"
      "  \"members\": [\n"
      "    { \"staged\": \"staged.shx\", \"target\": \"" + shxPath + "\", \"backup\": \"" + shxBackup + "\", \"had_target\": true },\n"
      "    { \"staged\": \"staged.dbf\", \"target\": \"" + dbfPath + "\", \"backup\": \"" + dbfBackup + "\", \"had_target\": true },\n"
      "    { \"staged\": \"staged.prj\", \"target\": \"" + prjPath + "\", \"backup\": \"" + prjPath + ".bak\", \"had_target\": true },\n"
      "    { \"staged\": \"staged.shp\", \"target\": \"" + mainPath + "\", \"backup\": \"" + mainPath + ".bak\", \"had_target\": true }\n"
      "  ]\n"
      "}\n";
    writeBinaryFile( journalPath, journalJson );

    REQUIRE( atomic_fs::hasPendingJournal( mainPath ) );

    // Execute recovery
    const bool recovered = atomic_fs::recoverDatasetGroup( mainPath );
    CHECK( recovered );
    CHECK_FALSE( atomic_fs::hasPendingJournal( mainPath ) );
    CHECK_FALSE( fs::exists( journalPath ) );
    CHECK_FALSE( fs::exists( shxBackup ) );
    CHECK_FALSE( fs::exists( dbfBackup ) );

    // Check byte-for-byte fidelity across ALL members
    CHECK( readBinaryFile( mainPath ) == origMain );
    CHECK( readBinaryFile( shxPath ) == origShx );
    CHECK( readBinaryFile( dbfPath ) == origDbf );
    CHECK( readBinaryFile( prjPath ) == origPrj );

    // Also verify recovery on brand-new dataset (had_target == false):
    // Staged files were written to targets, crash happened, recovery cleans up torn new targets
    const std::string newMainPath = dir + "/brand_new.shp";
    const std::string newShxPath = dir + "/brand_new.shx";
    const std::string newDbfPath = dir + "/brand_new.dbf";
    writeBinaryFile( newShxPath, "TORN_NEW_SHX" );
    writeBinaryFile( newDbfPath, "TORN_NEW_DBF" );
    const std::string newJournalPath = newMainPath + ".publish-journal";
    const std::string newJournalJson =
      "{\n"
      "  \"version\": 1,\n"
      "  \"main\": \"" + newMainPath + "\",\n"
      "  \"members\": [\n"
      "    { \"staged\": \"temp.shx\", \"target\": \"" + newShxPath + "\", \"backup\": \"" + newShxPath + ".bak\", \"had_target\": false },\n"
      "    { \"staged\": \"temp.dbf\", \"target\": \"" + newDbfPath + "\", \"backup\": \"" + newDbfPath + ".bak\", \"had_target\": false },\n"
      "    { \"staged\": \"temp.shp\", \"target\": \"" + newMainPath + "\", \"backup\": \"" + newMainPath + ".bak\", \"had_target\": false }\n"
      "  ]\n"
      "}\n";
    writeBinaryFile( newJournalPath, newJournalJson );

    CHECK( atomic_fs::recoverDatasetGroup( newMainPath ) );
    CHECK_FALSE( fs::exists( newShxPath ) );
    CHECK_FALSE( fs::exists( newDbfPath ) );
    CHECK_FALSE( fs::exists( newMainPath ) );
    CHECK_FALSE( fs::exists( newJournalPath ) );
  }

  // 2. Crash after atomic rename to .committed: recovery finalizes publishing without reverting new data
  SECTION( "Scenario 2: Crash after rename to .committed finalizes publishing without reverting new data" )
  {
    const std::string dir = scratch( "adv_crash_committed" );
    const std::string mainPath = dir + "/elevation.tif";
    const std::string sidecarPath = dir + "/elevation.tfw";
    const std::string mainBackup = mainPath + ".bak";
    const std::string sidecarBackup = sidecarPath + ".bak";
    const std::string commitMarkerPath = mainPath + ".publish-journal.committed";

    const std::string newMain = "NEW_ELEVATION_RASTER_DATA_PUBLISHED";
    const std::string newSidecar = "NEW_ELEVATION_WORLDFILE_DATA_PUBLISHED";
    const std::string oldMainBackup = "OLD_ELEVATION_RASTER_OBSOLETE_BACKUP";
    const std::string oldSidecarBackup = "OLD_ELEVATION_WORLDFILE_OBSOLETE_BACKUP";

    writeBinaryFile( mainPath, newMain );
    writeBinaryFile( sidecarPath, newSidecar );
    writeBinaryFile( mainBackup, oldMainBackup );
    writeBinaryFile( sidecarBackup, oldSidecarBackup );

    const std::string committedJson =
      "{\n"
      "  \"version\": 1,\n"
      "  \"main\": \"" + mainPath + "\",\n"
      "  \"members\": [\n"
      "    { \"staged\": \"temp.tfw\", \"target\": \"" + sidecarPath + "\", \"backup\": \"" + sidecarBackup + "\", \"had_target\": true },\n"
      "    { \"staged\": \"temp.tif\", \"target\": \"" + mainPath + "\", \"backup\": \"" + mainBackup + "\", \"had_target\": true }\n"
      "  ]\n"
      "}\n";
    writeBinaryFile( commitMarkerPath, committedJson );

    REQUIRE( atomic_fs::hasPendingJournal( mainPath ) );

    const bool recovered = atomic_fs::recoverDatasetGroup( mainPath );
    CHECK( recovered );
    CHECK_FALSE( atomic_fs::hasPendingJournal( mainPath ) );
    CHECK_FALSE( fs::exists( commitMarkerPath ) );
    CHECK_FALSE( fs::exists( mainBackup ) );
    CHECK_FALSE( fs::exists( sidecarBackup ) );

    // Crucial: new data MUST be preserved, NOT reverted to backups!
    CHECK( readBinaryFile( mainPath ) == newMain );
    CHECK( readBinaryFile( sidecarPath ) == newSidecar );
  }

  // 3. Corrupt/truncated .publish-journal handles errors gracefully without crashing or corruption
  SECTION( "Scenario 3: Corrupt/truncated journal handles bad input gracefully without crashing or corrupting filesystem" )
  {
    const std::string dir = scratch( "adv_corrupt_journal" );

    // Sub-case 3a: 0-byte truncated journal
    {
      const std::string ds = dir + "/trunc0.tif";
      const std::string data = "ORIGINAL_DATA_FOR_TRUNC0";
      writeBinaryFile( ds, data );
      writeBinaryFile( ds + ".publish-journal", "" );

      CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
      CHECK( readBinaryFile( ds ) == data );
      CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
    }

    // Sub-case 3b: Incomplete JSON token
    {
      const std::string ds = dir + "/trunc_json.tif";
      const std::string data = "ORIGINAL_DATA_FOR_TRUNC_JSON";
      writeBinaryFile( ds, data );
      writeBinaryFile( ds + ".publish-journal", "{\"version\": 1, \"main\": \"" + ds + "\", \"members\": [" );

      CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
      CHECK( readBinaryFile( ds ) == data );
      CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
    }

    // Sub-case 3c: Random binary garbage
    {
      const std::string ds = dir + "/garbage.tif";
      const std::string data = "ORIGINAL_DATA_FOR_GARBAGE";
      writeBinaryFile( ds, data );
      const std::string junk = std::string( "\x7f\x45\x4c\x46\x02\x01\x01\x00\xff\xfe\xca\xfe\xba\xbe", 14 );
      writeBinaryFile( ds + ".publish-journal", junk );

      CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
      CHECK( readBinaryFile( ds ) == data );
      CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
    }

    // Sub-case 3d: Valid JSON with unexpected root types
    {
      const std::string ds = dir + "/wrong_schema.tif";
      const std::string data = "ORIGINAL_DATA_FOR_WRONG_SCHEMA";
      writeBinaryFile( ds, data );
      writeBinaryFile( ds + ".publish-journal", "{\"version\": \"one\", \"members\": \"not_array\"}" );

      CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
      CHECK( readBinaryFile( ds ) == data );
      CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
    }

    // Sub-case 3e: Valid JSON array containing malformed non-object members
    {
      const std::string ds = dir + "/malformed_members.tif";
      const std::string data = "ORIGINAL_DATA_FOR_MALFORMED_MEMBERS";
      writeBinaryFile( ds, data );
      writeBinaryFile( ds + ".publish-journal", "{\"version\": 1, \"main\": \"" + ds + "\", \"members\": [null, 123, \"str\", {}]}" );

      CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
      CHECK( readBinaryFile( ds ) == data );
      CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
    }

    // Sub-case 3f: Valid JSON object with member fields having wrong primitive types
    {
      const std::string ds = dir + "/wrong_member_types.tif";
      const std::string data = "ORIGINAL_DATA_FOR_WRONG_MEMBER_TYPES";
      writeBinaryFile( ds, data );
      writeBinaryFile( ds + ".publish-journal", "{\"version\": 1, \"main\": \"" + ds + "\", \"members\": [{\"target\": 12345, \"had_target\": \"not_a_bool\"}]}" );

      CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
      CHECK( readBinaryFile( ds ) == data );
      CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
    }

    // Sub-case 3g: JSON root is valid JSON array instead of JSON object
    {
      const std::string ds = dir + "/root_array.tif";
      const std::string data = "ORIGINAL_DATA_FOR_ROOT_ARRAY";
      writeBinaryFile( ds, data );
      writeBinaryFile( ds + ".publish-journal", "[1, 2, 3, {\"members\": []}]" );

      CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
      CHECK( readBinaryFile( ds ) == data );
      CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
    }
  }

  // 4. Concurrent or repeated publish calls with leftover journals: auto-recovery heals cleanly
  SECTION( "Scenario 4: Repeated publish / auto-recovery cleanly heals leftover journals" )
  {
    const std::string dir = scratch( "adv_repeated_autorecovery" );
    const std::string mainPath = dir + "/survey.shp";
    const std::string sidecarPath = dir + "/survey.shx";
    const std::string mainBackup = mainPath + ".bak";
    const std::string sidecarBackup = sidecarPath + ".bak";

    // Setup original files
    writeBinaryFile( mainPath, "OLD_SURVEY_MAIN" );
    writeBinaryFile( sidecarPath, "OLD_SURVEY_SHX" );

    // Plant leftover uncommitted journal from prior crashed attempt
    writeBinaryFile( mainBackup, "OLD_SURVEY_MAIN" );
    writeBinaryFile( sidecarBackup, "OLD_SURVEY_SHX" );
    writeBinaryFile( mainPath, "CRASHED_TORN_MAIN" );
    writeBinaryFile( sidecarPath, "CRASHED_TORN_SHX" );

    const std::string journalJson =
      "{\n"
      "  \"version\": 1,\n"
      "  \"main\": \"" + mainPath + "\",\n"
      "  \"members\": [\n"
      "    { \"staged\": \"s.shx\", \"target\": \"" + sidecarPath + "\", \"backup\": \"" + sidecarBackup + "\", \"had_target\": true },\n"
      "    { \"staged\": \"s.shp\", \"target\": \"" + mainPath + "\", \"backup\": \"" + mainBackup + "\", \"had_target\": true }\n"
      "  ]\n"
      "}\n";
    writeBinaryFile( mainPath + ".publish-journal", journalJson );

    // Repeated manual recover calls:
    // First call heals and returns true
    CHECK( atomic_fs::recoverDatasetGroup( mainPath ) == true );
    CHECK( readBinaryFile( mainPath ) == "OLD_SURVEY_MAIN" );
    CHECK( readBinaryFile( sidecarPath ) == "OLD_SURVEY_SHX" );

    // Second call is a safe no-op returning false
    CHECK( atomic_fs::recoverDatasetGroup( mainPath ) == false );
    CHECK( readBinaryFile( mainPath ) == "OLD_SURVEY_MAIN" );
    CHECK( readBinaryFile( sidecarPath ) == "OLD_SURVEY_SHX" );

    // Now test auto-recovery embedded in publishStagedGroup:
    // Plant uncommitted journal again
    writeBinaryFile( mainBackup, "OLD_SURVEY_MAIN" );
    writeBinaryFile( mainPath + ".publish-journal", journalJson );
    REQUIRE( atomic_fs::hasPendingJournal( mainPath ) );

    // Prepare fresh staged files
    const std::string stagedMain = dir + "/fresh.shp";
    const std::string stagedSidecar = dir + "/fresh.shx";
    writeBinaryFile( stagedMain, "FINAL_FRESH_MAIN" );
    writeBinaryFile( stagedSidecar, "FINAL_FRESH_SHX" );

    // publishStagedGroup must auto-recover the stale journal and publish the new dataset
    REQUIRE_NOTHROW( atomic_fs::publishStagedGroup( stagedMain, mainPath ) );
    CHECK_FALSE( atomic_fs::hasPendingJournal( mainPath ) );
    CHECK( readBinaryFile( mainPath ) == "FINAL_FRESH_MAIN" );
    CHECK( readBinaryFile( sidecarPath ) == "FINAL_FRESH_SHX" );
    CHECK_FALSE( fs::exists( mainBackup ) );
    CHECK_FALSE( fs::exists( sidecarBackup ) );

    // Concurrency test: 8 parallel worker threads publishing and recovering distinct groups
    std::vector<std::future<bool>> futures;
    for ( int i = 0; i < 8; ++i )
    {
      futures.push_back( std::async( std::launch::async, [dir, i]() -> bool {
        const std::string ds = dir + "/concurrent_" + std::to_string( i ) + ".tif";
        const std::string stg = dir + "/staged_" + std::to_string( i ) + ".tif";
        writeBinaryFile( stg, "PAYLOAD_" + std::to_string( i ) );
        atomic_fs::publishStagedFile( stg, ds );
        return atomic_fs::fileExists( ds ) && readBinaryFile( ds ) == ( "PAYLOAD_" + std::to_string( i ) );
      } ) );
    }
    for ( auto &f : futures )
      CHECK( f.get() == true );
  }

  // 5. Multi-dataset directory recovery: recoverDirectoryJournals cleans up multiple orphaned journals
  SECTION( "Scenario 5: Multi-dataset directory recovery cleans up all orphaned journals across directory" )
  {
    const std::string dir = scratch( "adv_multidataset_dir" );

    // Dataset 1: roads.shp with uncommitted .publish-journal (should roll back)
    const std::string ds1 = dir + "/roads.shp";
    const std::string ds1Bak = ds1 + ".bak";
    const std::string ds1J = ds1 + ".publish-journal";
    writeBinaryFile( ds1, "DS1_TORN" );
    writeBinaryFile( ds1Bak, "DS1_ORIGINAL" );
    writeBinaryFile( ds1J, "{\"version\":1,\"main\":\"" + ds1 + "\",\"members\":[{\"target\":\"" + ds1 + "\",\"backup\":\"" + ds1Bak + "\",\"had_target\":true}]}" );

    // Dataset 2: elevation.tif with committed .publish-journal.committed (should finalize)
    const std::string ds2 = dir + "/elevation.tif";
    const std::string ds2Bak = ds2 + ".bak";
    const std::string ds2J = ds2 + ".publish-journal.committed";
    writeBinaryFile( ds2, "DS2_COMMITTED_NEW" );
    writeBinaryFile( ds2Bak, "DS2_OLD_BACKUP" );
    writeBinaryFile( ds2J, "{\"version\":1,\"main\":\"" + ds2 + "\",\"members\":[{\"target\":\"" + ds2 + "\",\"backup\":\"" + ds2Bak + "\",\"had_target\":true}]}" );

    // Dataset 3: landcover.nc with uncommitted .sicnu-journal (should roll back)
    const std::string ds3 = dir + "/landcover.nc";
    const std::string ds3Bak = ds3 + ".bak";
    const std::string ds3J = ds3 + ".sicnu-journal";
    writeBinaryFile( ds3, "DS3_TORN" );
    writeBinaryFile( ds3Bak, "DS3_ORIGINAL" );
    writeBinaryFile( ds3J, "{\"version\":1,\"main\":\"" + ds3 + "\",\"members\":[{\"target\":\"" + ds3 + "\",\"backup\":\"" + ds3Bak + "\",\"had_target\":true}]}" );

    // Dataset 4: boundaries.gpkg with committed .sicnu-journal.committed (should finalize)
    const std::string ds4 = dir + "/boundaries.gpkg";
    const std::string ds4Bak = ds4 + ".bak";
    const std::string ds4J = ds4 + ".sicnu-journal.committed";
    writeBinaryFile( ds4, "DS4_COMMITTED_NEW" );
    writeBinaryFile( ds4Bak, "DS4_OLD_BACKUP" );
    writeBinaryFile( ds4J, "{\"version\":1,\"main\":\"" + ds4 + "\",\"members\":[{\"target\":\"" + ds4 + "\",\"backup\":\"" + ds4Bak + "\",\"had_target\":true}]}" );

    // Dataset 5: pristine file with no journal (should stay untouched)
    const std::string ds5 = dir + "/clean_pristine.tif";
    writeBinaryFile( ds5, "PRISTINE_DATA" );

    // Recover directory
    const std::size_t recovered = atomic_fs::recoverDirectoryJournals( dir );
    CHECK( recovered == 4 );

    // Verify final states
    CHECK( readBinaryFile( ds1 ) == "DS1_ORIGINAL" );
    CHECK( readBinaryFile( ds2 ) == "DS2_COMMITTED_NEW" );
    CHECK( readBinaryFile( ds3 ) == "DS3_ORIGINAL" );
    CHECK( readBinaryFile( ds4 ) == "DS4_COMMITTED_NEW" );
    CHECK( readBinaryFile( ds5 ) == "PRISTINE_DATA" );

    CHECK_FALSE( fs::exists( ds1Bak ) );
    CHECK_FALSE( fs::exists( ds2Bak ) );
    CHECK_FALSE( fs::exists( ds3Bak ) );
    CHECK_FALSE( fs::exists( ds4Bak ) );
    CHECK_FALSE( fs::exists( ds1J ) );
    CHECK_FALSE( fs::exists( ds2J ) );
    CHECK_FALSE( fs::exists( ds3J ) );
    CHECK_FALSE( fs::exists( ds4J ) );

    // Second recovery run must return 0 (idempotent)
    CHECK( atomic_fs::recoverDirectoryJournals( dir ) == 0 );

    // Edge cases for directory recovery:
    // Non-existent directory returns 0
    CHECK( atomic_fs::recoverDirectoryJournals( dir + "/non_existent_subdir" ) == 0 );
    // File path passed to directory recovery returns 0
    CHECK( atomic_fs::recoverDirectoryJournals( ds5 ) == 0 );
  }
}

TEST_CASE( "Adversarial M3 - B06 Iteration 2: Deeply nested JSON, binary garbage, and permission edge cases",
           "[adv][m3][b06][gen2]" )
{
  const std::string dir = scratch( "adv_m3_gen2_stress" );

  SECTION( "Deeply nested JSON structures do not crash parser or recovery" )
  {
    const std::string ds = dir + "/deep_nested.tif";
    const std::string origData = "ORIGINAL_DEEP_NESTED_DATA";
    writeBinaryFile( ds, origData );

    // Nest 200 levels deep: {"a":{"a":...}}
    std::string deepJson;
    for ( int i = 0; i < 200; ++i )
      deepJson += "{\"level" + std::to_string( i ) + "\":";
    deepJson += "{\"members\":[]}";
    for ( int i = 0; i < 200; ++i )
      deepJson += "}";

    writeBinaryFile( ds + ".publish-journal", deepJson );
    CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
    CHECK( readBinaryFile( ds ) == origData );
    CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );

    // Deeply nested array: [[[[...]]]]
    std::string deepArray;
    for ( int i = 0; i < 200; ++i )
      deepArray += "[";
    deepArray += "1, 2, 3";
    for ( int i = 0; i < 200; ++i )
      deepArray += "]";

    writeBinaryFile( ds + ".publish-journal", deepArray );
    CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
    CHECK( readBinaryFile( ds ) == origData );
    CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
  }

  SECTION( "Multi-megabyte binary garbage and invalid UTF-8 sequences" )
  {
    const std::string ds = dir + "/large_garbage.tif";
    const std::string origData = "ORIGINAL_LARGE_GARBAGE_DATA";
    writeBinaryFile( ds, origData );

    // 2 MB of repeating pseudo-random binary bytes with NULs and invalid UTF-8
    std::string bigGarbage( 2 * 1024 * 1024, '\0' );
    for ( std::size_t i = 0; i < bigGarbage.size(); ++i )
      bigGarbage[i] = static_cast<char>( ( i * 37 + 101 ) & 0xFF );

    writeBinaryFile( ds + ".publish-journal", bigGarbage );
    CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
    CHECK( readBinaryFile( ds ) == origData );
    CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );

    // Embedded NUL bytes inside otherwise valid JSON
    const std::string nulJson = std::string( "{\"version\": 1, \"members\": [\x00\x00\x00]}", 32 );
    writeBinaryFile( ds + ".publish-journal", nulJson );
    CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
    CHECK( readBinaryFile( ds ) == origData );
    CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
  }

  SECTION( "Whitespace-only and invalid numerical literal JSON" )
  {
    const std::string ds = dir + "/whitespace.tif";
    const std::string origData = "ORIGINAL_WHITESPACE_DATA";
    writeBinaryFile( ds, origData );

    // Whitespace only
    writeBinaryFile( ds + ".publish-journal", "   \r\n\t  \n  " );
    CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
    CHECK( readBinaryFile( ds ) == origData );
    CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );

    // NaN / Infinity numerical literals (invalid standard JSON)
    writeBinaryFile( ds + ".publish-journal", "{\"version\": NaN, \"members\": [Infinity]}" );
    CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
    CHECK( readBinaryFile( ds ) == origData );
    CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
  }

  SECTION( "Large volume of malformed array members" )
  {
    const std::string ds = dir + "/malformed_swarm.tif";
    const std::string origData = "ORIGINAL_SWARM_DATA";
    writeBinaryFile( ds, origData );

    // 5000 mixed invalid member elements
    std::string swarmJson = "{\"version\": 1, \"main\": \"" + ds + "\", \"members\": [";
    for ( int i = 0; i < 5000; ++i )
    {
      if ( i > 0 )
        swarmJson += ",";
      switch ( i % 5 )
      {
        case 0: swarmJson += "null"; break;
        case 1: swarmJson += "12345"; break;
        case 2: swarmJson += "\"bogus_string\""; break;
        case 3: swarmJson += "[1,2,3]"; break;
        case 4: swarmJson += "{\"target\": 9999, \"had_target\": \"invalid_bool\", \"backup\": []}"; break;
      }
    }
    swarmJson += "]}";

    writeBinaryFile( ds + ".publish-journal", swarmJson );
    CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( ds ) );
    CHECK( readBinaryFile( ds ) == origData );
    CHECK_FALSE( fs::exists( ds + ".publish-journal" ) );
  }

  SECTION( "Rollback safety when destination file restoration cannot overwrite target" )
  {
    const std::string subDir = dir + "/ro_target_test";
    fs::create_directories( subDir );
    const std::string targetFile = subDir + "/locked_raster.tif";
    const std::string backupFile = targetFile + ".bak";
    const std::string journalFile = targetFile + ".publish-journal";

    writeBinaryFile( targetFile, "CORRUPTED_INCOMPLETE_WRITE" );
    writeBinaryFile( backupFile, "SAFE_ORIGINAL_BACKUP" );

    const std::string journalJson =
      "{\n"
      "  \"version\": 1,\n"
      "  \"main\": \"" + targetFile + "\",\n"
      "  \"members\": [\n"
      "    { \"staged\": \"temp.tif\", \"target\": \"" + targetFile + "\", \"backup\": \"" + backupFile + "\", \"had_target\": true }\n"
      "  ]\n"
      "}\n";
    writeBinaryFile( journalFile, journalJson );

    // Make target directory read-only so rename/remove inside it fails
    fs::permissions( subDir, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace );

    // Attempt recovery under read-only target conditions
    CHECK_NOTHROW( atomic_fs::recoverDatasetGroup( targetFile ) );

    // Restore permissions so we can inspect and clean up
    fs::permissions( subDir, fs::perms::all, fs::perm_options::replace );

    // Crucial durability guarantee: If target could not be restored, the backup MUST NOT be unlinked!
    CHECK( fs::exists( backupFile ) );
    CHECK( readBinaryFile( backupFile ) == "SAFE_ORIGINAL_BACKUP" );
  }
}
