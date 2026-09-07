/***************************************************************************
  tests/test_io_paths.cpp — Phase 16: security & cross-platform path suite.
  Path traversal · Unicode · Windows path shapes · relative/temp paths ·
  malformed datasets · symlinked sources.
 ***************************************************************************/

#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
std::string scratch( const std::string &name )
{
  const fs::path dir = fs::temp_directory_path() / "sicnu_io_test_paths" / name;
  std::error_code ec;
  fs::remove_all( dir, ec ); // idempotent suites: start from a clean scratch
  fs::create_directories( dir );
  return dir.string();
}
} // namespace

TEST_CASE( "nonexistent and hostile paths fail as structured OpenFailed", "[io][paths][security]" )
{
  // Traversal attempts: the layer reports failure, never follows into
  // something unintended silently.
  for ( const std::string &hostile : { std::string( "../../../etc/passwd" ),
                                       std::string( "..\\..\\windows\\system32\\config\\sam" ),
                                       std::string( "/vsizip/../..//etc" ),
                                       std::string( "C:/nonexistent_drive_xyz/a.tif" ) } )
  {
    try
    {
      sicnu::geo::inspectRaster( hostile );
      FAIL( "expected failure for " + hostile );
    }
    catch ( const sicnu::geo::GeoError &error )
    {
      CHECK( error.code() == sicnu::geo::ErrorCode::OpenFailed );
    }
  }
}

TEST_CASE( "unicode and spaces in paths work end to end", "[io][paths][unicode]" )
{
#ifdef _WIN32
  const auto utf8 = []( const fs::path &p ) {
    const std::u8string u8 = p.u8string();
    return std::string( u8.begin(), u8.end() );
  };
  const std::string dir = scratch( "unicode" );
  const fs::path target = fs::u8path( dir ) / fs::u8path( "数据集-ünïcødé-狀態.tif" );
  const std::string targetPath = utf8( target );

  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( targetPath, 4, 4, { {} }, {} );
  sicnu::geo::RasterWindow full;
  full.width = 4;
  full.height = 4;
  std::vector<double> values( 16, 7.0 );
  writer.writeWindow( 1, full, values.data() );
  writer.finalize();

  sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( targetPath );
  CHECK( reader.metadata().width == 4 );
  const std::string inspected = sicnu::geo::inspectAny( targetPath )["kind"].asString();
  CHECK( inspected == "raster" );

  // Space paths too.
  const std::string spaced = ( fs::u8path( dir ) / fs::u8path( "with space.tif" ) ).string();
  sicnu::geo::RasterWriter spaceWriter = sicnu::geo::RasterWriter::create( spaced, 4, 4, { {} }, {} );
  spaceWriter.finalize();
  CHECK( sicnu::geo::inspectRaster( spaced ).width == 4 );
#endif
}

TEST_CASE( "windows path shapes: relative, backslash, UNC structure", "[io][paths][windows]" )
{
#ifdef _WIN32
  const std::string dir = scratch( "shapes" );
  const std::string target = ( fs::path( dir ) / "plain.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, {} );
  writer.finalize();

  // Backslash spelling of the same file opens to the same metadata.
  std::string backslashed = target;
  for ( char &c : backslashed )
    if ( c == '/' )
      c = '\\';
  CHECK( sicnu::geo::inspectRaster( backslashed ).width == 4 );

  // Relative paths resolve against the process CWD like any file API; verify
  // a same-directory relative form works when we chdir there.
  const fs::path oldCwd = fs::current_path();
  fs::current_path( dir );
  try
  {
    CHECK( sicnu::geo::inspectRaster( "plain.tif" ).width == 4 );
    CHECK( sicnu::geo::inspectRaster( ".\\plain.tif" ).width == 4 );
  }
  catch ( ... )
  {
    fs::current_path( oldCwd );
    throw;
  }
  fs::current_path( oldCwd );

  // Long-ish path robustness: nesting depth stays functional.
  fs::path deep = fs::u8path( dir );
  for ( int i = 0; i < 8; ++i )
    deep = deep / "nested_level";
  fs::create_directories( deep );
  const std::string deepTarget = ( deep / "deep.tif" ).string();
  sicnu::geo::RasterWriter deepWriter = sicnu::geo::RasterWriter::create( deepTarget, 4, 4, { {} }, {} );
  deepWriter.finalize();
  CHECK( sicnu::geo::inspectRaster( deepTarget ).width == 4 );
#endif
}

TEST_CASE( "symlinked raster sources are readable through the link", "[io][paths][symlink]" )
{
  const std::string dir = scratch( "symlink" );
  const std::string target = ( fs::path( dir ) / "real.tif" ).string();
  sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( target, 4, 4, { {} }, {} );
  writer.finalize();

  const std::string linkPath = ( fs::path( dir ) / "link.tif" ).string();
  std::error_code ec;
  fs::create_symlink( fs::u8path( target ), fs::u8path( linkPath ), ec );
  if ( ec )
  {
    // Windows without symlink privilege: a hardlink exercises the same
    // indirection contract at directory-entry level.
    fs::create_hard_link( fs::u8path( target ), fs::u8path( linkPath ), ec );
    if ( ec )
    {
      WARN( "symlink/hardlink unavailable on this host — case self-skipped" );
      return;
    }
  }
  CHECK( sicnu::geo::inspectRaster( linkPath ).width == 4 );
}

TEST_CASE( "malformed datasets fail without crashing and without junk metadata", "[io][paths][malformed]" )
{
  const std::string dir = scratch( "malformed" );

  // Truncated TIFF: real header, cut off mid-file.
  {
    const std::string good = ( fs::path( dir ) / "to_truncate.tif" ).string();
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create( good, 64, 64, { {} }, {} );
    writer.finalize();
    const auto fullSize = sicnu::geo::atomic_fs::fileSize( good );
    {
      std::ifstream in( good, std::ios::binary );
      std::string content( static_cast<std::size_t>( fullSize ), '\0' );
      in.read( content.data(), fullSize );
      in.close();
      std::ofstream out( ( fs::path( dir ) / "truncated.tif" ).string(), std::ios::binary );
      out.write( content.data(), static_cast<std::streamsize>( fullSize / 3 ) );
    }
  }
  const std::string truncated = ( fs::path( dir ) / "truncated.tif" ).string();
  try
  {
    sicnu::geo::RasterReader reader = sicnu::geo::RasterReader::open( truncated );
    // Some GDAL builds open truncated TIFFs read-only; a window read crossing
    // the truncation must then fail loudly.
    bool failed = false;
    try
    {
      sicnu::geo::RasterWindow full;
      full.width = 64;
      full.height = 64;
      reader.readWindow( { 1 }, full );
    }
    catch ( const sicnu::geo::GeoError & )
    {
      failed = true;
    }
    CHECK( failed );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::OpenFailed );
  }

  // An empty file with a raster extension.
  const std::string empty = ( fs::path( dir ) / "empty.tif" ).string();
  { std::ofstream out( empty ); }
  CHECK( sicnu::geo::atomic_fs::fileExists( empty ) );
  try
  {
    sicnu::geo::inspectRaster( empty );
    FAIL( "expected OpenFailed" );
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    CHECK( error.code() == sicnu::geo::ErrorCode::OpenFailed );
  }
}

TEST_CASE( "atomic staging never escapes the target directory", "[io][paths][security]" )
{
  const std::string dir = scratch( "staging" );
  const std::string target = ( fs::path( dir ) / "out.tif" ).string();
  const std::string staged = sicnu::geo::atomic_fs::stagedPathFor( target );

  // The staged path lives beside the target (same volume → atomic rename).
  CHECK( staged.rfind( dir, 0 ) == 0 );
  CHECK( staged.find( ".tmp" ) != std::string::npos );
}
