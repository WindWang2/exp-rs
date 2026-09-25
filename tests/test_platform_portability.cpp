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
#include <cstdio>
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
  REQUIRE( isWindowsReservedName( "con.txt.txt" ) ); // base before the first dot is CON
  REQUIRE_FALSE( isWindowsReservedName( "" ) );
  REQUIRE_FALSE( isWindowsReservedName( ".txt" ) );
}

TEST_CASE( "envUtf8: a set-but-empty variable and a missing variable are both "
           "empty, and callers' missing-value semantics are unchanged",
           "[platform][env]" )
{
  const char *name = "SICNU_PORTABLE_EMPTY_VAR";
#if !defined( _WIN32 )
  ::setenv( name, "", 1 );
  REQUIRE( envUtf8( name ).empty() );    // set-but-empty -> empty bytes
#else
  ::SetEnvironmentVariableW( sicnu::portable::wideFromUtf8( name ).c_str(), L"" );
  REQUIRE( envUtf8( name ).empty() );
#endif
  REQUIRE( envUtf8( "SICNU_PORTABLE_ABSENT_VAR_XYZ" ).empty() ); // missing -> empty

  // The old const char* idiom `if (raw && *raw)` maps onto `value.empty()`
  // without changing either branch's outcome.
  const std::string value = envUtf8( name );
  const bool usableOldWay = !value.empty();
  REQUIRE_FALSE( usableOldWay );
}

TEST_CASE( "path<->UTF-8 is byte-transparent even for bytes that are not "
           "valid UTF-8 (and for Windows-style spellings on any platform)",
           "[platform][utf8]" )
{
  // Raw non-UTF-8 bytes (e.g. a POSIX filename in a legacy encoding): the
  // bytes must pass through untouched — any locale/ACP detour would corrupt
  // them. On Windows this exact sequence has no ACP mapping, which is why
  // the conversion must ride the wide API, never path::string().
  const std::string raw = std::string( "raw-\xC3\x28-\xFF-\xFE.txt" );
  REQUIRE( sicnu::portable::pathToUtf8( sicnu::portable::pathFromUtf8( raw ) ) == raw );

  // Windows-style spellings are byte strings on POSIX (drive/UNC/long
  // prefix fixtures). Round-trip is exact everywhere; what these bytes DO on
  // a Windows host (drive resolution, long-prefix opt-out of MAX_PATH
  // stripping) is compile/static evidence on this lane, not runtime-verified.
  const std::string windowsFlavors[] = {
    "C:\\Users\\café\\数据.tif",
    "\\\\server\\share\\数据\\x.tif",
    "\\\\?\\C:\\long\\カタカナ\\x.tif",
  };
  for ( const std::string &flavor : windowsFlavors )
    REQUIRE( sicnu::portable::pathToUtf8( sicnu::portable::pathFromUtf8( flavor ) )
             == flavor );

  // Reserved-name classification applies to the BASE NAME of Windows-bound
  // writes; drive-qualified spellings must never be fed here, but the helper
  // still answers for its documented input domain.
  REQUIRE( isWindowsReservedName( "CON" ) );
  REQUIRE_FALSE( isWindowsReservedName( "C:" ) );
}

TEST_CASE( "fileOpenUtf8 opens through the UTF-8 boundary", "[platform][fs]" )
{
  const fs::path base = fs::temp_directory_path() / "sicnu-portable-fopen-实验-🌍";
  fs::remove_all( base );
  fs::create_directories( base );
  const std::string path = sicnu::portable::pathToUtf8( base / "block.dat" );

  {
    std::FILE *out = sicnu::portable::fileOpenUtf8( path, "wb" );
    REQUIRE( out != nullptr );
    REQUIRE( std::fwrite( "0123456789", 1, 10, out ) == 10 );
    std::fclose( out );
  }
  {
    std::FILE *in = sicnu::portable::fileOpenUtf8( path, "rb" );
    REQUIRE( in != nullptr );
    char buffer[ 11 ] = {};
    REQUIRE( std::fread( buffer, 1, 10, in ) == 10 );
    std::fclose( in );
    REQUIRE( std::string( buffer ) == "0123456789" );
  }
  // Missing file -> nullptr, like fopen.
  REQUIRE( sicnu::portable::fileOpenUtf8(
             sicnu::portable::pathToUtf8( base / "absent.dat" ), "rb" ) == nullptr );

  fs::remove_all( base );
}

TEST_CASE( "syncFileUtf8 flushes an existing file and fails a missing one; "
           "directory sync is best-effort silent",
           "[platform][fs][durability]" )
{
  const fs::path base = fs::temp_directory_path() / "sicnu-portable-fsync";
  fs::remove_all( base );
  fs::create_directories( base );
  const std::string file = sicnu::portable::pathToUtf8( base / "doc.txt" );
  {
    std::ofstream out( file, std::ios::binary );
    out << "payload";
  }
  REQUIRE( sicnu::portable::syncFileUtf8( file ) );
  REQUIRE_FALSE( sicnu::portable::syncFileUtf8(
    sicnu::portable::pathToUtf8( base / "absent.txt" ) ) );
  // Best-effort: never throws, whatever the target (missing, file-not-dir).
  sicnu::portable::syncDirectoryBestEffortUtf8(
    sicnu::portable::pathToUtf8( base / "doc.txt" ) );
  sicnu::portable::syncDirectoryBestEffortUtf8(
    sicnu::portable::pathToUtf8( base / "absent-dir" / "x" ) );

  // The save→fsync→rename→reopen sequence the publish contract names, over
  // the primitives themselves (the journal suite drives it end-to-end).
  {
    std::ofstream out( file, std::ios::binary | std::ios::trunc );
    out << "payload-v2";
  }
  REQUIRE( sicnu::portable::syncFileUtf8( file ) );
  const std::string published = sicnu::portable::pathToUtf8( base / "published.txt" );
  fs::rename( sicnu::portable::pathFromUtf8( file ), sicnu::portable::pathFromUtf8( published ) );
  std::ifstream in( published, std::ios::binary );
  std::string body( ( std::istreambuf_iterator<char>( in ) ),
                    std::istreambuf_iterator<char>() );
  REQUIRE( body == "payload-v2" );

  fs::remove_all( base );
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
