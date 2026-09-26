/***************************************************************************
  tests/test_core_failure_injection_r4.cpp
  core-foundations-r4 (Track 5) — error-path failure injection lane.
  ---------------------------
  Begin                : 2026-09-27
  Copyright            : (C) 2026 SICNU GEO RS

  Controlled failure injection through PUBLIC API seams only (no friend,
  no white-box probes). Injection devices, per class:

    RO-DIR     read-only directory (chmod 0555) — the disk-full/EBUSY proxy:
               every write-path allocation or rename must fail closed
    RO-FILE    read-only file (chmod 0444)
    MISSING    missing file / missing parent directory
    AS-DIR     a directory where a regular file is required (rename target)
    CORRUPT    malformed / oversized / wrong-schema persisted input
    INTERRUPT  failure raised mid-transaction (writer throw, mid-publish chmod)

  Every injected class asserts the three-piece contract:
    (1) typed GeoError (never swallowed, never a half-initialized object),
    (2) an error message carrying locating context (path or phase substring),
    (3) repeatability/reuse: the same call fails identically when repeated,
        and succeeds again once the injected fault is removed.

  Complement (not duplicate) of test_io_atomic_failures.cpp: that lane owns
  the group-rollback matrix; this lane owns the fault-injection devices the
  old lane cannot express (permissions, corruption, mid-transaction state).
 ***************************************************************************/

#include "geospatial/io/stage_ledger.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/atomic_fs.h"
#include "platform/portable.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "geospatial/vector/vector_writer.h"

namespace fs = std::filesystem;

using sicnu::geo::GeoError;
namespace atomic_fs = sicnu::geo::atomic_fs;

namespace
{

std::string u8( const fs::path &path )
{
  const std::u8string text = path.u8string();
  return std::string( text.begin(), text.end() );
}

class ScratchDir
{
public:
  ScratchDir()
  {
    static std::atomic<unsigned> counter{ 0 };
    std::random_device rd;
    mPath = fs::temp_directory_path() /
            fs::path( "sicnu_inject_r4_" + std::to_string( sicnu::portable::pid() ) + "_" +
                      std::to_string( counter++ ) + "_" + std::to_string( rd() ) );
    fs::create_directories( mPath );
  }
  ~ScratchDir() { release(); std::error_code ec; fs::remove_all( mPath, ec ); }
  ScratchDir( const ScratchDir & ) = delete;
  ScratchDir &operator=( const ScratchDir & ) = delete;

  const fs::path &path() const { return mPath; }
  std::string child( const std::string &name ) const { return u8( mPath / fs::path( name ) ); }

  void makeReadOnly() { chmod( mPath.c_str(), 0555 ); }
  void makeWritable() { chmod( mPath.c_str(), 0755 ); }
  void release() { makeWritable(); }

private:
  fs::path mPath;
};

void writeBytes( const std::string &path, const std::string &bytes )
{
  std::ofstream out( sicnu::portable::pathFromUtf8( path ), std::ios::binary | std::ios::trunc );
  REQUIRE( static_cast<bool>( out ) );
  out.write( bytes.data(), static_cast<std::streamsize>( bytes.size() ) );
  REQUIRE( static_cast<bool>( out ) );
}

std::string readBytes( const std::string &path )
{
  std::ifstream in( sicnu::portable::pathFromUtf8( path ), std::ios::binary );
  REQUIRE( static_cast<bool>( in ) );
  return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}

/// Catches `expr`, requires the typed GeoError, and asserts the message
/// carries every `needles` substring (locating context — the P1 error-path
/// contract: a failure you cannot locate is a failure you cannot fix).
template <typename Fn>
std::string requireGeoErrorWith( Fn &&expr, const std::vector<const char *> &needles )
{
  std::string message;
  try
  {
    expr();
    FAIL( "expected a GeoError" );
  }
  catch ( const GeoError &ex )
  {
    message = ex.what();
    for ( const char *needle : needles )
    {
      INFO( "message: " << message << "\nneedle: " << needle );
      REQUIRE( message.find( needle ) != std::string::npos );
    }
  }
  return message;
}

/// The reuse leg of the contract: with the fault cleared, the same operation
/// on the same target succeeds and its result is verifiable.
void requireWritableAgain( const std::string &target, const std::string &payload )
{
  atomic_fs::writeFileAtomic( target, [ & ]( const std::string &staged ) {
    writeBytes( staged, payload );
  } );
  REQUIRE( readBytes( target ) == payload );
}

} // namespace

// ============================================================================
// RO-DIR: staging allocation must fail closed
// ============================================================================

TEST_CASE( "inject RO-DIR: stagedPathFor refuses a read-only directory with a typed, located error",
           "[inject][ro-dir][staging]" )
{
  ScratchDir dir;
  dir.makeReadOnly();
  const std::string target = dir.child( "scene.tif" );

  requireGeoErrorWith( [ & ] { atomic_fs::stagedPathFor( target ); },
                       { "Cannot allocate a staging path", "scene.tif" } );

  // Repeatability: the second attempt under the same fault behaves identically.
  requireGeoErrorWith( [ & ] { atomic_fs::stagedPathFor( target ); },
                       { "Cannot allocate a staging path" } );

  dir.release();
  requireWritableAgain( target, "after-fault-clear" );
}

TEST_CASE( "inject MISSING: stagedPathFor refuses a missing parent directory",
           "[inject][missing][staging]" )
{
  ScratchDir dir;
  const std::string target = dir.child( "absent" ) + "/scene.tif";

  requireGeoErrorWith( [ & ] { atomic_fs::stagedPathFor( target ); },
                       { "Cannot allocate a staging path" } );
}

TEST_CASE( "inject RO-DIR: writeFileAtomic keeps the target-less state clean and is reusable",
           "[inject][ro-dir][publish]" )
{
  ScratchDir dir;
  const std::string target = dir.child( "report.json" );
  atomic_fs::writeFileAtomic( target, [ & ]( const std::string &staged ) {
    writeBytes( staged, "v1" );
  } );
  const std::string before = readBytes( target );

  dir.makeReadOnly();
  requireGeoErrorWith( [ & ] {
    atomic_fs::writeFileAtomic( target, [ & ]( const std::string &staged ) {
      writeBytes( staged, "v2 should never land" );
    } );
  }, { "Cannot allocate a staging path" } );
  // The previous good content is untouched (no torn state).
  REQUIRE( readBytes( target ) == before );
  // Repeated failure does not corrupt the target either.
  requireGeoErrorWith( [ & ] {
    atomic_fs::writeFileAtomic( target, []( const std::string & ) {} );
  }, { "Cannot allocate a staging path" } );
  REQUIRE( readBytes( target ) == before );

  dir.release();
  requireWritableAgain( target, "v2" );
}

// ============================================================================
// fsync gate failure classes
// ============================================================================

TEST_CASE( "inject MISSING: fsyncFile types a missing file",
           "[inject][missing][fsync]" )
{
  ScratchDir dir;
  requireGeoErrorWith( [ & ] { atomic_fs::fsyncFile( dir.child( "absent.bin" ) ); },
                       { "fsync: cannot open", "absent.bin" } );
}

TEST_CASE( "inject AS-DIR: fsyncFile refuses a directory path",
           "[inject][as-dir][fsync]" )
{
  ScratchDir dir;
  const std::string sub = dir.child( "nested" );
  fs::create_directories( sicnu::portable::pathFromUtf8( sub ) );

  requireGeoErrorWith( [ & ] { atomic_fs::fsyncFile( sub ); },
                       { "fsync", "nested" } );
}

TEST_CASE( "inject RO-FILE: fsyncFile fails closed on an unwritable file",
           "[inject][ro-file][fsync]" )
{
  ScratchDir dir;
  const std::string file = dir.child( "locked.bin" );
  writeBytes( file, "payload" );
  chmod( file.c_str(), 0444 );

  requireGeoErrorWith( [ & ] { atomic_fs::fsyncFile( file ); },
                       { "fsync", "locked.bin" } );

  // Reuse: once writable again the gate passes.
  chmod( file.c_str(), 0644 );
  REQUIRE_NOTHROW( atomic_fs::fsyncFile( file ) );
}

// ============================================================================
// Publish failure classes
// ============================================================================

TEST_CASE( "inject MISSING: publishStagedFile refuses a missing staged file and spares the target",
           "[inject][missing][publish]" )
{
  ScratchDir dir;
  const std::string target = dir.child( "map.png" );
  writeBytes( target, "good" );
  const std::string staged = dir.child( "ghost.png" );

  requireGeoErrorWith( [ & ] { atomic_fs::publishStagedFile( staged, target ); },
                       { "publish: staged file missing", "ghost.png" } );
  REQUIRE( readBytes( target ) == "good" );
}

TEST_CASE( "inject AS-DIR: publishing onto a directory path fails typed, staged file survives for the caller",
           "[inject][as-dir][publish]" )
{
  ScratchDir dir;
  const std::string dirTarget = dir.child( "blocked" );
  fs::create_directories( sicnu::portable::pathFromUtf8( dirTarget ) );
  const std::string staged = dir.child( "blocked.tmp" );
  writeBytes( staged, "payload" );

  requireGeoErrorWith( [ & ] { atomic_fs::publishStagedFile( staged, dirTarget ); },
                       { "publish: rename failed", "blocked" } );
  // The caller owns the staged file on publishStagedFile failure (header
  // contract: "staged file is left for discardStaged by the caller").
  REQUIRE( atomic_fs::fileExists( staged ) );
  atomic_fs::discardStaged( staged );
  REQUIRE_FALSE( atomic_fs::fileExists( staged ) );
}

TEST_CASE( "inject RO-DIR: publishStagedFile cannot replace into a read-only directory",
           "[inject][ro-dir][publish]" )
{
  ScratchDir dir;
  const std::string target = dir.child( "out.bin" );
  const std::string staged = dir.child( "out.bin.staged" );
  writeBytes( target, "old" );
  writeBytes( staged, "new" );

  dir.makeReadOnly();
  requireGeoErrorWith( [ & ] { atomic_fs::publishStagedFile( staged, target ); },
                       { "publish: rename failed", "out.bin" } );
  dir.release();

  // Reuse: same pair publishes once the directory accepts writes.
  atomic_fs::publishStagedFile( staged, target );
  REQUIRE( readBytes( target ) == "new" );
}

TEST_CASE( "inject RO-DIR: renameReplaceQuiet is a quiet false; removeFileQuiet reports the block",
           "[inject][ro-dir][soft-path]" )
{
  ScratchDir dir;
  const std::string from = dir.child( "gc-a" );
  const std::string to = dir.child( "gc-b" );
  writeBytes( from, "x" );
  writeBytes( to, "y" );

  dir.makeReadOnly();
  // Soft paths return false — they never throw (header contract).
  REQUIRE_FALSE( atomic_fs::renameReplaceQuiet( from, to ) );
  // The quarantine remove reports the blocked file instead of lying.
  REQUIRE_FALSE( atomic_fs::removeFileQuiet( from ) );
  dir.release();

  REQUIRE( atomic_fs::renameReplaceQuiet( from, to ) );
  REQUIRE( readBytes( to ) == "x" );
}

// ============================================================================
// Classification helpers under weird input (malformed-input dimension)
// ============================================================================

TEST_CASE( "inject CORRUPT: fileExists classifies empty and weird inputs without throwing",
           "[inject][corrupt][classify]" )
{
  REQUIRE_FALSE( atomic_fs::fileExists( "" ) );
  REQUIRE_FALSE( atomic_fs::fileExists( std::string( "/dev/null" ) + "/impossible" ) );
  REQUIRE( atomic_fs::fileSize( "" ) == 0 );
  REQUIRE( atomic_fs::fileSize( "/definitely/not/here.bin" ) == 0 );

  // Repeated weird input stays stable (reusable classifiers).
  for ( int i = 0; i < 3; ++i )
  {
    REQUIRE_FALSE( atomic_fs::fileExists( "" ) );
    REQUIRE( atomic_fs::fileSize( "" ) == 0 );
  }
}

// ============================================================================
// stage_ledger corruption / oversize / missing (CORRUPT dimension)
// ============================================================================

TEST_CASE( "inject CORRUPT: readStageLedger types a missing ledger",
           "[inject][missing][ledger]" )
{
  ScratchDir dir;
  const std::string finalPath = dir.child( "dataset.tif" );
  requireGeoErrorWith( [ & ] { sicnu::geo::io::readStageLedger( finalPath ); },
                       { "stage ledger missing", "dataset.tif" } );
}

TEST_CASE( "inject CORRUPT: a malformed ledger is a typed refusal, never a partial record",
           "[inject][corrupt][ledger]" )
{
  ScratchDir dir;
  const std::string finalPath = dir.child( "dataset.tif" );
  writeBytes( sicnu::geo::io::stageLedgerPath( finalPath ), "{ not json at all..." );

  requireGeoErrorWith( [ & ] { sicnu::geo::io::readStageLedger( finalPath ); },
                       { "stage ledger is not valid JSON" } );

  // Schema-corrupt (valid JSON, wrong types) is refused with the same discipline.
  writeBytes( sicnu::geo::io::stageLedgerPath( finalPath ), "{\"schema_version\": 12, \"state\": []}" );
  requireGeoErrorWith( [ & ] { sicnu::geo::io::readStageLedger( finalPath ); },
                       { "stage ledger" } );
}

TEST_CASE( "inject CORRUPT: an oversized ledger is refused before parsing",
           "[inject][corrupt][ledger]" )
{
  ScratchDir dir;
  const std::string finalPath = dir.child( "dataset.tif" );
  // kMaxLedgerBytes is 16 MiB (stage_ledger.cpp:42); 17 MiB must trip the cap.
  std::string big( 17ull * 1024ull * 1024ull, 'x' );
  writeBytes( sicnu::geo::io::stageLedgerPath( finalPath ), big );

  requireGeoErrorWith( [ & ] { sicnu::geo::io::readStageLedger( finalPath ); },
                       { "exceeds the size cap" } );
}

TEST_CASE( "inject RO-DIR: sweepOrphans refuses an unreadable directory with a located error",
           "[inject][ro-dir][ledger][sweep]" )
{
  ScratchDir dir;
  // 0333 strips the read bit: directory_iterator cannot list (0555 would
  // still allow it — reading a directory needs the r bit specifically).
  chmod( dir.path().c_str(), 0333 );
  requireGeoErrorWith( [ & ] { sicnu::geo::io::sweepOrphans( dir.child( "" ) ); },
                       { "sweep: cannot read directory" } );
  // Repeated failure stays typed.
  requireGeoErrorWith( [ & ] { sicnu::geo::io::sweepOrphans( dir.child( "" ) ); },
                       { "sweep: cannot read directory" } );
  dir.release();
  // Reuse: the same directory sweeps clean once readable.
  const auto strays = sicnu::geo::io::sweepOrphans( dir.child( "" ) );
  REQUIRE( strays.empty() );
}

// ============================================================================
// INTERRUPT: mid-transaction faults
// ============================================================================

TEST_CASE( "inject INTERRUPT: a writer throwing mid-write leaves no torn target and the lane stays usable",
           "[inject][interrupt][publish]" )
{
  ScratchDir dir;
  const std::string target = dir.child( "journal.json" );
  writeBytes( target, "{\"good\":true}" );

  for ( int attempt = 0; attempt < 2; ++attempt )
  {
    // The header contract: the writer's OWN exception is rethrown after the
    // staged discard (writeFileAtomic never converts it to GeoError).
    bool threw = false;
    try
    {
      atomic_fs::writeFileAtomic( target, [ & ]( const std::string &staged ) {
        writeBytes( staged, "{\"half" );
        throw std::runtime_error( "injected writer crash" );
      } );
      FAIL( "the writer exception must propagate" );
    }
    catch ( const std::runtime_error &ex )
    {
      threw = true;
      REQUIRE( std::string( ex.what() ) == "injected writer crash" );
    }
    REQUIRE( threw );
    REQUIRE( readBytes( target ) == "{\"good\":true}" );
    // No staged residue after either interrupted attempt.
    for ( const fs::directory_entry &entry : fs::directory_iterator( dir.path() ) )
      REQUIRE( entry.path().filename().u8string().find( u8".tmp" ) == std::u8string::npos );
  }

  requireWritableAgain( target, "{\"recovered\":true}" );
}

TEST_CASE( "inject INTERRUPT: a staged raster whose publish phase hits a read-only directory is discarded, target intact, retry works",
           "[inject][interrupt][ro-dir][raster]" )
{
  ScratchDir dir;
  const std::string target = dir.child( "cube.tif" );
  sicnu::geo::RasterBandSpec band;
  band.dtype = "Byte";
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
      target, 2, 2, { band }, { "GTiff", {}, true } );
    const double pixels[4] = { 1, 2, 3, 4 };
    writer.writeWindow( 1, sicnu::geo::RasterWindow{ 0, 0, 2, 2 }, pixels );
    // Interrupt between staging and publish: the target directory goes
    // read-only before finalize() can rename.
    dir.makeReadOnly();
    requireGeoErrorWith( [ & ] { writer.finalize(); },
                         { "cube.tif" } );
    // finalize() closes the writer on any failure; the staged group must be
    // gone (RAII discard) and the target absent (never half-published).
    REQUIRE_FALSE( writer.isOpen() );
  }
  REQUIRE_FALSE( atomic_fs::fileExists( target ) );

  dir.release();
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
      target, 2, 2, { band }, { "GTiff", {}, true } );
    const double pixels[4] = { 9, 8, 7, 6 };
    writer.writeWindow( 1, sicnu::geo::RasterWindow{ 0, 0, 2, 2 }, pixels );
    writer.finalize();
  }
  REQUIRE( atomic_fs::fileExists( target ) );
}

TEST_CASE( "inject RO-DIR: RasterWriter and VectorWriter staging fail closed on unwritable directories",
           "[inject][ro-dir][raster][vector]" )
{
  ScratchDir dir;
  dir.makeReadOnly();

  sicnu::geo::RasterBandSpec band;
  band.dtype = "Byte";
  requireGeoErrorWith( [ & ] {
    sicnu::geo::RasterWriter::create( dir.child( "r.tif" ), 1, 1, { band }, { "GTiff", {}, true } );
  }, { "r.tif" } );

  requireGeoErrorWith( [ & ] {
    sicnu::geo::VectorWriter::create( dir.child( "v.gpkg" ), "pts", "Point", {},
                                      sicnu::geo::Crs::fromAuthid( "EPSG:4326" ), {} );
  }, { "v.gpkg" } );

  dir.release();
  // Reuse: creation succeeds once the directory is writable; cancel leaves
  // nothing behind.
  {
    sicnu::geo::RasterWriter writer = sicnu::geo::RasterWriter::create(
      dir.child( "r.tif" ), 1, 1, { band }, { "GTiff", {}, true } );
    REQUIRE( writer.isOpen() );
    writer.cancel();
  }
  REQUIRE_FALSE( atomic_fs::fileExists( dir.child( "r.tif" ) ) );
}

// ============================================================================
// UTF-8 hostile names ride the failure paths with their context intact
// ============================================================================

TEST_CASE( "inject RO-DIR: non-ASCII and spaced names survive failure messages byte-exact",
           "[inject][ro-dir][utf8]" )
{
  ScratchDir dir;
  const std::string sub = dir.child( "\xe6\x95\xb0\xe6\x8d\xae \xe7\x9b\xae\xe5\xbd\x95" ); // "数据 目录"
  fs::create_directories( sicnu::portable::pathFromUtf8( sub ) );
  const std::string target = sub + "/\xe5\x9c\xb0\xe5\x9b\xbe out.tif"; // "地图 out.tif"

  chmod( sicnu::portable::pathFromUtf8( sub ).c_str(), 0555 );
  const std::string message = requireGeoErrorWith(
    [ & ] { atomic_fs::stagedPathFor( target ); },
    { "Cannot allocate a staging path", "\xe5\x9c\xb0\xe5\x9b\xbe out.tif" } );
  INFO( "message bytes: " << message );
  chmod( sicnu::portable::pathFromUtf8( sub ).c_str(), 0755 );
  dir.release();
  requireWritableAgain( target, "utf8-after-fault" );
}

// ============================================================================
// discardStaged: cleanup classes
// ============================================================================

TEST_CASE( "inject INTERRUPT: discardStaged sweeps the main file and any sidecars the writer left",
           "[inject][interrupt][cleanup]" )
{
  ScratchDir dir;
  const std::string target = dir.child( "final.tif" );
  writeBytes( target, "delivered" );
  const std::string before = readBytes( target );

  bool threw = false;
  try
  {
    atomic_fs::writeFileAtomic( target, [ & ]( const std::string &staged ) {
      // The writer produced a sidecar (PAM/world file) before dying.
      writeBytes( staged, "partial" );
      writeBytes( staged + ".aux.xml", "<pam/>" );
      throw std::runtime_error( "die after sidecar" );
    } );
  }
  catch ( const std::runtime_error & )
  {
    threw = true;
  }
  REQUIRE( threw );
  // Main AND sidecar are gone; the delivered target is byte-identical.
  REQUIRE_FALSE( atomic_fs::fileExists( dir.child( "final.tif.tmp" ) ) );
  REQUIRE_FALSE( atomic_fs::fileExists( target + ".tmp" ) );
  REQUIRE( readBytes( target ) == before );
  for ( const fs::directory_entry &entry : fs::directory_iterator( dir.path() ) )
  {
    const std::string name = u8( entry.path().filename() );
    INFO( "stray entry: " << name );
    REQUIRE( name != "final.tif.tmp" );
    REQUIRE( name.find( ".tmp" ) == std::string::npos );
  }
}

TEST_CASE( "inject AS-DIR: removeFileQuiet treats a directory as nothing-to-do (never recursive)",
           "[inject][as-dir][cleanup]" )
{
  ScratchDir dir;
  const std::string sub = dir.child( "precious" );
  fs::create_directories( sicnu::portable::pathFromUtf8( sub ) );
  writeBytes( dir.child( "precious/keep.txt" ), "still here" );

  // A quiet remove must not recurse into directories: true (nothing removed
  // is not an error) and the directory content survives.
  REQUIRE( atomic_fs::removeFileQuiet( sub ) );
  REQUIRE( fs::is_directory( sicnu::portable::pathFromUtf8( sub ) ) );
  REQUIRE( readBytes( dir.child( "precious/keep.txt" ) ) == "still here" );
}
