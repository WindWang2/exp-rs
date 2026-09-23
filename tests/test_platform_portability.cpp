/***************************************************************************
  tests/test_platform_portability.cpp — Cross-platform Completion oracle.

  Pins the contract of src/platform/portable.h: process id, UTF-8 path
  round-trips, UTF-8 environment reads, Windows reserved-name
  classification, and the POSIX rename-replaces-symlink publication
  contract that geospatial/util/atomic_fs relies on. Header-only subject
  (no sicnu library) so the suite builds and runs on every lane.
 ***************************************************************************/

#include "platform/portable.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#if !defined( _WIN32 )
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using sicnu::portable::envUtf8;
using sicnu::portable::isWindowsReservedName;
using sicnu::portable::pathFromUtf8;
using sicnu::portable::pathToUtf8;
using sicnu::portable::pid;

TEST_CASE( "portable pid is nonzero, stable and matches getpid on POSIX", "[platform][pid]" )
{
  const std::uint32_t a = pid();
  REQUIRE( a != 0 );
  REQUIRE( a == pid() );
#if !defined( _WIN32 )
  REQUIRE( a == static_cast<std::uint32_t>( ::getpid() ) );
#endif
}

TEST_CASE( "path <-> UTF-8 round trip is exact", "[platform][utf8]" )
{
  // Empty stays empty (used as "no path configured" sentinel by callers).
  REQUIRE( pathFromUtf8( std::string() ).empty() );
  REQUIRE( pathToUtf8( fs::path() ).empty() );

  const std::string samples[] = {
    "simple.txt",
    "数据/文件-①.txt",
    "café/🌍/naïve.txt",
    "dir with space/name",
    "ミックス/cat-写真.tif",
    "./relative/segment",
  };
  for ( const std::string &sample : samples )
  {
    INFO( "sample: " << sample );
    REQUIRE( pathToUtf8( pathFromUtf8( sample ) ) == sample );
  }
}

TEST_CASE( "non-ASCII paths survive a real create/write/reopen cycle", "[platform][utf8][fs]" )
{
  const fs::path base = fs::temp_directory_path() / pathFromUtf8( "sicnu-portable-实验-🌍" );
  fs::remove_all( base );
  fs::create_directories( base );

  const std::string name = "café_数据-①.txt";
  const fs::path file = base / pathFromUtf8( name );

  {
    std::ofstream out( file, std::ios::binary | std::ios::trunc );
    REQUIRE( out.good() );
    out << "payload-987";
  }
  REQUIRE( fs::exists( file ) );
  // filename() → UTF-8 must return the original bytes (this is what the
  // stores do when they persist an entry name derived from a fs::path).
  REQUIRE( pathToUtf8( file.filename() ) == name );

  {
    std::ifstream in( file, std::ios::binary );
    REQUIRE( in.good() );
    std::string payload;
    in >> payload;
    REQUIRE( payload == "payload-987" );
  }

  fs::remove_all( base );
  REQUIRE_FALSE( fs::exists( base ) );
}

TEST_CASE( "envUtf8 returns UTF-8 value bytes and empty for missing", "[platform][env]" )
{
  const char *name = "SICNU_PORTABLE_TEST_VAR";
  const char *value = "café-🌍-路径";
#if !defined( _WIN32 )
  REQUIRE( ::setenv( name, value, 1 ) == 0 );
#else
  REQUIRE( ::SetEnvironmentVariableW( sicnu::portable::wideFromUtf8( name ).c_str(),
                                      sicnu::portable::wideFromUtf8( value ).c_str() ) != 0 );
#endif
  REQUIRE( envUtf8( name ) == value );
  REQUIRE( envUtf8( "SICNU_PORTABLE_TEST_VAR_MISSING_XYZ" ).empty() );
}

TEST_CASE( "isWindowsReservedName classifies device names", "[platform][windows]" )
{
  // Reserved with or without extension, any case.
  REQUIRE( isWindowsReservedName( "CON" ) );
  REQUIRE( isWindowsReservedName( "con" ) );
  REQUIRE( isWindowsReservedName( "Con.txt" ) );
  REQUIRE( isWindowsReservedName( "NUL" ) );
  REQUIRE( isWindowsReservedName( "PRN.aux" ) );
  REQUIRE( isWindowsReservedName( "AUX" ) );
  REQUIRE( isWindowsReservedName( "com1" ) );
  REQUIRE( isWindowsReservedName( "COM9.dat" ) );
  REQUIRE( isWindowsReservedName( "lpt4.bak" ) );

  // Near-misses and ordinary names are NOT reserved.
  REQUIRE_FALSE( isWindowsReservedName( "LPT10" ) );
  REQUIRE_FALSE( isWindowsReservedName( "COM0" ) );
  REQUIRE_FALSE( isWindowsReservedName( "CONX" ) );
  REQUIRE_FALSE( isWindowsReservedName( "con txt" ) );
  REQUIRE_FALSE( isWindowsReservedName( "data" ) );
  REQUIRE_FALSE( isWindowsReservedName( "con.txt.txt" ) == false ); // base before first dot is CON
  REQUIRE_FALSE( isWindowsReservedName( "" ) );
  REQUIRE_FALSE( isWindowsReservedName( ".txt" ) );
}

#if !defined( _WIN32 )
// POSIX publication contract (Windows equivalent is MoveFileExW/
// ReplaceFileW inside atomic_fs): rename(2) swaps the directory entry and
// never follows a symlink at the destination — publishing over a symlinked
// output replaces the link itself and leaves the link target untouched.
TEST_CASE( "rename over a symlink replaces the link, not the target", "[platform][fs][symlink]" )
{
  const fs::path base = fs::temp_directory_path() / "sicnu-portable-symlink";
  fs::remove_all( base );
  fs::create_directories( base );

  const fs::path target = base / "real-output.txt";
  const fs::path link = base / "output.txt";
  { std::ofstream out( target, std::ios::binary ); out << "old"; }
  fs::create_symlink( "real-output.txt", link );
  REQUIRE( fs::is_symlink( link ) );

  const fs::path staged = base / "output.txt.staged";
  { std::ofstream out( staged, std::ios::binary ); out << "new"; }

  fs::rename( staged, link );
  // The link became a regular file (the rename replaced the link itself);
  // the link target is untouched and still a regular file.
  REQUIRE( fs::is_regular_file( link ) );
  REQUIRE_FALSE( fs::is_symlink( link ) );
  REQUIRE_FALSE( fs::is_symlink( target ) );

  std::ifstream in( link, std::ios::binary );
  std::string payload;
  in >> payload;
  REQUIRE( payload == "new" );
  std::ifstream tin( target, std::ios::binary );
  tin >> payload;
  REQUIRE( payload == "old" );

  fs::remove_all( base );
}
#endif
