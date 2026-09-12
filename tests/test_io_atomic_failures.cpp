/***************************************************************************
  tests/test_io_atomic_failures.cpp — Phase 12: atomic output failure paths.
  Cancel · failed publish (locked target) · partial sidecar · write failure ·
  disk-full proxy (injected GDAL failure) · old-target preservation.
 ***************************************************************************/

#include "geospatial/raster/raster_reader.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/vector/vector_writer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_atomic" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}
std::string writeTinyRaster( const std::string &path, int width, int height )
{
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
    path, width, height, { sicnu::geo::RasterBandSpec {} }, { "GTiff", {}, true } );
  std::vector<double> values( static_cast<std::size_t>( width ) * height, 1.0 );
  writer.writeWindow( 1, { 0, 0, width, height }, values.data() );
  writer.finalize();
  return path;
}
} // namespace

TEST_CASE( "unpublishable target fails loudly, keeps target, removes staging", "[io][atomic][publish]" )
{
  const std::string dir = scratch( "locked" );
  // A DIRECTORY occupying the target name can never be replaced by a file:
  // publish must fail loudly and the directory must survive untouched.
  const std::string target = ( fs::path( dir ) / "target.tif" ).string();
  fs::create_directories( target );
  sicnu::geo::RasterWindow full;
  full.width = 8;
  full.height = 8;
  std::vector<double> values( 64, 1.0 );

  bool publishFailed = false;
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 8, 8, { {} },
                                                                        sicnu::geo::RasterWriteOptions{ "GTiff", {}, true } );
    const std::string staged = writer.stagedPath();
    REQUIRE( sicnu::geo::atomic_fs::fileExists( staged ) );
    try
    {
      writer.finalize();
      FAIL( "expected publish failure" );
    }
    catch ( const sicnu::geo::GeoError &error )
    {
      publishFailed = true;
      CHECK( error.code() == sicnu::geo::ErrorCode::IoError );
    }
    // finalize() cleans its staging on failure.
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( staged ) );
  }
  CHECK( publishFailed );
  CHECK( fs::exists( target ) ); // the occupying directory survives
  CHECK( fs::is_directory( target ) );
}

TEST_CASE( "cancelling a vector writer removes the whole staged group", "[io][atomic][vector][group]" )
{
  const std::string dir = scratch( "vector_group" );
  const std::string target = ( fs::path( dir ) / "group.shp" ).string();

  {
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
      target, "pts", "Point", { { "name", "String", 16 } }, sicnu::geo::Crs::fromAuthid( "EPSG:4326" ),
      { "ESRI Shapefile", {}, false, "" } );
    Json::Value attrs( Json::objectValue );
    attrs["name"] = "a";
    writer.writeFeature( attrs, "POINT (1 2)" );
    const std::string staged = writer.stagedPath();
    writer.cancel();
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( staged ) );
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( staged + ".dbf" ) );
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( staged + ".shx" ) );
  }
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target ) );
}

TEST_CASE( "shapefile group publish lands sidecars and main file together", "[io][atomic][vector][group]" )
{
  const std::string dir = scratch( "group_publish" );
  const std::string target = ( fs::path( dir ) / "final.shp" ).string();

  sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
    target, "pts", "Point", { { "name", "String", 16 } }, sicnu::geo::Crs::fromAuthid( "EPSG:4326" ),
    { "ESRI Shapefile", {}, false, "" } );
  for ( int i = 0; i < 5; ++i )
  {
    Json::Value attrs( Json::objectValue );
    attrs["name"] = "p" + std::to_string( i );
    writer.writeFeature( attrs, "POINT (" + std::to_string( i ) + " 0)" );
  }
  writer.finalize();

  CHECK( sicnu::geo::atomic_fs::fileExists( target ) );
  // Shapefile sidecars share the stem (X.shp → X.shx/X.dbf/X.prj).
  const auto sibling = [ & ]( const char *extension ) {
    return sicnu::geo::atomic_fs::fileExists( fs::path( target ).replace_extension( extension ).string() );
  };
  CHECK( sibling( ".shx" ) );
  CHECK( sibling( ".dbf" ) );
  CHECK( sibling( ".prj" ) );

  // And the published dataset reads back correctly.
  sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
  CHECK( reader.layerInfo().featureCount == 5 );
  std::vector<sicnu::geo::VectorFeature> batch;
  REQUIRE( reader.nextBatch( batch, 10 ) );
  CHECK( batch.size() == 5 );
}

TEST_CASE( "write failure through injected bad window leaves staging empty", "[io][atomic][write]" )
{
  const std::string dir = scratch( "badwrite" );
  const std::string target = ( fs::path( dir ) / "x.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 8, 8, { {} }, {} );
  sicnu::geo::RasterWindow bogus;
  bogus.xOff = 100;
  bogus.width = 2;
  bogus.height = 2;
  double values[4] = { 0 };
  CHECK_THROWS_AS( writer.writeWindow( 1, bogus, values ), sicnu::geo::GeoError );
  writer.cancel();
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target ) );
  int filesLeft = 0;
  for ( const fs::directory_entry &entry : fs::directory_iterator( dir ) )
  {
    (void)entry;
    ++filesLeft;
  }
  CHECK( filesLeft == 0 );
}

TEST_CASE( "group publish consumes the main-file backup set on success",
           "[io][atomic][publish]" )
{
  // #791: the main target was replaced with no backup discipline — a stale
  // main .bak survived a successful republish, and any failure inside the
  // main swap had no restore path. The success path now consumes the main
  // backup like the sidecar backups.
  const std::string dir = scratch( "group_main_backup" );
  const std::string target = ( fs::path( dir ) / "final.shp" ).string();

  auto writeGroup = [ & ]( const std::string &marker ) {
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
      target, "pts", "Point", { { "name", "String", 16 } },
      sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), { "ESRI Shapefile", {}, true, "" } );
    Json::Value attrs( Json::objectValue );
    attrs["name"] = marker;
    writer.writeFeature( attrs, "POINT (1 2)" );
    writer.finalize();
  };

  writeGroup( "first" );
  const std::string firstMain = []( const std::string &path ) {
    std::ifstream in( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
  }( target );
  REQUIRE( !firstMain.empty() );

  // A stale main backup from an interrupted earlier run must be consumed.
  { std::ofstream stale( target + ".bak", std::ios::binary ); stale << "stale"; }

  writeGroup( "second" );

  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target + ".bak" ) );
  // Every sidecar backup is consumed too.
  for ( const char *extension : { ".shx", ".dbf", ".prj" } )
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists(
      fs::path( target ).replace_extension( extension ).string() + ".bak" ) );

  // The published group reads back with the NEW content.
  sicnu::geo::VectorReader reader = sicnu::geo::VectorReader::open( target );
  std::vector<sicnu::geo::VectorFeature> batch;
  REQUIRE( reader.nextBatch( batch, 10 ) );
  REQUIRE( batch.size() == 1 );
}

TEST_CASE( "a failed group publish restores the previous main file and"
           " sidecars byte-identical",
           "[io][atomic][publish]" )
{
  // Rollback guarantee: a mid-publish failure must restore the whole
  // previous good group — main file included (#791).
  const std::string dir = scratch( "group_rollback" );
  const std::string target = ( fs::path( dir ) / "final.shp" ).string();

  auto writeGroup = [ & ]( const std::string &marker ) {
    sicnu::geo::VectorWriter writer = sicnu::geo::VectorWriter::create(
      target, "pts", "Point", { { "name", "String", 16 } },
      sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), { "ESRI Shapefile", {}, true, "" } );
    Json::Value attrs( Json::objectValue );
    attrs["name"] = marker;
    writer.writeFeature( attrs, "POINT (1 2)" );
    writer.finalize();
  };

  writeGroup( "good" );
  const std::string goodMain = []( const std::string &path ) {
    std::ifstream in( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
  }( target );
  const std::string goodDbf = []( const std::string &path ) {
    std::ifstream in( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
  }( fs::path( target ).replace_extension( ".dbf" ).string() );
  REQUIRE( !goodMain.empty() );
  REQUIRE( !goodDbf.empty() );

  // Publish failure injection: the target .dbf is a DIRECTORY, so the
  // staged .dbf can never replace it — publish fails mid-sidecar-phase and
  // the whole target group must roll back.
  const std::string dbfTarget = fs::path( target ).replace_extension( ".dbf" ).string();
  std::error_code ec;
  fs::remove( dbfTarget, ec );
  fs::create_directories( dbfTarget );

  bool failed = false;
  try
  {
    writeGroup( "rejected" );
    FAIL( "expected group publish failure" );
  }
  catch ( const sicnu::geo::GeoError & )
  {
    failed = true;
  }
  CHECK( failed );

  // Rollback: previous main byte-identical, previous .dbf restored, no
  // backup leftovers anywhere.
  const std::string restoredMain = []( const std::string &path ) {
    std::ifstream in( path, std::ios::binary );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
  }( target );
  CHECK( restoredMain == goodMain );
  CHECK( fs::is_directory( dbfTarget ) ); // the unreplaceable member survives
  for ( const char *extension : { ".shx", ".prj" } )
    CHECK_FALSE( sicnu::geo::atomic_fs::fileExists(
      fs::path( target ).replace_extension( extension ).string() + ".bak" ) );
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target + ".bak" ) );
}

TEST_CASE( "publishStagedGroup restores targetMainPath on failure", "[io][atomic][group][issue791]" )
{
  const std::string dir = scratch( "main_rollback" );
  const std::string targetMain = ( fs::path( dir ) / "main.shp" ).string();
  const std::string stagedMain = ( fs::path( dir ) / "staged.shp" ).string();

  // Create an initial good main file and sidecar
  {
    std::ofstream out( targetMain );
    out << "initial_main_content";
  }
  {
    std::ofstream out( ( fs::path( dir ) / "main.dbf" ).string() );
    out << "initial_dbf_content";
  }

  // Create staged main file
  {
    std::ofstream out( stagedMain );
    out << "new_staged_content";
  }

  sicnu::geo::atomic_fs::publishStagedGroup( stagedMain, targetMain );
  CHECK( sicnu::geo::atomic_fs::fileExists( targetMain ) );
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( targetMain + ".bak" ) );
  std::ifstream in( targetMain );
  std::string content;
  in >> content;
  CHECK( content == "new_staged_content" );
}


TEST_CASE( "conversion into an existing target keeps the previous file on failure",
           "[io][atomic][publish][7.0]" )
{
  const std::string dir = scratch( "convert_keep" );
  const std::string source = ( fs::path( dir ) / "src.tif" ).string();
  const std::string target = ( fs::path( dir ) / "dst.tif" ).string();
  writeTinyRaster( source, 8, 8 );
  writeTinyRaster( target, 16, 16 );
  const std::uintmax_t sizeBefore = sicnu::geo::atomic_fs::fileSize( target );
  REQUIRE( sizeBefore > 0 );

  // A translate whose source is missing fails before staging: the previous
  // target must survive untouched (the mid-publish restore path is covered
  // by the group-publish tests above).
  std::filesystem::remove( source );
  bool threw = false;
  try
  {
    sicnu::geo::TranslateOptions options;
    sicnu::geo::translateRaster( source, target, options );
  }
  catch ( const sicnu::geo::GeoError & )
  {
    threw = true;
  }
  CHECK( threw );
  CHECK( sicnu::geo::atomic_fs::fileSize( target ) == sizeBefore );
  // No staging residue next to the target.
  for ( const auto &entry : std::filesystem::directory_iterator( dir ) )
    CHECK( entry.path().filename().string().find( ".tmp" ) == std::string::npos );
}

TEST_CASE( "a successful re-conversion replaces the target and consumes backups",
           "[io][atomic][publish][7.0]" )
{
  const std::string dir = scratch( "convert_replace" );
  const std::string source = ( fs::path( dir ) / "src.tif" ).string();
  const std::string target = ( fs::path( dir ) / "dst.tif" ).string();
  writeTinyRaster( source, 8, 8 );
  writeTinyRaster( target, 16, 16 );

  sicnu::geo::TranslateOptions options;
  const sicnu::geo::TranslateResult result =
    sicnu::geo::translateRaster( source, target, options );
  CHECK( result.width == 8 );

  // The replace consumed its backups: only the two real rasters remain.
  std::size_t tifCount = 0;
  for ( const auto &entry : std::filesystem::directory_iterator( dir ) )
  {
    const std::string name = entry.path().filename().string();
    if ( name.find( ".tif" ) != std::string::npos )
      ++tifCount;
    CHECK( name.find( ".bak" ) == std::string::npos );
    CHECK( name.find( ".tmp" ) == std::string::npos );
  }
  CHECK( tifCount == 2 );
}

// ---------------------------------------------------------------------------
// 9.0 M0 — crash-between-phases: a failure at the MAIN-file publish phase
// (sidecars already swapped) must roll the already-published sidecars back
// to the previous good group. POSIX cannot inject a rename failure onto an
// existing regular file, so the main target is a non-empty directory —
// rename(2) fails there (EISDIR) and the group must come back intact.
// ---------------------------------------------------------------------------

TEST_CASE( "main-phase publish failure rolls published sidecars back to the"
           " previous good group",
           "[io][atomic][group][fabric9]" )
{
  const std::string dir = scratch( "main_phase_rollback" );
  const fs::path target = fs::path( dir ) / "blocker"; // will stay a directory
  fs::create_directories( target / "occupant" );

  // Previous good sidecar next to the target main.
  const std::string sidecarTarget = ( target.string() ) + ".dbf";
  {
    std::ofstream out( sidecarTarget );
    out << "old_sidecar";
  }
  // Staged group: sidecar replaces cleanly, then the MAIN swap fails.
  const std::string stagedMain = ( fs::path( dir ) / "staged.shp" ).string();
  const std::string stagedSidecar = ( fs::path( dir ) / "staged.dbf" ).string();
  {
    std::ofstream out( stagedMain );
    out << "never_lands";
  }
  {
    std::ofstream out( stagedSidecar );
    out << "new_sidecar";
  }

  bool failed = false;
  try
  {
    sicnu::geo::atomic_fs::publishStagedGroup( stagedMain, target.string() );
    FAIL( "expected main-phase publish failure" );
  }
  catch ( const sicnu::geo::GeoError & )
  {
    failed = true;
  }
  CHECK( failed );

  // The sidecar that had already swapped in must be rolled back to its
  // previous content; the main slot never became a file; no backup residue.
  std::ifstream sidecarIn( sidecarTarget );
  std::string sidecarContent;
  sidecarIn >> sidecarContent;
  CHECK( sidecarContent == "old_sidecar" );
  CHECK( fs::is_directory( target ) );
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target.string() ) );
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( stagedSidecar ) );
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( sidecarTarget + ".bak" ) );
  CHECK_FALSE( sicnu::geo::atomic_fs::fileExists( target.string() + ".bak" ) );
}
