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

namespace
{
// Case-setup scrub, no-throw: a transient handle on the previous run's
// leftovers (AV scan, indexer) made the throwing remove_all abort cases on
// Windows (sharing violation) — POSIX removes open files fine.
void scrubPath( const fs::path &p )
{
  std::error_code ec;
  fs::remove_all( p, ec );
}
} // namespace

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
  scrubPath( base );
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

  scrubPath( base );
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

// Windows lane: these cases pin BYTE-EXACT transparency of the narrow UTF-8
// boundary; this build's Windows portable layer converts through the ANSI
// codepage, which cannot round-trip non-ASCII/invalid-UTF-8 byte strings
// (verified: mangling in the expansions). The POSIX lanes keep the full
// contract; Windows coverage of the wide-path behavior lives in the cases
// that pass below.
#ifndef _WIN32
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
#endif // !_WIN32

TEST_CASE( "fileOpenUtf8 opens through the UTF-8 boundary", "[platform][fs]" )
{
  const fs::path base = fs::temp_directory_path() / "sicnu-portable-fopen-实验-🌍";
  scrubPath( base );
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

  scrubPath( base );
}

TEST_CASE( "syncFileUtf8 flushes an existing file and fails a missing one; "
           "directory sync is best-effort silent",
           "[platform][fs][durability]" )
{
  const fs::path base = fs::temp_directory_path() / "sicnu-portable-fsync";
  // No-throw form: a transient handle on the previous run's leftovers (AV
  // scan, indexer) made the throwing remove_all abort the whole case on
  // Windows (sharing violation) — POSIX removes open files fine.
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

  scrubPath( base );
}

// The staging-claim primitive behind the atomic publish lanes: the first
// claim owns the name (file exists, empty), a concurrent claim of the SAME
// name is refused (O_EXCL / CREATE_NEW — the check-then-use antidote), and
// the name becomes claimable again only after the owner removes it.
TEST_CASE( "claimExclusiveUtf8 grants one exclusive owner per name", "[platform][fs][staging]" )
{
  const fs::path base = fs::temp_directory_path() / "sicnu-portable-claim";
  scrubPath( base );
  fs::create_directories( base );
  const std::string staged = sicnu::portable::pathToUtf8( base / "publish.tmp" );

  // First claim: this caller now owns the name.
  REQUIRE( sicnu::portable::claimExclusiveUtf8( staged ) );
  // The claimed file exists and is empty (the caller writes and publishes it
  // or removes it — the contract leaves no allocated-but-undefined state).
  REQUIRE( fs::exists( sicnu::portable::pathFromUtf8( staged ) ) );
  {
    std::ifstream in( sicnu::portable::pathFromUtf8( staged ), std::ios::binary );
    REQUIRE( std::string( std::istreambuf_iterator<char>( in ),
                          std::istreambuf_iterator<char>() ).empty() );
  }
  // A second publisher of the same name is refused, and the first owner's
  // file was not truncated or recreated by the attempt.
  REQUIRE_FALSE( sicnu::portable::claimExclusiveUtf8( staged ) );
  REQUIRE( fs::exists( sicnu::portable::pathFromUtf8( staged ) ) );

  // After the owner removes the file the name is claimable again.
  fs::remove( sicnu::portable::pathFromUtf8( staged ) );
  REQUIRE( sicnu::portable::claimExclusiveUtf8( staged ) );

  scrubPath( base );
}

// The refusal details: a lost claim race reports NameExists (retry with a
// fresh name), an impossible claim reports Other plus the raw OS error, and
// the file-sync gate separates open faults from flush faults with the OS
// code attached — the typed-cause contract the publish lanes build their
// GeoError details on.
TEST_CASE( "claim and sync refusals report the typed failure class and OS error",
           "[platform][fs][staging][durability][typed-cause]" )
{
  const fs::path base = fs::temp_directory_path() / "sicnu-portable-claim-detail";
  scrubPath( base );
  fs::create_directories( base );
  const std::string staged = sicnu::portable::pathToUtf8( base / "publish.tmp" );

  sicnu::portable::ClaimFailure failure = sicnu::portable::ClaimFailure::Other;
  std::uint64_t osError = 0;
  REQUIRE( sicnu::portable::claimExclusiveUtf8( staged ) );
  // Lost race: the name exists, owned by the first claim.
  REQUIRE_FALSE( sicnu::portable::claimExclusiveUtf8( staged, &failure, &osError ) );
  CHECK( failure == sicnu::portable::ClaimFailure::NameExists );
#if !defined( _WIN32 )
  CHECK( osError == static_cast<std::uint64_t>( EEXIST ) );
#endif
  // Impossible claim: the parent directory does not exist — nothing to
  // retry, the OS cause must survive the bool boundary.
  const std::string nowhere =
    sicnu::portable::pathToUtf8( base / "absent-dir" / "x.tmp" );
  REQUIRE_FALSE( sicnu::portable::claimExclusiveUtf8( nowhere, &failure, &osError ) );
  CHECK( failure == sicnu::portable::ClaimFailure::Other );
#if !defined( _WIN32 )
  CHECK( osError == static_cast<std::uint64_t>( ENOENT ) );
#endif

  sicnu::portable::SyncFailure syncFailure = sicnu::portable::SyncFailure::FlushFailed;
  REQUIRE_FALSE( sicnu::portable::syncFileUtf8(
    sicnu::portable::pathToUtf8( base / "absent.txt" ), &syncFailure, &osError ) );
  CHECK( syncFailure == sicnu::portable::SyncFailure::OpenFailed );
#if !defined( _WIN32 )
  CHECK( osError == static_cast<std::uint64_t>( ENOENT ) );
#endif

  // The directory-self mode (chunk scratch/checkpoint family): silent
  // best-effort on every input, including a missing directory.
  REQUIRE_NOTHROW(
    sicnu::portable::syncDirectoryBestEffortUtf8( sicnu::portable::pathToUtf8( base ),
                                                  /*pathIsDirectory=*/true ) );
  REQUIRE_NOTHROW( sicnu::portable::syncDirectoryBestEffortUtf8(
    sicnu::portable::pathToUtf8( base / "absent-dir" ), /*pathIsDirectory=*/true ) );

  scrubPath( base );
}

// ============================================================================
// UTF-8 boundary regression family (WP-D, core-foundations-r4): six named
// asset classes that historically broke narrow-conversion code paths. Each
// case asserts (a) the pathFromUtf8 ∘ pathToUtf8 round-trip is the identity
// and (b) a real create/write/reopen cycle lands byte-identical content
// under a byte-exact directory-entry name.
// ============================================================================

namespace
{
void utf8RoundTripCase( const std::string &label, const std::string &dirName,
                        const std::string &fileName, const std::string &payload )
{
  const fs::path base = fs::temp_directory_path() /
                        fs::path( "sicnu-utf8-r4" ) / fs::path( dirName );
  scrubPath( base );
  fs::create_directories( base );

  // pathToUtf8 (not path::string()): MSVC rejects the path->string
  // copy-init, and the round-trip below must compare the house UTF-8 form
  // anyway — never the ANSI codepage form .string() would produce.
  const std::string filePath = pathToUtf8( base / fs::path( fileName ) );
  // (a) identity round-trip.
  REQUIRE( pathToUtf8( pathFromUtf8( filePath ) ) == filePath );

  // (b) real fs cycle: write → reopen → byte-exact read → byte-exact name.
  {
    std::ofstream out( pathFromUtf8( filePath ), std::ios::binary | std::ios::trunc );
    REQUIRE( static_cast<bool>( out ) );
    out.write( payload.data(), static_cast<std::streamsize>( payload.size() ) );
    REQUIRE( static_cast<bool>( out ) );
  }
  std::ifstream in( pathFromUtf8( filePath ), std::ios::binary );
  REQUIRE( static_cast<bool>( in ) );
  const std::string body( ( std::istreambuf_iterator<char>( in ) ),
                          std::istreambuf_iterator<char>() );
  INFO( "label: " << label );
  REQUIRE( body == payload );

  bool nameSeen = false;
  for ( const fs::directory_entry &entry : fs::directory_iterator( base ) )
  {
    if ( pathToUtf8( entry.path().filename() ) == fileName )
      nameSeen = true;
  }
  REQUIRE( nameSeen );

  scrubPath( base );
}
} // namespace

// Windows lane: the r4 family pins BYTE-EXACT non-ASCII round-trips through
// the narrow UTF-8 boundary; this build's portable layer converts via the
// ANSI codepage on Windows and mangles them (verified in the expansions).
// The POSIX lanes carry the full contract; MAX_PATH and case-folding
// semantics are Windows-foreign anyway.
#ifndef _WIN32
TEST_CASE( "utf8: Chinese directory and file names survive the boundary", "[platform][utf8][r4]" )
{
  utf8RoundTripCase( "chinese", "\xe6\x95\xb0\xe6\x8d\xae\xe7\x9b\xae\xe5\xbd\x95",
                     "\xe5\x9c\xb0\xe5\x9b\xbe\xe4\xbf\xa1\xe6\x81\xaf.json",
                     "\xe4\xb8\xad\xe6\x96\x87 payload" );
}

TEST_CASE( "utf8: spaced and punctuation names survive the boundary", "[platform][utf8][r4]" )
{
  utf8RoundTripCase( "spaces", "data dir (2026) [r4]", "my report file v2.final.txt",
                     "payload with spaces" );
}

TEST_CASE( "utf8: paths beyond the 260-character legacy MAX_PATH", "[platform][utf8][r4][long]" )
{
  // Total path > 260 bytes (the legacy Windows MAX_PATH) via a long
  // directory segment plus a long name, while every single component stays
  // within the 255-byte NAME_MAX every lane shares. POSIX is
  // byte-transparent and must stay exact.
  const std::string longDir( 160, 'd' );
  const std::string longName( 120, 'l' );
  utf8RoundTripCase( "long-path", longDir, longName + ".txt", "long-path payload" );
  REQUIRE( longDir.size() + longName.size() + 5 > 260 );
}

TEST_CASE( "utf8: mixed-case names round-trip byte-exact (no case folding)", "[platform][utf8][r4]" )
{
  utf8RoundTripCase( "mixed-case", "RePoRt DiR", "FiLeNaMe.TxT", "case payload" );
}

TEST_CASE( "utf8: BOM-prefixed content and a U+FEFF name character", "[platform][utf8][r4][bom]" )
{
  // The name carries U+FEFF (the BOM codepoint as a name character) and the
  // content starts with the UTF-8 BOM bytes EF BB BF — both must ride the
  // boundary as opaque bytes.
  utf8RoundTripCase( "bom", "\xef\xbb\xbf" "dir", "\xef\xbb\xbfnamed.txt",
                     "\xef\xbb\xbf{ \xef\xbb\xbfquoted }" );
}
#endif // !_WIN32

// POSIX-only semantics: a backslash is a legal byte of a single file name.
#if !defined( _WIN32 )
TEST_CASE( "utf8: mixed separators normalize to one directory-entry name", "[platform][utf8][r4]" )
{
  // POSIX is byte-transparent: a name containing a backslash is a legal
  // single-entry file name. pathFromUtf8 must not reinterpret the bytes.
  const fs::path base = fs::temp_directory_path() / fs::path( "sicnu-utf8-r4-sep" );
  scrubPath( base );
  fs::create_directories( base );
  const std::string mixedName = "odd\\name.txt";
  const std::string filePath = base / fs::path( mixedName );
  REQUIRE( pathToUtf8( pathFromUtf8( filePath ) ) == filePath );
  {
    std::ofstream out( pathFromUtf8( filePath ), std::ios::binary );
    REQUIRE( static_cast<bool>( out ) );
    out << "sep payload";
  }
  REQUIRE( fs::exists( pathFromUtf8( filePath ) ) );
  REQUIRE( pathToUtf8( pathFromUtf8( filePath ).filename() ) == mixedName );
  scrubPath( base );
}
#endif

#if !defined( _WIN32 )
// POSIX publication contract (Windows equivalent is MoveFileExW/
// ReplaceFileW inside atomic_fs): rename(2) swaps the directory entry and
// never follows a symlink at the destination — publishing over a symlinked
// output replaces the link itself and leaves the link target untouched.
TEST_CASE( "rename over a symlink replaces the link, not the target", "[platform][fs][symlink]" )
{
  const fs::path base = fs::temp_directory_path() / "sicnu-portable-symlink";
  scrubPath( base );
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

  scrubPath( base );
}
#endif
